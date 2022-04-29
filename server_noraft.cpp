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
#include <mutex>
#include <thread>

#include "SessionDb.cpp"
#include "includes/skinny.grpc.pb.h"
#include "includes/skinny.pb.h"

using grpc::Server;
using grpc::ServerContext;
using grpc::ServerUnaryReactor;
using grpc::Status;

class FileMetaData {
 public:
  FileMetaData()
      : file_exists(true),
        instance_num(0),
        content_gen_num(0),
        lock_gen_num(0) {}

  std::unordered_set<int> lock_owners;
  bool is_locked_ex;
  std::mutex mutex;
  std::condition_variable cv;
  std::list<std::weak_ptr<Session>> subscribers;

  bool file_exists;
  int instance_num;
  int content_gen_num;
  int lock_gen_num;
};

class SkinnyImpl final : public skinny::Skinny::Service {
 public:
  explicit SkinnyImpl(std::shared_ptr<SessionDb> sdb) : sdb(sdb){};

 private:
  Status Open(ServerContext *context, const skinny::OpenReq *req,
              skinny::Handle *res) override {
    auto session = sdb->find_session(req->session_id());
    if (data.contains(req->path())) {
      data[req->path()];
    }
    data[req->path()].first.file_exists = true;  // for previously deleted keys
    if (req->subscribe()) {
      data[req->path()].first.subscribers.push_back(session);
    }
    auto fh = session->add_new_handle(req->path(),
                                      data[req->path()].first.instance_num);
    res->set_fh(fh);
    return Status::OK;
  }

  Status Delete(ServerContext *context, const skinny::DeleteReq *req,
                skinny::Response *res) override {
    auto session = sdb->find_session(req->session_id());
    auto &[meta, content] = data.at(session->fh_to_key(req->fh()));
    content.clear();
    {
      std::lock_guard<std::mutex> guard(meta.mutex);
      meta.file_exists = false;
      meta.instance_num++;
      meta.content_gen_num = 0;
      meta.lock_gen_num = 0;
      for (const int &session_id : meta.lock_owners) {
        // TODO: notify sessions that this lock is deleted.
      }
      meta.lock_owners.clear();
    }
    meta.cv.notify_all();
    return Status::OK;
  }

  Status GetContent(ServerContext *context, const skinny::GetContentReq *req,
                    skinny::Content *res) override {
    auto session = sdb->find_session(req->session_id());
    auto &[meta, content] = data.at(session->fh_to_key(req->fh()));
    if (session->handle_inum(req->fh()) != meta.instance_num)
      return Status(grpc::StatusCode::NOT_FOUND, "Instance num mismatch");
    if (!meta.file_exists)
      return Status(grpc::StatusCode::NOT_FOUND, "File does not exist");
    res->set_content(content);
    return Status::OK;
  }

  Status TryAcquire(ServerContext *context, const skinny::LockAcqReq *req,
                    skinny::Response *res) override {
    auto session = sdb->find_session(req->session_id());
    auto key = session->fh_to_key(req->fh());
    auto &meta = data.at(key).first;
    if (session->handle_inum(req->fh()) != meta.instance_num)
      return Status(grpc::StatusCode::NOT_FOUND, "Instance num mismatch");
    {
      std::lock_guard<std::mutex> guard(meta.mutex);
      if (!meta.file_exists)
        return Status(grpc::StatusCode::NOT_FOUND, "File does not exist");
      if (req->ex()) {  // Lock in exclusive mode
        if (meta.lock_owners.empty()) {
          meta.lock_owners.insert(session->id);
          meta.is_locked_ex = true;
          meta.lock_gen_num++;
          res->set_res(0);
        } else {
          res->set_res(-1);  // fail to acquire
        }
      } else {  // Lock in shared mode
        if (meta.lock_owners.empty() || !meta.is_locked_ex) {
          if (meta.lock_owners.empty()) {
            meta.is_locked_ex = false;  // first reader needs to set it
            meta.lock_gen_num++;
          }
          meta.lock_owners.insert(session->id);

          res->set_res(0);
        } else {
          res->set_res(-1);  // fail to acquire
        }
      }
    }
    return Status::OK;
  }

