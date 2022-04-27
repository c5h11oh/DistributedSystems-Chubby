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
#include <unordered_set>

#include "StateMachine.cpp"
#include "includes/skinny.grpc.pb.h"
#include "includes/skinny.pb.h"

using grpc::ServerContext;
using grpc::Status;

class SkinnyImpl final : public skinny::Skinny::Service {
 public:
  explicit SkinnyImpl(std::shared_ptr<nuraft::raft_server> raft,
                      std::shared_ptr<StateMachine::StateMachine> sm)
      : raft_(raft), sm_(sm){};

 private:
  Status Open(ServerContext *context, const skinny::OpenReq *req,
              skinny::Handle *res) override {
    action::OpenAction action{req->session_id(), req->path()};
    auto raft_ret = raft_->append_entries({action.serialize()});
    if (!raft_ret->get_accepted()) {
      std::cout << "Open failed (raft)" << std::endl;
      return Status::CANCELLED;
    }
    action::OpenReturn r(*raft_ret->get());
    res->set_fh(r.fh);
    return Status::OK;
  }

  Status GetContent(ServerContext *context, const skinny::GetContentReq *req,
                    skinny::Content *res) override {
    if (!raft_->is_leader()) return Status::CANCELLED;
    res->set_content(sm_->get_content(req->session_id(), req->fh()));
    return Status::OK;
  }

  Status SetContent(ServerContext *context, const skinny::SetContentReq *req,
                    skinny::Empty *) override {
    action::SetContentAction action{req->session_id(), req->fh(),
                                    req->content()};
    auto raft_ret = raft_->append_entries({action.serialize()});
    if (!raft_ret->get_accepted()) {
      std::cout << "SetContent failed (raft)" << std::endl;
      return Status::CANCELLED;
    }
    return Status::OK;
  }

  Status StartSession(ServerContext *context, const skinny::Empty *,
                      skinny::SessionId *res) override {
    action::StartSessionAction action;
    auto raft_ret = raft_->append_entries({action.serialize()});
    if (!raft_ret->get_accepted()) {
      std::cout << "StartSession failed (raft)" << std::endl;
      return Status::CANCELLED;
    }
    action::StartSessionReturn r(*raft_ret->get());
    res->set_session_id(r.seesion_id);
    return Status::OK;
  }

  std::shared_ptr<nuraft::raft_server> raft_;
  std::shared_ptr<StateMachine::StateMachine> sm_;
};
