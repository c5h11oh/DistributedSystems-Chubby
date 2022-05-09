#include <fcntl.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/server_callback.h>
#include <grpcpp/support/status.h>
#include <grpcpp/support/status_code_enum.h>
#include <grpcpp/support/sync_stream.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

#include "StateMachine.cpp"
#include "async.hxx"
#include "buffer_serializer.hxx"
#include "includes/skinny.grpc.pb.h"
#include "includes/skinny.pb.h"
#include "srv_config.h"

using grpc::ServerContext;
using grpc::ServerUnaryReactor;
using grpc::Status;

class SkinnyImpl final : public skinny::Skinny::Service {
 public:
  explicit SkinnyImpl(std::shared_ptr<nuraft::raft_server> raft,
                      std::shared_ptr<DataStore> ds,
                      std::shared_ptr<session::Db> sdb)
      : raft_(raft), ds_(ds), sdb_(sdb){};

 private:
  Status parse_raft_result(
      nuraft::ptr<nuraft::cmd_result<nuraft::ptr<nuraft::buffer>>> r) {
    if (r->get_accepted() && r->get_result_code() == nuraft::OK) {
      action::Response res(*r->get());
      if (res.res >= 0) {
        return Status::OK;
      } else {
        return Status(grpc::StatusCode::ABORTED, res.msg);
      }
    } else if (r->get_result_code() == nuraft::NOT_LEADER) {
      return Status(
          static_cast<grpc::StatusCode>(skinny::ErrorCode::NOT_LEADER),
          std::to_string(raft_->get_leader()));
    } else {
      return Status(grpc::StatusCode::ABORTED, "");
    }
  };

  Status Open(ServerContext *context, const skinny::OpenReq *req,
              skinny::Handle *res) override {
    action::OpenAction action{req};
    auto raft_ret = raft_->append_entries({action.serialize()});
    if (auto status = parse_raft_result(raft_ret); !status.ok()) {
      return status;
    }
    action::OpenReturn r(*raft_ret->get());
    std::filesystem::path path = req->path();
    std::string parent_path = path.parent_path();
    auto &[parent_meta, parent_content] = ds_->at(parent_path);
    notify_events(parent_meta);
    res->set_fh(r.fh);
    return Status::OK;
  }

  Status Close(ServerContext *context, const skinny::CloseReq *req,
               skinny::Empty *) override {
    action::CloseAction action{req->session_id(), req->fh()};
    auto raft_ret = raft_->append_entries({action.serialize()});
    if (auto status = parse_raft_result(raft_ret); !status.ok()) {
      return status;
    }
    return Status::OK;
  }

  Status GetContent(ServerContext *context, const skinny::GetContentReq *req,
                    skinny::Content *res) override {
    if (!raft_->is_leader()) {
      return Status(
          static_cast<grpc::StatusCode>(skinny::ErrorCode::NOT_LEADER),
          std::to_string(raft_->get_leader()));
    }
    // TODO
    auto session = sdb_->find_session(req->session_id());
    auto &[meta, content] = ds_->at(session->fh_to_key(req->fh()));
    res->set_content(content);
    return Status::OK;
  }

  Status SetContent(ServerContext *context, const skinny::SetContentReq *req,
                    skinny::Empty *) override {
    action::SetContentAction action{req};
    auto raft_ret = raft_->append_entries({action.serialize()});
    if (auto status = parse_raft_result(raft_ret); !status.ok()) {
      return status;
    }
    auto session = sdb_->find_session(req->session_id());
    assert(session != nullptr);
    auto &[meta, content] = ds_->at(session->fh_to_key(req->fh()));
    notify_events(meta);
    return Status::OK;
  }

  Status StartSession(ServerContext *context, const skinny::Empty *,
                      skinny::SessionId *res) override {
    action::StartSessionAction action;
    auto raft_ret = raft_->append_entries({action.serialize()});
    if (auto status = parse_raft_result(raft_ret); !status.ok()) {
      return status;
    }
    action::StartSessionReturn r(*raft_ret->get());
    if (raft_->is_leader()) {
      sdb_->find_session(r.session_id)->start_kathread();
    }
    res->set_session_id(r.session_id);
    return Status::OK;
  }

  Status EndSession(ServerContext *context, const skinny::SessionId *req,
                    skinny::Empty *) override {
    action::EndSessionAction action(req->session_id());
    auto raft_ret = raft_->append_entries({action.serialize()});
    if (auto status = parse_raft_result(raft_ret); !status.ok()) {
      return status;
    }
    return Status::OK;
  }

  // res->res return:
  // -3: session not found
  // -2: file does not exist
  // -1: instance number mismatch
  //  0: lock acquired
  //  1: lock NOT acquired
  Status TryAcquire(ServerContext *context, const skinny::LockAcqReq *req,
                    skinny::Response *res) override {
    auto session = sdb_->find_session(req->session_id());
    if (!session) {
      res->set_res(-3);
      res->set_msg("Session not found");
      return Status::OK;
    }
    auto key = session->fh_to_key(req->fh());
    auto &meta = ds_->at(key).first;
    {
      std::lock_guard<std::mutex> guard(meta.mutex);
      if (session->handle_inum(req->fh()) != meta.instance_num) {
        res->set_res(-1);
        res->set_msg("Instance num mismatch");
        return Status::OK;
      }
      if (!meta.file_exists) {
        res->set_res(-2);
        res->set_msg("File does not exist");
        return Status::OK;
      }
    }

    action::AcqAction action(req->session_id(), req->fh(), req->ex());
    auto raft_ret = raft_->append_entries({action.serialize()});
    if (auto status = parse_raft_result(raft_ret); !status.ok()) {
      return status;
    }
    action::Response sm_result(*raft_ret->get());
    if (sm_result.res == -1) {
      return Status(
          static_cast<grpc::StatusCode>(skinny::ErrorCode::LOCK_RELATED),
          sm_result.msg);
    }

    res->set_res(sm_result.res);
    res->set_msg(sm_result.msg);
    return Status::OK;
  }

