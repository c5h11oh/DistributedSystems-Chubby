#include "clientlib.h"

#include <fcntl.h>
#include <grpcpp/client_context.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/support/status.h>
#include <grpcpp/support/status_code_enum.h>
#include <signal.h>
#include <sys/stat.h>

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "grpcpp/channel.h"
#include "grpcpp/create_channel.h"
#include "grpcpp/security/credentials.h"
#include "includes/diagnostic.grpc.pb.h"
#include "includes/diagnostic.pb.h"
#include "includes/skinny.grpc.pb.h"
#include "includes/skinny.pb.h"
#include "srv_config.h"
using grpc::ClientContext;

class SkinnyClient::impl {
  enum class KeepAliveState { ON_GOING, DONE, KILLED };

 public:
  impl()
      : kathread(std::invoke(([this]() {
          StartSessionOrDie();
          return [this]() {
            std::optional<int> eid = std::nullopt;
            bool cont = true;
            while (std::tie(cont, eid) = KeepAlive(eid), cont)
              ;
          };
        }))) {}

  ~impl() {
    ClientContext context;
    skinny::SessionId req;
    skinny::Empty res;
    req.set_session_id(session_id);
    stub_->EndSession(&context, req, &res);
    has_active_keep_alive.store(KeepAliveState::KILLED);
    has_active_keep_alive.notify_one();
    kathread.join();
  }

  int Open(const std::string &path,
           const std::optional<std::function<void(int)>> &cb = std::nullopt,
           bool is_directory = false) {
    skinny::OpenReq req;
    skinny::Handle res;
    req.set_path(path);
    req.set_session_id(session_id);
    req.set_is_directory(is_directory);
    auto status = InvokeRpc([&]() {
      ClientContext context;
      return stub_->Open(&context, req, &res);
    });
    assert(status.ok());
    auto fh = res.fh();
    if (cb) {
      callbacks[fh] = cb.value();
    }
    return fh;
  }

  void Close(int fh) {
    skinny::CloseReq req;
    skinny::Empty res;
    req.set_session_id(session_id);
    req.set_fh(fh);
    auto status = InvokeRpc([&]() {
      ClientContext context;
      return stub_->Close(&context, req, &res);
    });
    {
      std::lock_guard lg(cache_lock_);
      if (auto it = cache_.find(fh); it != cache_.end()) {
        cache_.erase(it);
      }
    }
  }

  std::string GetContent(int fh) {
    {
      std::lock_guard lg(cache_lock_);
      if (auto it = cache_.find(fh);
          has_conn_.load() == true && it != cache_.end()) {
        std::cout << "Read from cache" << std::endl;
        return it->second;
      } else {
        std::cout << "Send read request" << std::endl;
      }
    }
    skinny::GetContentReq req;
    ClientContext context;
    skinny::Content res;
    req.set_session_id(session_id);
    req.set_fh(fh);
    auto status = InvokeRpc([&]() {
      ClientContext context;
      return stub_->GetContent(&context, req, &res);
    });
    assert(status.ok());
    return cache_[fh] = res.content();
  }

  void SetContent(int fh, const std::string &content) {
    skinny::SetContentReq req;
    ClientContext context;
    skinny::Empty res;
    req.set_session_id(session_id);
    req.set_content(content);
    req.set_fh(fh);
    auto status = InvokeRpc([&]() {
      ClientContext context;
      return stub_->SetContent(&context, req, &res);
    });
    assert(status.ok());
  }

  bool TryAcquire(int fh, bool ex) {
    skinny::LockAcqReq req;
    ClientContext context;
    skinny::Response res;
    req.set_session_id(session_id);
    req.set_fh(fh);
    req.set_ex(ex);
    auto status = InvokeRpc([&]() {
      ClientContext context;
      return stub_->TryAcquire(&context, req, &res);
    });
    assert(status.ok());
    return (res.res() == 0);
  }

  bool Acquire(int fh, bool ex) {
    skinny::LockAcqReq req;
    ClientContext context;
    skinny::Response res;
    req.set_session_id(session_id);
    req.set_fh(fh);
    req.set_ex(ex);
    auto status = InvokeRpc([&]() {
      ClientContext context;
      return stub_->Acquire(&context, req, &res);
    });
    assert(status.ok());
    return (res.res() == 0);
  }

  void Release(int fh) {
    skinny::LockRelReq req;
    ClientContext context;
    skinny::Response res;
    req.set_session_id(session_id);
    req.set_fh(fh);
    auto status = InvokeRpc([&]() {
      ClientContext context;
      return stub_->Release(&context, req, &res);
    });
    assert(status.ok());
  }

  void Delete(int fh) {
    skinny::DeleteReq req;
    ClientContext context;
    skinny::Response res;
    req.set_session_id(session_id);
    req.set_fh(fh);
    auto status = InvokeRpc([&]() {
      ClientContext context;
      return stub_->Delete(&context, req, &res);
    });
    assert(status.ok());
  }

 private:
  grpc::Status InvokeRpc(std::function<grpc::Status()> &&fun) {
    while (true) {
      while (has_conn_.load() == 0) has_conn_.wait(0);
      grpc::Status status = std::invoke(fun);
      if (!(status.error_code() ==
                static_cast<grpc::StatusCode>(skinny::ErrorCode::NOT_LEADER) ||
            status.error_code() == grpc::StatusCode::UNAVAILABLE)) {
        return status;
      }
    }
  }

