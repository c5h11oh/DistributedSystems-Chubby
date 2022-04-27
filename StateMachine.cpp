
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
  std::list<std::weak_ptr<session::Entry>> subscribers;

  bool file_exists;
  int instance_num;
  int content_gen_num;
  int lock_gen_num;
};

namespace StateMachine {
using namespace nuraft;
class StateMachine : public state_machine {
 public:
  StateMachine() : last_committed_idx_(0) {}

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

  const std::string& get_content(int session_id, int fh) {
    auto session = sdb_.find_session(session_id);
    auto& [meta, content] = data_.at(session->fh_to_key(fh));
    return content;
    // if (session->handle_inum(req->fh()) != meta.instance_num)
    //   return Status(grpc::StatusCode::NOT_FOUND, "Instance num mismatch");
    // if (!meta.file_exists)
    //   return Status(grpc::StatusCode::NOT_FOUND, "File does not exist");
    // res->set_content(content);
    // return Status::OK;
  }

 private:
  ptr<buffer> apply_(action::OpenAction& a) {
    auto session = sdb_.find_session(a.session_id);
    if (data_.find(a.path) == data_.end()) {
      data_[a.path];
    }
    data_[a.path].first.file_exists = true;  // for previously deleted keys
    // if (a.subscribe) {
    //   data_[path()].first.subscribers.push_back(session);
    // }
    auto fh = session->add_new_handle(a.path, data_[a.path].first.instance_num);
    action::OpenReturn ret(fh);
    return ret.serialize();
  }

  ptr<buffer> apply_(action::StartSessionAction& a) {
    auto session = sdb_.create_session();
    action::StartSessionReturn ret(session->id);
    return ret.serialize();
  }

  ptr<buffer> apply_(action::SetContentAction& a) {
    auto session = sdb_.find_session(a.session_id);
    auto& [meta, content] = data_.at(session->fh_to_key(a.fh));
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

  // Last committed Raft log number.
  std::atomic<uint64_t> last_committed_idx_;

  // Last snapshot.
  ptr<snapshot> last_snapshot_;

  // Mutex for last snapshot.
  std::mutex last_snapshot_lock_;

  session::Db sdb_;
  std::unordered_map<std::string, std::pair<FileMetaData, std::string>> data_;
};
}  // namespace StateMachine
