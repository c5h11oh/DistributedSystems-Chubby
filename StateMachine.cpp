
#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <optional>
#include <variant>

#include "Session.cpp"
#include "buffer_serializer.hxx"
#include "includes/action.pb.h"
#include "libnuraft/buffer.hxx"
#include "libnuraft/nuraft.hxx"
#include "libnuraft/state_machine.hxx"
#include "srv_config.h"
#include "utils.cpp"

namespace StateMachine {
using namespace nuraft;
class StateMachine : public state_machine {
 public:
  StateMachine(std::shared_ptr<DataStore> ds, std::shared_ptr<session::Db> sdb)
      : last_committed_idx_(0), sdb_(sdb), ds_(ds) {}

  ~StateMachine() {}

  ptr<buffer> commit(const ulong log_idx, buffer& data) override {
    action::Action action;
    size_t len;
    auto buf = data.get_bytes(len);
    action.ParseFromArray(buf, len);
    std::optional<action::Response> result;
    switch (action.action_case()) {
      case action::Action::kOpenAction:
        result = apply_(action.open_action());
        break;
      case action::Action::kLockAcqAction:
        result = apply_(action.lock_acq_action());
        break;
      case action::Action::kStartSessionAction:
        result = apply_(action.start_session_action());
        break;
      case action::Action::kSetContentAction:
        result = apply_(action.set_content_action());
        break;
      case action::Action::ACTION_NOT_SET:
        std::cerr << "Action not set" << std::endl;
        assert(0);
        std::terminate();
    }
    // Update last committed index number.
    last_committed_idx_ = log_idx;
    return result ? action::serialize(result.value()) : nullptr;
  }

  // TODO
  bool apply_snapshot(snapshot& s) override {
    std::cout << "apply snapshot " << s.get_last_log_idx() << " term "
              << s.get_last_log_term() << std::endl;
    // Clone snapshot from `s`.
    {
      std::lock_guard<std::mutex> l(last_snapshot_lock_);
      ptr<buffer> snp_buf = s.serialize();
      last_snapshot_ = snapshot::deserialize(*snp_buf);
    }
    return true;
  }

  // TODO
  ptr<snapshot> last_snapshot() override {
    // Just return the latest snapshot.
    std::lock_guard<std::mutex> l(last_snapshot_lock_);
    return last_snapshot_;
  }

  ulong last_commit_index() override { return last_committed_idx_; }

  // TODO
  void create_snapshot(snapshot& s,
                       async_result<bool>::handler_type& when_done) override {
    std::cout << "create snapshot " << s.get_last_log_idx() << " term "
              << s.get_last_log_term() << std::endl;
    // Clone snapshot from `s`.
    {
      std::lock_guard<std::mutex> l(last_snapshot_lock_);
      ptr<buffer> snp_buf = s.serialize();
      last_snapshot_ = snapshot::deserialize(*snp_buf);
    }
    ptr<std::exception> except(nullptr);
    bool ret = true;
    when_done(ret, except);
  }

 private:
  action::Response apply_(const action::OpenAction& a) {
    auto session = sdb_->find_session(a.session_id());
    if (ds_->find(a.path()) == ds_->end()) {
      ds_->operator[](a.path());
    }
    ds_->at(a.path()).first.file_exists = true;  // for previously deleted keys
    // if (a.subscribe) {
    //   data_[path()].first.subscribers.push_back(session);
    // }
    auto fh =
        session->add_new_handle(a.path(), ds_->at(a.path()).first.instance_num);
    action::OpenReturn ret;
    ret.set_fh(fh);
    action::Response res;
    res.set_allocated_open_return(&ret);
    return res;
  }

  action::Response apply_(const action::StartSessionAction& a) {
    auto session = sdb_->create_session();
    auto ret = std::make_unique<action::StartSessionReturn>();
    ret->set_seesion_id(session->id);
    action::Response res;
    res.set_allocated_start_session_return(ret.release());
    return res;
  }

  std::nullopt_t apply_(const action::SetContentAction& a) {
    auto session = sdb_->find_session(a.session_id());
    auto& [meta, content] = ds_->at(session->fh_to_key(a.fh()));
    // if (session->handle_inum(req->fh()) != meta.instance_num)
    //   return Status(grpc::StatusCode::NOT_FOUND, "Instance num mismatch");
    // if (!meta.file_exists)
    //   return Status(grpc::StatusCode::NOT_FOUND, "File does not exist");
    content = a.content();
    // for (auto it = meta.subscribers.begin(); it != meta.subscribers.end();) {
    //   auto ptr = it->lock();
    //   if (ptr) {
    //     ptr->enqueue_event(req->fh());
    //     it++;
    //   } else {
    //     it = meta.subscribers.erase(it);
    //   }
    // }
    return std::nullopt;
  }

  action::Response apply_(const action::LockAcqAction& a) {
    auto session = sdb_->find_session(a.session_id());
    auto key = session->fh_to_key(a.fh());
    auto& meta = ds_->at(key).first;

    action::SimpleRes res;

    // if (session->handle_inum(a.fh) != meta.instance_num)
    //   return Status(grpc::StatusCode::NOT_FOUND, "Instance num mismatch");
    {
      std::lock_guard<std::mutex> guard(meta.mutex);
      if (!meta.file_exists) {
        // return Status(grpc::StatusCode::NOT_FOUND, "File does not exist");
        res.set_res(-1);
        res.set_msg("File does not exist");
      }

      if (a.ex()) {  // Lock in exclusive mode
        if (meta.lock_owners.empty()) {
          meta.lock_owners.insert(session->id);
          meta.is_locked_ex = true;
          meta.lock_gen_num++;
          res.set_res(0);
        } else {
          res.set_res(-1);
          res.set_msg("fail to acquire");
        }
      } else {  // Lock in shared mode
        if (meta.lock_owners.empty() || !meta.is_locked_ex) {
          if (meta.lock_owners.empty()) {
            meta.is_locked_ex = false;  // first reader needs to set it
            meta.lock_gen_num++;
          }
          meta.lock_owners.insert(session->id);
          res.set_res(0);
        } else {
          res.set_res(-1);
          res.set_msg("fail to acquire");
        }
      }
    }

    action::Response ret;
    ret.set_allocated_simple_res(&res);
    return ret;
  }

  // Last committed Raft log number.
  std::atomic<uint64_t> last_committed_idx_;

  // Last snapshot.
  ptr<snapshot> last_snapshot_;

  // Mutex for last snapshot.
  std::mutex last_snapshot_lock_;

  std::shared_ptr<session::Db> sdb_;
  std::shared_ptr<DataStore> ds_;
};
}  // namespace StateMachine
