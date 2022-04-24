
#include <cassert>
#include <fcntl.h>
#include <grpcpp/client_context.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/support/status.h>
#include <signal.h>
#include <sys/stat.h>

#include "includes/skinny.grpc.pb.h"
#include "includes/skinny.pb.h"
#include <iostream>
#include <memory>
#include <string>

using grpc::ClientContext;

class SkinnyClient {
public:
  SkinnyClient() {
    const std::string target_str = "0.0.0.0:50051";
    grpc::ChannelArguments ch_args;
    ch_args.SetMaxReceiveMessageSize(INT_MAX);
    ch_args.SetMaxSendMessageSize(INT_MAX);
    auto channel = grpc::CreateCustomChannel(
        target_str, grpc::InsecureChannelCredentials(), ch_args);
    stub_ = skinny::Skinny::NewStub(channel);
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
    return;
  }

  int Open(const std::string &path) {
    skinny::OpenReq req;
    ClientContext context;
    skinny::Handle res;
    req.set_path(path);
    req.set_session_id(session_id);
    auto status = stub_->Open(&context, req, &res);
    assert(status.ok());
    return res.fh();
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

private:
  std::unique_ptr<skinny::Skinny::Stub> stub_;
  int session_id;
};
