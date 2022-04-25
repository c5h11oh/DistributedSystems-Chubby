#include <fcntl.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/server_callback.h>
#include <grpcpp/support/sync_stream.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <condition_variable>
#include <exception>
#include <memory>
#include <thread>

#include "SessionDb.cpp"
#include "includes/skinny.grpc.pb.h"
#include "includes/skinny.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerUnaryReactor;
using grpc::Status;

class FileMetaData {
 public:
  FileMetaData() : instance_num(0), content_gen_num(0), lock_gen_num(0) {}

  std::unordered_set<int> lock_holding_session;
  bool is_locked_ex;
  std::mutex mutex;
  std::condition_variable cv;

  uint32_t instance_num;
  uint32_t content_gen_num;
  uint32_t lock_gen_num;
};

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
    auto &[meta, content] = data.at(session->fh_to_key(req->fh()));
    res->set_content(content);
    return Status::OK;
  }

  Status TryAcquire(ServerContext *context, const skinny::LockAcqReq *req,
                    skinny::LockRes *res) {
    auto session = sdb->find_session(req->session_id());
    auto key = session->fh_to_key(req->fh());
    auto &meta = data.at(key).first;
    {
      std::lock_guard<std::mutex> guard(meta.mutex);
      if (req->ex()) {  // Lock in exclusive mode
        if (meta.lock_holding_session.empty()) {
          meta.lock_holding_session.insert(session->id);
          meta.is_locked_ex = true;
          meta.lock_gen_num++;
          res->set_res(0);
        } else {
          res->set_res(-1);  // fail to acquire
        }
      } else {  // Lock in shared mode
        if (meta.lock_holding_session.empty() || !meta.is_locked_ex) {
          meta.lock_holding_session.insert(session->id);
          meta.is_locked_ex = false;  // first reader needs to set it
          res->set_res(0);
        } else {
          res->set_res(-1);  // fail to acquire
        }
      }
    }
    return Status::OK;
  }

  Status Acquire(ServerContext *context, const skinny::LockAcqReq *req,
                 skinny::LockRes *res) {
    auto session = sdb->find_session(req->session_id());
    auto key = session->fh_to_key(req->fh());
    auto &meta = data.at(key).first;
    {
      std::unique_lock<std::mutex> ulock(meta.mutex);
      if (req->ex()) {  // Lock in exclusive mode
        meta.cv.wait(ulock, [&] { return meta.lock_holding_session.empty(); });
        assert(meta.lock_holding_session.empty());  // TODO: delete this line...
        meta.lock_holding_session.insert(session->id);
        meta.is_locked_ex = true;
        meta.lock_gen_num++;
      } else {  // Lock in shared mode
        meta.cv.wait(ulock, [&] {
          return meta.lock_holding_session.empty() || !meta.is_locked_ex;
        });
        meta.lock_holding_session.insert(session->id);
        meta.is_locked_ex = false;
      }
      res->set_res(0);
    }
    return Status::OK;
  }

  Status Release(ServerContext *context, const skinny::LockRelReq *req,
                 skinny::LockRes *res) {
    auto session = sdb->find_session(req->session_id());
    auto key = session->fh_to_key(req->fh());
    auto &meta = data.at(key).first;
    bool need_notify;
    {
      std::lock_guard<std::mutex> guard(meta.mutex);
      meta.lock_holding_session.erase(session->id);
      need_notify = meta.lock_holding_session.empty();
      res->set_res(0);
    }
    if (need_notify)
      meta.cv.notify_all();  // If a EX lock is released, *all* waiting SH reqs
                             // should acquire the lock
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
  std::unordered_map<std::string, std::pair<FileMetaData, std::string>> data;
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
    if (session->kathread) session->kathread->join();
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