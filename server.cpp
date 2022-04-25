#include "includes/skinny.grpc.pb.h"

#include "SessionDb.cpp"
#include "includes/skinny.pb.h"
#include <exception>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/server_callback.h>
#include <grpcpp/support/sync_stream.h>
#include <memory>
#include <signal.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerUnaryReactor;
using grpc::ServerWriter;
using grpc::Status;

class FileMetaData {};
std::unordered_map<std::string, std::pair<FileMetaData, std::string>> data;

class SkinnyImpl final : public skinny::Skinny::Service {
public:
  explicit SkinnyImpl(std::shared_ptr<SessionDb> sdb) : sdb(sdb){};

private:
  Status Open(ServerContext *context, const skinny::OpenReq *req,
              skinny::Handle *res) override {
    auto session = sdb->find_session(req->session_id());
    if (data.find(req->path()) == data.end()) {
      data[req->path()];
    }
    auto fh = session->add_new_handle(req->path());
    res->set_fh(fh);
    return Status::OK;
  }

  Status GetContent(ServerContext *context, const skinny::GetContentReq *req,
                    skinny::Content *res) override {
    auto session = sdb->find_session(req->session_id());
    auto [meta, content] = data.at(session->fh_to_key(req->fh()));
    res->set_content(content);
    return Status::OK;
  }

  Status SetContent(ServerContext *context, const skinny::SetContentReq *req,
                    skinny::Empty *) override {
    auto session = sdb->find_session(req->session_id());
    data.at(session->fh_to_key(req->fh())).second = req->content();
    return Status::OK;
  }

  Status StartSession(ServerContext *context, const skinny::Empty *,
                      skinny::SessionId *res) override {
    // TODO: session ID
    auto session = sdb->create_session();
    res->set_session_id(session->id);
    return Status::OK;
  }

  std::shared_ptr<SessionDb> sdb;
};

class SkinnyCbImpl final : public skinny::SkinnyCb::CallbackService {
public:
  explicit SkinnyCbImpl(std::shared_ptr<SessionDb> sdb) : sdb(sdb){};

private:
  ServerUnaryReactor *KeepAlive(grpc::CallbackServerContext *context,
                                const skinny::SessionId *req,
                                skinny::Empty *) override {
    auto session = sdb->find_session(req->session_id());
    ServerUnaryReactor *reactor = context->DefaultReactor();
    std::cout << "keepalive" << std::endl;
    if (session->kathread)
      session->kathread->join();
    session->kathread = std::make_unique<std::thread>([reactor]() {
      std::this_thread::sleep_for(std::chrono::seconds(5));
      reactor->Finish(Status::OK);
    });
    return reactor;
  }

  std::shared_ptr<SessionDb> sdb;
};

int main() {
  std::string server_address("localhost:50012");
  auto sdb = std::make_shared<SessionDb>();
  SkinnyImpl service(sdb);
  SkinnyCbImpl service_cb(sdb);
  grpc::ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.SetMaxSendMessageSize(INT_MAX);
  builder.SetMaxReceiveMessageSize(INT_MAX);
  builder.SetMaxMessageSize(INT_MAX);

  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  builder.RegisterService(&service_cb);
  // Finally assemble the server.
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}