  // res->res return:
  // -3: session not found
  // -2: file does not exist
  // -1: instance number mismatch
  //  0: lock acquired
  Status Acquire(ServerContext *context, const skinny::LockAcqReq *req,
                 skinny::Response *res) override {
    auto session = sdb_->find_session(req->session_id());
    if (!session) {
      res->set_res(-3);
      res->set_msg("Session not found");
      return Status::OK;
    }
    auto key = session->fh_to_key(req->fh());
    auto &meta = ds_->at(key).first;
    {
      std::unique_lock<std::mutex> ulock(meta.mutex);

      if (req->ex())
        meta.cv.wait(ulock, [&] { return meta.lock_owners.empty(); });
      else
        meta.cv.wait(ulock, [&] {
          return meta.lock_owners.empty() || !meta.is_locked_ex;
        });

      if (session->handle_inum(req->fh()) != meta.instance_num) {
        res->set_res(-1);
        res->set_msg("Instance num mismatch");
        return Status::OK;
      }
      if (!meta.file_exists) {
        res->set_res(-2);
        res->set_msg("File does not exist");
        return Status::OK;
      }
    }
    action::AcqAction action(req->session_id(), req->fh(), req->ex());
    auto raft_ret = raft_->append_entries({action.serialize()});
    if (auto status = parse_raft_result(raft_ret); !status.ok()) {
      return status;
    }
    action::Response sm_result(*raft_ret->get());
    // since we used cv to check condition, we should be able to grab the lock
    assert(sm_result.res == 0);
    res->set_res(sm_result.res);
    res->set_msg(sm_result.msg);
    return Status::OK;
  }

  Status Release(ServerContext *context, const skinny::LockRelReq *req,
                 skinny::Response *res) override {
    action::RelAction action(req->session_id(), req->fh());
    auto raft_ret = raft_->append_entries({action.serialize()});
    if (auto status = parse_raft_result(raft_ret); !status.ok()) {
      return status;
    }
    action::Response sm_result(*raft_ret->get());
    if (sm_result.res < 0)
      return Status(
          static_cast<grpc::StatusCode>(skinny::ErrorCode::LOCK_RELATED),
          sm_result.msg);
    return Status::OK;
  }

  Status Delete(ServerContext *context, const skinny::DeleteReq *req,
                skinny::Response *res) override {
    action::DeleteAction action{req};
    auto raft_ret = raft_->append_entries({action.serialize()});
    if (auto status = parse_raft_result(raft_ret); !status.ok()) {
      return status;
    }
    action::Response r(*raft_ret->get());
    res->set_res(r.res);

    auto session = sdb_->find_session(req->session_id());
    auto key = session->fh_to_key(req->fh());
    std::filesystem::path path{key};
    std::string parent_path = path.parent_path();
    auto &[parent_meta, parent_content] = ds_->at(parent_path);
    notify_events(parent_meta);
    return Status::OK;
  }

  void notify_events(FileMetaData &meta) {
    std::vector<std::thread> vt;
    for (auto it = meta.subscribers.begin(); it != meta.subscribers.end();) {
      auto ptr = it->first.lock();
      if (ptr && ptr->handle_inum(it->second) != -1) {
        auto eid = ptr->enqueue_event(it->second);
        if (eid) {
          vt.emplace_back([ptr, eid = eid.value()]() {
            ptr->block_until_event_acked(eid);
          });
        }
        it++;
      } else {
        it = meta.subscribers.erase(it);
      }
    }
    for (auto &t : vt) {
      t.join();
    }
  }

  std::shared_ptr<nuraft::raft_server> raft_;
  const std::shared_ptr<DataStore> ds_;
  std::shared_ptr<session::Db> sdb_;
};

class SkinnyCbImpl final : public skinny::SkinnyCb::CallbackService {
 public:
  explicit SkinnyCbImpl(std::shared_ptr<nuraft::raft_server> raft,
                        std::shared_ptr<session::Db> sdb)
      : sdb_(sdb), raft_(raft){};

 private:
  ServerUnaryReactor *KeepAlive(grpc::CallbackServerContext *context,
                                const skinny::KeepAliveReq *req,
                                skinny::Event *res) override {
    ServerUnaryReactor *reactor = context->DefaultReactor();
    if (!raft_->is_leader()) {
      reactor->Finish(
          Status(static_cast<grpc::StatusCode>(skinny::ErrorCode::NOT_LEADER),
                 std::to_string(raft_->get_leader())));
      return reactor;
    }
    auto session = sdb_->find_session(req->session_id());
    if (!session) {
      reactor->Finish(Status::CANCELLED);
      return reactor;
    }
    // std::cout << "keepalive" << std::endl;
    session->set_reactor(reactor, res,
                         req->has_acked_event() ? req->acked_event() : -1);
    return reactor;
  }

  std::shared_ptr<nuraft::raft_server> raft_;
  std::shared_ptr<session::Db> sdb_;
};
