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
#include "includes/skinny.grpc.pb.h"
#include "includes/skinny.pb.h"
#include "srv_config.h"

using grpc::ServerContext;
using grpc::ServerUnaryReactor;
using grpc::Status;

class SkinnyImpl final : public skinny::Skinny::Service {
 public:
  explicit SkinnyImpl(std::shared_ptr<nuraft::raft_server> raft,
                      std::shared_ptr<StateMachine::StateMachine> sm)
      : raft_(raft), sm_(sm){};

 private:
  Status parse_raft_result(
      nuraft::ptr<nuraft::cmd_result<nuraft::ptr<nuraft::buffer>>> r) {
    if (r->get_accepted()) {
      return Status::OK;
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
    res->set_fh(r.fh);
    return Status::OK;
  }

  Status GetContent(ServerContext *context, const skinny::GetContentReq *req,
                    skinny::Content *res) override {
    if (!raft_->is_leader()) {
      return Status(
          static_cast<grpc::StatusCode>(skinny::ErrorCode::NOT_LEADER),
          std::to_string(raft_->get_leader()));
    }
    res->set_content(sm_->get_content(req->session_id(), req->fh()));
    return Status::OK;
  }

  Status SetContent(ServerContext *context, const skinny::SetContentReq *req,
                    skinny::Empty *) override {
    action::SetContentAction action{req};
    auto raft_ret = raft_->append_entries({action.serialize()});
    if (auto status = parse_raft_result(raft_ret); !status.ok()) {
      return status;
    }
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
    res->set_session_id(r.seesion_id);
    return Status::OK;
  }

  std::shared_ptr<nuraft::raft_server> raft_;
  std::shared_ptr<StateMachine::StateMachine> sm_;
};

class SkinnyCbImpl final : public skinny::SkinnyCb::CallbackService {
 public:
  explicit SkinnyCbImpl(std::shared_ptr<nuraft::raft_server> raft,
                        std::shared_ptr<session::Db> sdb)
      : sdb_(sdb), raft_(raft){};

 private:
  ServerUnaryReactor *KeepAlive(grpc::CallbackServerContext *context,
                                const skinny::SessionId *req,
                                skinny::Event *res) override {
    ServerUnaryReactor *reactor = context->DefaultReactor();
    if (!raft_->is_leader()) {
      reactor->Finish(
          Status(static_cast<grpc::StatusCode>(skinny::ErrorCode::NOT_LEADER),
                 std::to_string(raft_->get_leader())));
      return reactor;
    }
    auto session = sdb_->find_session(req->session_id());
    std::cout << "keepalive" << std::endl;
    if (!session->event_queue.empty()) {
      res->set_fh(session->pop_event());
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
            res->set_fh(session->pop_event());
            session->event_queue.pop();
          }
          reactor->Finish(Status::OK);
        });
    return reactor;
  }

  std::shared_ptr<nuraft::raft_server> raft_;
  std::shared_ptr<session::Db> sdb_;
};
