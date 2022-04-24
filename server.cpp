#include "includes/skinny.grpc.pb.h"

#include "Session.cpp"
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/server_context.h>
#include <memory>
#include <signal.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

class FileMetaData {};
std::unordered_map<std::string, std::pair<FileMetaData, std::string>> data;

class SkinnyImpl final : public skinny::Skinny::Service {
  Status Open(ServerContext *context, const skinny::OpenReq *req,
              skinny::Handle *res) {
    auto session = find_session(req->session_id());
    if (data.find(req->path()) == data.end()) {
      data[req->path()];
    }
    auto fh = session->add_new_handle(req->path());
    res->set_fh(fh);
    return Status::OK;
  }

  Status GetContent(ServerContext *context, const skinny::GetContentReq *req,
                    skinny::Content *res) {
    auto session = find_session(req->session_id());
    auto [meta, content] = data.at(session->fh_to_key(req->fh()));
    res->set_content(content);
    return Status::OK;
  }

  Status SetContent(ServerContext *context, const skinny::SetContentReq *req,
                    skinny::Empty *) {
    auto session = find_session(req->session_id());
    data.at(session->fh_to_key(req->fh())).second = req->content();
    return Status::OK;
  }

  Status StartSession(ServerContext *context, const skinny::Empty *,
                      skinny::SessionId *res) {
    // TODO: session ID
    auto session = std::make_shared<Session>();
    session_db[session->id] = session;
    res->set_session_id(session->id);
    return Status::OK;
  }

  std::unordered_map<int, std::shared_ptr<Session>> session_db;
  std::shared_ptr<Session> find_session(int id) {
    auto it = session_db.find(id);
    assert(it != session_db.end());
    return it->second;
  }
};

int main() {
  std::string server_address("localhost:50051");
  SkinnyImpl service;
  grpc::ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.SetMaxSendMessageSize(INT_MAX);
  builder.SetMaxReceiveMessageSize(INT_MAX);
  builder.SetMaxMessageSize(INT_MAX);

  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}
