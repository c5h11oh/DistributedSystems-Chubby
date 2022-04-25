
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
#include <iostream>
#include <memory>
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

  int Open(const std::string &path) { return OpenImpl(path, false); }

  int Open(const std::string &path, const std::function<void()> &cb) {
    auto fh = OpenImpl(path, true);
    callbacks[fh] = cb;
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
    skinny::LockRes res;
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
    skinny::LockRes res;
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
    skinny::LockRes res;
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

    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    grpc::Status status;
    stub_cb_->async()->KeepAlive(&context, &req, &res,
                                 [&mu, &cv, &done, &status](grpc::Status s) {
                                   status = std::move(s);
                                   std::lock_guard<std::mutex> lock(mu);
                                   done = true;
                                   cv.notify_one();
                                 });

    std::unique_lock<std::mutex> lock(mu);
    while (!done) {
      cv.wait(lock);
    }

    if (status.ok()) {
      if (res.has_fh()) {
        if (auto it = callbacks.find(res.fh()); it != callbacks.end()) {
          std::invoke(it->second);
        }
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

  int OpenImpl(const std::string &path, const bool subscribe) {
    skinny::OpenReq req;
    ClientContext context;
    skinny::Handle res;
    req.set_path(path);
    req.set_session_id(session_id);
    req.set_subscribe(subscribe);
    auto status = stub_->Open(&context, req, &res);
    assert(status.ok());
    return res.fh();
  }

  std::shared_ptr<grpc::Channel> channel;
  // TODO: Maybe not an unordered_map
  std::unordered_map<int, std::function<void()>> callbacks;
  std::unique_ptr<skinny::Skinny::Stub> stub_;
  std::unique_ptr<skinny::SkinnyCb::Stub> stub_cb_;
  int session_id;
  std::thread kathread;
};
