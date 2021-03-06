
#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <variant>

#include "Session.cpp"
#include "buffer_serializer.hxx"
#include "includes/action.cpp"
#include "libnuraft/buffer.hxx"
#include "libnuraft/nuraft.hxx"
#include "libnuraft/state_machine.hxx"
#include "utils.h"

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

  bool apply_snapshot(snapshot& s) override { return false; }

  ptr<snapshot> last_snapshot() override { return 0; }

  ulong last_commit_index() override { return 0; }

  void create_snapshot(snapshot& s,
                       async_result<bool>::handler_type& when_done) override {
    return;
  }

 private:
  ptr<buffer> apply_(action::OpenAction& a) {
    auto session = sdb_->find_session(a.session_id);
    if (!session) {
      return action::OpenReturn(-1, SESSION_NOT_FOUND_STR, -1).serialize();
    }
    if (ds_->find(a.path) == ds_->end()) {
      ds_->operator[](a.path);
    }
    auto& [meta, content] = ds_->at(a.path);
    // Handle creating a directory
    if (meta.file_exists == false) {
      meta.is_directory = a.is_directory;
    }
    meta.is_ephemeral = a.is_ephemeral;
    // Update parent dir
    std::filesystem::path path = a.path;
    std::string parent_path = path.parent_path();
    if (!ds_->contains(parent_path)) {
      std::cout << "Parent path does not exist" << std::endl;
      assert(0);
    }
    auto& [parent_meta, parent_content] = ds_->at(parent_path);
    if (!parent_meta.is_directory) {
      std::cout << "Parent is not a directory" << std::endl;
      assert(0);
    }
    if (path != "/" && meta.file_exists == false) {
      std::lock_guard lg(parent_meta.mutex);
      parent_content += std::string(1, '\0') + std::string(path.filename());
    }
    meta.file_exists = true;  // for previously deleted keys
    auto fh = session->add_new_handle(a.path, meta.instance_num);
    meta.subscribers[session->id] = fh;
    action::OpenReturn ret(0, "OK", fh);
    return ret.serialize();
  }

  ptr<buffer> apply_(action::CloseAction& a) {
    auto session = sdb_->find_session(a.session_id);
    if (!session) {
      return action::CloseReturn(-1, SESSION_NOT_FOUND_STR, false, "")
          .serialize();
    }
    auto ret = close_file_delete_ephermeral(*session, a.fh);
    release_lock(a.session_id, a.fh);
    return action::CloseReturn(0, "OK", !!ret, ret.value()).serialize();
  }

  std::optional<std::string> close_file_delete_ephermeral(
      session::Entry& session, int fh) {
    if (session.handle_inum(fh) == -1) return std::nullopt;
    session.close_handle(fh);
    std::string key = session.fh_to_key(fh);
    auto& [meta, content] = ds_->at(key);

    if (meta.subscribers.contains(session.id)) {
      meta.subscribers.erase(session.id);
      if (meta.subscribers.empty() && meta.is_ephemeral) {
        meta.file_exists = false;
        std::filesystem::path path{key};
        std::filesystem::path parent_path = path.parent_path();
        auto& [parent_meta, parent_content] = ds_->at(parent_path);
        {
          std::lock_guard lg(parent_meta.mutex);
          size_t pos = parent_content.find(std::string(1, '\0') +
                                           std::string(path.filename()));
          if (pos != std::string::npos) {
            parent_content.erase(pos,
                                 std::string(path.filename()).length() + 1);
            return parent_path;
          }
        }
      }
    }
    return std::nullopt;
  }

  ptr<buffer> apply_(action::StartSessionAction& a) {
    auto session = sdb_->create_session();
    action::StartSessionReturn ret(0, "OK", session->id);
    return ret.serialize();
  }

  ptr<buffer> apply_(action::EndSessionAction& a) {
    auto session = sdb_->find_session(a.session_id);
    if (!session) {
      return action::Response(-1, SESSION_NOT_FOUND_STR).serialize();
    }
    std::vector<std::string> parent_path;
    const std::string ok = "OK";
    int size =
        sizeof(int32_t) + sizeof(ok.size()) + ok.size() + sizeof(int32_t);
    for (int i = 0; i < session->handle_count(); ++i) {
      auto ret = close_file_delete_ephermeral(*session, i);
      if (ret) {
        parent_path.push_back(ret.value());
        size += parent_path.size() + sizeof(parent_path.size());
      }
      release_lock(a.session_id, i);
    }
    sdb_->delete_session(a.session_id);
    nuraft::ptr<nuraft::buffer> buf = nuraft::buffer::alloc(size);
    nuraft::buffer_serializer bs(buf);
    bs.put_i32(0);
    bs.put_str(ok);
    bs.put_i32(parent_path.size());
    for (auto& s : parent_path) {
      bs.put_str(s);
    }
    return buf;
  }

  ptr<buffer> apply_(action::SetContentAction& a) {
    auto session = sdb_->find_session(a.session_id);
    if (!session) {
      return action::Response(-1, SESSION_NOT_FOUND_STR).serialize();
    }
    auto& [meta, content] = ds_->at(session->fh_to_key(a.fh));
    if (session->handle_inum(a.fh) != meta.instance_num)
      return action::Response(-1, "Instance num mismatch").serialize();
    if (!meta.file_exists)
      return action::Response(-1, "File does not exist").serialize();
    content = a.content;
    return action::Response(0, "OK").serialize();
  }

  // this is try acquire.
  ptr<buffer> apply_(action::AcqAction& a) {
    // must acquired meta.mutex to be here
    auto session = sdb_->find_session(a.session_id);
    if (!session) {
      return action::Response(-1, SESSION_NOT_FOUND_STR).serialize();
    }
    auto key = session->fh_to_key(a.fh);
    auto& meta = ds_->at(key).first;

    if (a.ex) {  // Lock in exclusive mode
      if (meta.lock_owners.empty()) {
        meta.lock_owners.insert(session->id);
        meta.is_locked_ex = true;
        meta.lock_gen_num++;
        return action::Response(0, "").serialize();
      } else {
        return action::Response(1, "fail to acquire").serialize();
      }
    } else {  // Lock in shared mode
      if (meta.lock_owners.empty() || !meta.is_locked_ex) {
        if (meta.lock_owners.empty()) {
          meta.is_locked_ex = false;  // first reader needs to set it
          meta.lock_gen_num++;
        }
        meta.lock_owners.insert(session->id);

        puts("Successfully get lock");
        return action::Response(0, "").serialize();
      } else {
        puts("Failed to get lock");
        return action::Response(1, "fail to acquire").serialize();
      }
    }
  }

  ptr<buffer> apply_(action::RelAction& a) {
    int rc = release_lock(a.session_id, a.fh);
    if (rc == -2)
      return action::Response(-2, "file not found").serialize();
    else if (rc == -1)
      return action::Response(-1, "the session does not hold this lock")
          .serialize();
    else if (rc == 0)
      return action::Response(0, "").serialize();
    else {
      std::terminate();
      assert(false);
    }
  }

  // return: whether a lock is released.
  // -2: file not found
  // -1: the session does not hold this lock
  // 0: release succeed
  int release_lock(int session_id, int fh) {
    auto session = sdb_->find_session(session_id);
    if (session == nullptr) return -1;
    auto& [meta, content] = ds_->at(session->fh_to_key(fh));
    bool released;

    if (!meta.file_exists) return -2;
    released = meta.lock_owners.erase(session_id);
    // std::cout << "sess " << session_id << " rel lock @ "
    //           << session->fh_to_key(fh) << ": " << std::boolalpha << released
    //           << std::endl;
    if (meta.lock_owners.empty()) {
      meta.cv.notify_all();  // If a EX lock is released, *all* waiting SH reqs
      // should acquire the lock
    }
    return released ? 0 : -1;
  }

  ptr<buffer> apply_(action::DeleteAction& a) {
    auto session = sdb_->find_session(a.session_id);
    if (session == nullptr) {
      return action::Response({-1, SESSION_NOT_FOUND_STR}).serialize();
    }
    auto key = session->fh_to_key(a.fh);
    auto& [meta, content] = ds_->at(key);
    if (meta.is_directory && !content.empty()) {
      std::cout << "Directory is not empty" << std::endl;
      assert(0);
    }
    content.clear();
    {
      std::lock_guard<std::mutex> guard(meta.mutex);
      meta.file_exists = false;
      meta.instance_num++;
      meta.content_gen_num = 0;
      meta.lock_gen_num = 0;
      for (const int& session_id : meta.lock_owners) {
        session = sdb_->find_session(session_id);
        if (session) {
          session->enqueue_event(a.fh);
        }
      }
      meta.lock_owners.clear();
    }
    meta.cv.notify_all();
    std::filesystem::path path{key};
    std::filesystem::path parent_path = path.parent_path();
    auto& [parent_meta, parent_content] = ds_->at(parent_path);
    {
      std::lock_guard lg(parent_meta.mutex);
      size_t pos = parent_content.find(std::string(1, '\0') +
                                       std::string(path.filename()));
      if (pos != std::string::npos)
        parent_content.erase(pos, std::string(path.filename()).length() + 1);
    }
    action::Response res({0, ""});
    return res.serialize();
  }

  // Last committed Raft log number.
  std::atomic<uint64_t> last_committed_idx_;

  std::shared_ptr<session::Db> sdb_;
  std::shared_ptr<DataStore> ds_;
};
}  // namespace StateMachine