  std::pair<bool, std::optional<int>> KeepAlive(std::optional<int> eid) {
    using namespace std::chrono_literals;
    skinny::KeepAliveReq req;
    skinny::Event res;
    ClientContext context;
    auto deadline = std::chrono::system_clock::now() + 10s;
    context.set_deadline(deadline);
    req.set_session_id(session_id);
    if (eid) req.set_acked_event(eid.value());

    grpc::Status status;
    auto done = KeepAliveState::DONE;
    has_active_keep_alive.compare_exchange_weak(done, KeepAliveState::ON_GOING);
    stub_cb_->async()->KeepAlive(&context, &req, &res,
                                 [this, &status](grpc::Status s) {
                                   auto ongoing = KeepAliveState::ON_GOING;
                                   status = s;
                                   has_active_keep_alive.compare_exchange_weak(
                                       ongoing, KeepAliveState::DONE);
                                   has_active_keep_alive.notify_one();
                                 });

    while (has_active_keep_alive.load() == KeepAliveState::ON_GOING)
      has_active_keep_alive.wait(KeepAliveState::ON_GOING);
    if (has_active_keep_alive.load() == KeepAliveState::KILLED) {
      context.TryCancel();
      return {false, std::nullopt};
    }
    std::optional<int> new_eid = std::nullopt;
    if (status.ok()) {
      if (has_conn_.load() == 0) {
        has_conn_ = 1;
        has_conn_.notify_all();
      }
      if (!res.has_fh()) return {true, std::nullopt};
      new_eid = res.event_id();
      if (auto it = callbacks.find(res.fh()); it != callbacks.end()) {
        std::invoke(it->second, res.fh());
      }
      if (auto it = cache_.find(res.fh()); it != cache_.end()) {
        std::cout << "invalid cache " << res.fh() << std::endl;
        cache_.erase(it);
      }
    } else {
      has_conn_ = 0;
      int next_server_id = cur_srv_id + 1;
      if (status.error_code() == grpc::StatusCode::CANCELLED) {
        if (auto new_leader = std::stoi(status.error_message());
            new_leader != -1)
          next_server_id = new_leader;
      }
      change_server(next_server_id);
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
      std::this_thread::sleep_for(500ms);
    }
    return {true, new_eid};
  }

  void StartSessionOrDie() {
    for (int i = 0; i < SRV_CONFIG.size(); ++i) {
      change_server(i);
      skinny::Empty req;
      ClientContext context;
      skinny::SessionId res;
      auto status = stub_->StartSession(&context, req, &res);
      if (status.ok()) {
        session_id = res.session_id();
        has_conn_ = true;
        std::cerr << "session id: " << session_id << std::endl;
        return;
      }
    }
    assert(false);
    std::terminate();
  }

  void change_server(int server_id) {
    assert(server_id >= 0);
    cur_srv_id = server_id % SRV_CONFIG.size();
    channel = grpc::CreateChannel(
        std::get<0>(SRV_CONFIG[cur_srv_id]) + ":" +
            std::to_string(std::get<1>(SRV_CONFIG[cur_srv_id]) + 1),
        grpc::InsecureChannelCredentials());
    stub_ = skinny::Skinny::NewStub(channel);
    stub_cb_ = skinny::SkinnyCb::NewStub(channel);
    return;
  }

  int cur_srv_id;
  std::shared_ptr<grpc::Channel> channel;
  // TODO: Maybe not an unordered_map
  std::unordered_map<int, std::function<void(int)>> callbacks;
  std::mutex cache_lock_;
  std::unordered_map<int, std::string> cache_;
  std::unique_ptr<skinny::Skinny::Stub> stub_;
  std::unique_ptr<skinny::SkinnyCb::Stub> stub_cb_;
  int session_id;
  std::atomic<bool> has_conn_;
  std::atomic<KeepAliveState> has_active_keep_alive;

  std::thread kathread;
};

SkinnyClient::SkinnyClient() { pImpl = std::make_unique<impl>(); };
SkinnyClient::~SkinnyClient() = default;
int SkinnyClient::Open(const std::string &path,
                       const std::optional<std::function<void(int)>> &cb) {
  return pImpl->Open(path, cb);
};
int SkinnyClient::OpenDir(const std::string &path,
                          const std::optional<std::function<void(int)>> &cb) {
  return pImpl->Open(path, cb, true);
};
std::string SkinnyClient::GetContent(int fh) { return pImpl->GetContent(fh); };
void SkinnyClient::SetContent(int fh, const std::string &content) {
  return pImpl->SetContent(fh, content);
}
bool SkinnyClient::TryAcquire(int fh, bool ex) {
  return pImpl->TryAcquire(fh, ex);
}
void SkinnyClient::Release(int fh) { return pImpl->Release(fh); }
bool SkinnyClient::Acquire(int fh, bool ex) { return pImpl->Acquire(fh, ex); }
void SkinnyClient::Close(int fh) { return pImpl->Close(fh); }
void SkinnyClient::Delete(int fh) { return pImpl->Delete(fh); }

SkinnyDiagnosticClient::SkinnyDiagnosticClient() {
  for (auto &[host, port] : SRV_CONFIG) {
    stubs_.push_back(diagnostic::Diagnostic::NewStub(
        grpc::CreateChannel(host + ":" + std::to_string(port + 1),
                            grpc::InsecureChannelCredentials())));
  }
}

int SkinnyDiagnosticClient::GetLeader() {
  using namespace std::chrono_literals;
  int idx = 0;
  while (true) {
    diagnostic::Empty req;
    diagnostic::Leader res;
    ClientContext context;
    auto deadline = std::chrono::system_clock::now() + 1s;
    context.set_deadline(deadline);
    grpc::Status status = stubs_[idx]->GetLeader(&context, req, &res);
    if (status.ok()) {
      return res.leader();
    }
    idx = (idx + 1) % stubs_.size();
  }
}
