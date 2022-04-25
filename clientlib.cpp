
#include <fcntl.h>
#include <grpcpp/client_context.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/support/status.h>
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

using grpc::ClientContext;

class SkinnyClient {
 public:
  SkinnyClient()
      : channel(grpc::CreateChannel("0.0.0.0:23456",
                                    grpc::InsecureChannelCredentials())),
        stub_(skinny::Skinny::NewStub(channel)),
        stub_cb_(skinny::SkinnyCb::NewStub(channel)),
        kathread(([this]() {
          StartSessionOrDie();
          return [this]() {
            while (true) {
              KeepAlive();
            }
          };
        })()) {}

  int Open(const std::string &path,
           const std::optional<std::function<void()>> &cb = std::nullopt) {
    skinny::OpenReq req;
    ClientContext context;
    skinny::Handle res;
    req.set_path(path);
    req.set_session_id(session_id);
    req.set_subscribe(cb.has_value());
    auto status = stub_->Open(&context, req, &res);
    assert(status.ok());
    auto fh = res.fh();
    if (cb) {
      callbacks[fh] = cb.value();
    }
    return fh;
  }

  std::string GetContent(int fh) {
    skinny::GetContentReq req;
    ClientContext context;
    skinny::Content res;
    req.set_session_id(session_id);
    req.set_fh(fh);
    auto status = stub_->GetContent(&context, req, &res);
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
    auto status = stub_->SetContent(&context, req, &res);
    assert(status.ok());
  }

  bool TryAcquire(int fh, bool ex) {
    skinny::LockAcqReq req;
    ClientContext context;
    skinny::Response res;
    req.set_session_id(session_id);
    req.set_fh(fh);
    req.set_ex(ex);
    auto status = stub_->TryAcquire(&context, req, &res);
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
    auto status = stub_->Acquire(&context, req, &res);
    assert(status.ok());
    return (res.res() == 0);
  }

  void Release(int fh) {
    skinny::LockRelReq req;
    ClientContext context;
    skinny::Response res;
    req.set_session_id(session_id);
    req.set_fh(fh);
    auto status = stub_->Release(&context, req, &res);
    assert(status.ok());
  }

 private:
  void KeepAlive() {
    skinny::SessionId req;
    skinny::Event res;
    ClientContext context;
    req.set_session_id(session_id);

    std::promise<grpc::Status> status_promise;
    std::future status_future = status_promise.get_future();
    stub_cb_->async()->KeepAlive(&context, &req, &res,
                                 [&status_promise](grpc::Status s) {
                                   status_promise.set_value(std::move(s));
                                 });

    if (auto status = status_future.get(); status.ok()) {
      if (!res.has_fh()) return;
      if (auto it = callbacks.find(res.fh()); it != callbacks.end()) {
        std::invoke(it->second);
      }
    } else {
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
    }
  }

  void StartSessionOrDie() {
    skinny::Empty req;
    ClientContext context;
    skinny::SessionId res;
    auto status = stub_->StartSession(&context, req, &res);

    std::cout << status.error_message() << std::endl;
    std::cout << status.error_code() << std::endl;
    assert(status.ok());
    session_id = res.session_id();
    std::cerr << "session id: " << session_id << std::endl;
    return;
  }

  std::shared_ptr<grpc::Channel> channel;
  // TODO: Maybe not an unordered_map
  std::unordered_map<int, std::function<void()>> callbacks;
  std::unique_ptr<skinny::Skinny::Stub> stub_;
  std::unique_ptr<skinny::SkinnyCb::Stub> stub_cb_;
  int session_id;
  std::thread kathread;
};