  Status Acquire(ServerContext *context, const skinny::LockAcqReq *req,
                 skinny::Response *res) override {
    auto session = sdb->find_session(req->session_id());
    auto key = session->fh_to_key(req->fh());
    auto &meta = data.at(key).first;
    if (session->handle_inum(req->fh()) != meta.instance_num)
      return Status(grpc::StatusCode::NOT_FOUND, "Instance num mismatch");
    {
      std::unique_lock<std::mutex> ulock(meta.mutex);
      if (req->ex()) {  // Lock in exclusive mode
        meta.cv.wait(ulock, [&] { return meta.lock_owners.empty(); });
        if (!meta.file_exists)
          return Status(grpc::StatusCode::NOT_FOUND, "File does not exist");
        meta.lock_owners.insert(session->id);
        meta.is_locked_ex = true;
        meta.lock_gen_num++;
      } else {  // Lock in shared mode
        meta.cv.wait(ulock, [&] {
          return meta.lock_owners.empty() || !meta.is_locked_ex;
        });
        if (!meta.file_exists)
          return Status(grpc::StatusCode::NOT_FOUND, "File does not exist");
        if (meta.lock_owners.empty()) {
          meta.is_locked_ex = false;
          meta.lock_gen_num++;
        }
        meta.lock_owners.insert(session->id);
      }
      res->set_res(0);
    }
    return Status::OK;
  }

  Status Release(ServerContext *context, const skinny::LockRelReq *req,
                 skinny::Response *res) override {
    auto session = sdb->find_session(req->session_id());
    auto key = session->fh_to_key(req->fh());
    auto &meta = data.at(key).first;
    if (session->handle_inum(req->fh()) != meta.instance_num)
      return Status(grpc::StatusCode::NOT_FOUND, "Instance num mismatch");
    return unlock(session->id, meta)
               ? Status::OK
               : Status(grpc::StatusCode::NOT_FOUND, "File does not exist");
  }

  Status SetContent(ServerContext *context, const skinny::SetContentReq *req,
                    skinny::Empty *) override {
    auto session = sdb->find_session(req->session_id());
    auto &[meta, content] = data.at(session->fh_to_key(req->fh()));
    if (session->handle_inum(req->fh()) != meta.instance_num)
      return Status(grpc::StatusCode::NOT_FOUND, "Instance num mismatch");
    if (!meta.file_exists)
      return Status(grpc::StatusCode::NOT_FOUND, "File does not exist");
    content = req->content();
    for (auto it = meta.subscribers.begin(); it != meta.subscribers.end();) {
      auto ptr = it->lock();
      if (ptr) {
        ptr->enqueue_event(req->fh());
        it++;
      } else {
        it = meta.subscribers.erase(it);
      }
    }
    return Status::OK;
  }

  Status StartSession(ServerContext *context, const skinny::Empty *,
                      skinny::SessionId *res) override {
    // TODO: session ID
    auto session = sdb->create_session();
    res->set_session_id(session->id);
    return Status::OK;
  }

  bool unlock(int session_id, FileMetaData &meta) {
    bool need_notify;
    {
      std::lock_guard<std::mutex> guard(meta.mutex);
      if (!meta.file_exists) return false;
      meta.lock_owners.erase(session_id);
      need_notify = meta.lock_owners.empty();
    }
    if (need_notify)
      meta.cv.notify_all();  // If a EX lock is released, *all* waiting SH reqs
    // should acquire the lock
    return true;
  }

  void close_handle(int session_id, int fh) {
    auto session = sdb->find_session(session_id);
    if (session->handle_inum(fh) == -1) return;  // handle already closed
    auto &meta = data.at(session->fh_to_key(fh)).first;
    if (meta.lock_owners.contains(session_id)) unlock(session_id, meta);
    session->close_handle(fh);
  }

  void close_session(int64_t session_id) {
    auto session = sdb->find_session(session_id);
    for (int i = 0; i < session->handle_count(); ++i) session->close_handle(i);
    sdb->delete_session(session_id);
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
                                skinny::Event *res) override {
    auto session = sdb->find_session(req->session_id());
    ServerUnaryReactor *reactor = context->DefaultReactor();
    std::cout << "keepalive" << std::endl;
    if (!session->event_queue.empty()) {
      res->set_fh(session->event_queue.front());
      session->event_queue.pop();
      reactor->Finish(Status::OK);
      return reactor;
    }
    if (session->kathread && session->kathread->joinable())
      session->kathread->join();
    session->kathread =
        std::make_unique<std::thread>([reactor, session, res]() {
          using namespace std::chrono_literals;
          std::unique_lock<std::mutex> lk(session->emu);
          if (session->ecv.wait_for(lk, 1s, [session]() {
                return !session->event_queue.empty();
              })) {
            res->set_fh(session->event_queue.front());
            session->event_queue.pop();
          }
          reactor->Finish(Status::OK);
        });
    return reactor;
  }

  std::shared_ptr<SessionDb> sdb;
};

int main() {
  std::string server_address("localhost:23456");
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
