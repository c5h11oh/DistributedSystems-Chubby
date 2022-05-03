#pragma once

#include <grpcpp/grpcpp.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "includes/skinny.pb.h"
namespace session {
class Db;
class KAThread {
 public:
  KAThread(int sid, std::function<void(int)> expire_cb)
      : expire_cb_(expire_cb),
        sid_(sid),
        t_([this] {
          using namespace std::chrono_literals;
          while (true) {
            std::unique_lock ul(cv_lock_);
            auto ret = cv_.wait_for(ul, 1s);
            // will wake if received new keepalive, or new event, or cancelled
            // or timeout
            if (cancelled_.load()) return;
            {
              std::lock_guard lg(reactor_lock_);
              if (reactor_ && res_) {
                // If have active keepalive
                if (ret == std::cv_status::timeout) {
                  //  timeout => ack keepalive
                  reactor_->Finish(grpc::Status::OK);
                  reactor_ = nullptr;
                  res_ = nullptr;
                } else if (std::lock_guard lg(event_queue_lock_);
                           !event_queue_.empty()) {
                  // Got event => return event
                  res_->set_fh(event_queue_.front());
                  event_queue_.pop();
                  reactor_->Finish(grpc::Status::OK);
                  reactor_ = nullptr;
                  res_ = nullptr;
                }
                // wake due to new keep alive => do nothing
              } else if (ret == std::cv_status::timeout) {
                // no active keepalive and timeout => call expired callback
                std::invoke(expire_cb_, sid_);
                return;
              }
              // Other cases:
              // 1. no active keep alive but got event => do nothing
              // 2. no active keep alive and received new keepalive => no gonna
              // happen
            }
          }
        }),
        res_(nullptr),
        reactor_(nullptr) {}

  ~KAThread() {
    cancel();
    if (std::this_thread::get_id() == t_.get_id()) {
      t_.detach();
    } else {
      t_.join();
    }
  }

  void cancel() {
    cancelled_.store(1);
    cv_.notify_one();
  }

  void set_reactor(grpc::ServerUnaryReactor *reactor, skinny::Event *res) {
    {
      std::lock_guard lg(reactor_lock_);
      reactor_ = reactor;
      res_ = res;
    }
    cv_.notify_one();
  }

  void enqueue_event(int fh) {
    {
      std::lock_guard l(event_queue_lock_);
      event_queue_.push(fh);
    }
    cv_.notify_one();
  }

 private:
  std::thread t_;
  std::mutex reactor_lock_, event_queue_lock_, cv_lock_;
  std::condition_variable cv_;
  std::atomic<bool> cancelled_;
  grpc::ServerUnaryReactor *reactor_;
  std::queue<int> event_queue_;
  skinny::Event *res_;
  std::function<void(int)> expire_cb_;
  int sid_;

  int pop_event() {
    std::lock_guard l(event_queue_lock_);
    assert(!event_queue_.empty());
    int fh = event_queue_.front();
    event_queue_.pop();
    return fh;
  }
};

class Entry {
 public:
  int id;
  KAThread kathread;

  Entry(const std::function<void(int)> &cb)
      : id(next_id.fetch_add(1, std::memory_order_relaxed)), kathread(id, cb) {}

  ~Entry() { std::cout << "Destruct session entry" << std::endl; }

  int add_new_handle(std::string path, int instance_num) {
    v.push_back(path);
    inum.push_back(instance_num);
    return v.size() - 1;
  }
  void close_handle(int fh) { inum.at(fh) = -1; }
  int handle_count() { return v.size(); }
  const int &handle_inum(int fh) { return inum.at(fh); }

  const std::string &fh_to_key(int fh) { return v.at(fh); }

 private:
  std::vector<std::string> v;
  std::vector<int> inum;  // instance_num
  static std::atomic<int> inline next_id{0};
  std::queue<int> event_queue;  // queue<fh>
};

class Db {
  std::unordered_map<int, std::shared_ptr<Entry>> session_db;
  std::mutex db_lock;

 public:
  std::shared_ptr<Entry> create_session() {
    auto session =
        std::make_shared<Entry>([this](int id) { delete_session(id); }
                                // TODO unlock, raft ...
        );
    {
      std::lock_guard lg(db_lock);
      session_db[session->id] = session;
    }
    return session;
  }

  std::shared_ptr<Entry> find_session(int id) {
    std::lock_guard lg(db_lock);
    auto it = session_db.find(id);
    assert(it != session_db.end());
    return it->second;
  }

  void delete_session(int id) {
    std::lock_guard lg(db_lock);
    auto it = session_db.find(id);
    assert(it != session_db.end());
    session_db.erase(it);
  }
};
}  // namespace session
