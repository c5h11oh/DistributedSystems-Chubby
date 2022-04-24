
#include <fcntl.h>
#include <grpcpp/client_context.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/support/status.h>
#include <signal.h>
#include <sys/stat.h>

#include "includes/hello.grpc.pb.h"
#include "includes/hello.pb.h"
#include <iostream>
#include <memory>
#include <string>

using grpc::ClientContext;

class GRPCClient {
public:
  GRPCClient(std::shared_ptr<grpc::Channel> channel)
      : stub_(hafs::gRPCService::NewStub(channel)) {}

  std::string hi(const std::string &s) {
    hafs::ClientRequest request;
    request.set_str("World");
    ClientContext context;
    hafs::ServerResponse reply;
    auto status = stub_->hi(&context, request, &reply);
    if (status.ok()) {
      return reply.str();
    } else {
      return "Failed";
    }
  }

private:
  std::unique_ptr<hafs::gRPCService::Stub> stub_;
};

int main(int argc, char *argv[]) {
  const std::string target_str = "0.0.0.0:50051";
  grpc::ChannelArguments ch_args;

  ch_args.SetMaxReceiveMessageSize(INT_MAX);
  ch_args.SetMaxSendMessageSize(INT_MAX);

  GRPCClient gRPCClient(grpc::CreateCustomChannel(
      target_str, grpc::InsecureChannelCredentials(), ch_args));

  std::cout << gRPCClient.hi("world") << std::endl;

  return 0;
}