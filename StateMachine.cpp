
#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <variant>

#include "Session.cpp"
#include "buffer_serializer.hxx"
#include "includes/action.cpp"
#include "libnuraft/buffer.hxx"
#include "libnuraft/nuraft.hxx"
#include "libnuraft/state_machine.hxx"
#include "srv_config.h"

namespace StateMachine {
using namespace nuraft;
class StateMachine : public state_machine {
 public:
  StateMachine(std::shared_ptr<DataStore> ds, std::shared_ptr<session::Db> sdb)
      : last_committed_idx_(0), sdb_(sdb), ds_(ds) {}

  ~StateMachine() {}

  ptr<buffer> commit(const ulong log_idx, buffer& data) override {
    auto action = action::create_action_from_buf(data);
    auto result =
        std::visit([this](auto&& arg) { return apply_(arg); }, action);
    // Update last committed index number.
    last_committed_idx_ = log_idx;
    return result;
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
  ptr<buffer> apply_(action::OpenAction& a) {
    auto session = sdb_->find_session(a.session_id);
    if (ds_->find(a.path) == ds_->end()) {
      ds_->operator[](a.path);
    }
    ds_->at(a.path).first.file_exists = true;  // for previously deleted keys
    // if (a.subscribe) {
    //   data_[path()].first.subscribers.push_back(session);
    // }
    auto fh =
        session->add_new_handle(a.path, ds_->at(a.path).first.instance_num);
    action::OpenReturn ret(fh);
    return ret.serialize();
  }

  ptr<buffer> apply_(action::StartSessionAction& a) {
    auto session = sdb_->create_session();
    action::StartSessionReturn ret(session->id);
    return ret.serialize();
  }

  ptr<buffer> apply_(action::SetContentAction& a) {
    auto session = sdb_->find_session(a.session_id);
    auto& [meta, content] = ds_->at(session->fh_to_key(a.fh));
    // if (session->handle_inum(req->fh()) != meta.instance_num)
    //   return Status(grpc::StatusCode::NOT_FOUND, "Instance num mismatch");
    // if (!meta.file_exists)
    //   return Status(grpc::StatusCode::NOT_FOUND, "File does not exist");
    content = a.content;
    // for (auto it = meta.subscribers.begin(); it != meta.subscribers.end();) {
    //   auto ptr = it->lock();
    //   if (ptr) {
    //     ptr->enqueue_event(req->fh());
    //     it++;
    //   } else {
    //     it = meta.subscribers.erase(it);
    //   }
    // }
    return nullptr;
  }

  ptr<buffer> apply_(action::LockAcqAction& a) {
    auto session = sdb_->find_session(a.session_id);
    auto key = session->fh_to_key(a.fh);
    auto& meta = ds_->at(key).first;

    action::Response res;

    // if (session->handle_inum(a.fh) != meta.instance_num)
    //   return Status(grpc::StatusCode::NOT_FOUND, "Instance num mismatch");
    {
      std::lock_guard<std::mutex> guard(meta.mutex);
      if (!meta.file_exists) {
        // return Status(grpc::StatusCode::NOT_FOUND, "File does not exist");
        res = {-1, "File does not exist"};
      }

      if (a.ex) {  // Lock in exclusive mode
        if (meta.lock_owners.empty()) {
          meta.lock_owners.insert(session->id);
          meta.is_locked_ex = true;
          meta.lock_gen_num++;
          res = {0, ""};
        } else {
          res = {-1, "fail to acquire"};  // fail to acquire
        }
      } else {  // Lock in shared mode
        if (meta.lock_owners.empty() || !meta.is_locked_ex) {
          if (meta.lock_owners.empty()) {
            meta.is_locked_ex = false;  // first reader needs to set it
            meta.lock_gen_num++;
          }
          meta.lock_owners.insert(session->id);

          res = {0, ""};
        } else {
          res = {-1, "fail to acquire"};  // fail to acquire
        }
      }
    }
    return res.serialize();
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
