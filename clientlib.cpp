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
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "grpcpp/channel.h"
#include "grpcpp/create_channel.h"
#include "grpcpp/security/credentials.h"
#include "includes/skinny.grpc.pb.h"
#include "includes/skinny.pb.h"
#include "srv_config.h"
using grpc::ClientContext;

class SkinnyClient::impl {
 public:
  impl()
      : kathread(std::invoke(([this]() {
          StartSessionOrDie();
          return [this]() {
            while (true) {
              KeepAlive();
            }
          };
        }))) {}

  ~impl() {
    ClientContext context;
    skinny::SessionId req;
    skinny::Empty res;
    req.set_session_id(session_id);
    stub_->EndSession(&context, req, &res);
  }

  int Open(const std::string &path,
           const std::optional<std::function<void()>> &cb = std::nullopt) {
    skinny::OpenReq req;
    skinny::Handle res;
    req.set_path(path);
    req.set_session_id(session_id);
    req.set_subscribe(cb.has_value());
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
  }

  std::string GetContent(int fh) {
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
    return res.content();
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

  void KeepAlive() {
    using namespace std::chrono_literals;
    skinny::SessionId req;
    skinny::Event res;
    ClientContext context;
    auto deadline = std::chrono::system_clock::now() + 10s;
    context.set_deadline(deadline);
    req.set_session_id(session_id);

    std::promise<grpc::Status> status_promise;
    std::future status_future = status_promise.get_future();
    stub_cb_->async()->KeepAlive(&context, &req, &res,
                                 [&status_promise](grpc::Status s) {
                                   status_promise.set_value(std::move(s));
                                 });

    if (auto status = status_future.get(); status.ok()) {
      if (has_conn_.load() == 0) {
        has_conn_ = 1;
        has_conn_.notify_all();
      }
      if (!res.has_fh()) return;
      if (auto it = callbacks.find(res.fh()); it != callbacks.end()) {
        std::invoke(it->second);
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
  std::unordered_map<int, std::function<void()>> callbacks;
  std::unique_ptr<skinny::Skinny::Stub> stub_;
  std::unique_ptr<skinny::SkinnyCb::Stub> stub_cb_;
  int session_id;
  std::atomic<bool> has_conn_;

  std::thread kathread;
};

SkinnyClient::SkinnyClient() { pImpl = std::make_unique<impl>(); };
SkinnyClient::~SkinnyClient() = default;
int SkinnyClient::Open(const std::string &path,
                       const std::optional<std::function<void()>> &cb) {
  return pImpl->Open(path, cb);
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
