#pragma once

#include <grpcpp/grpcpp.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "includes/skinny.pb.h"
namespace session {
class Db;
class KAThread {
 public:
  KAThread(int sid, const std::function<void(int)> &expire_cb)
      : sid_(sid),
        expire_cb_(expire_cb),
        t_([this] {
          using namespace std::chrono_literals;
          while (true) {
            std::unique_lock ul(cv_lock_);
            auto ret = cv_.wait_for(ul, 1s);
            // will wake if received new keepalive, or new event, or cancelled
            // or timeout
            if (cancelled_.load()) {
              std::cout << "KAThread " << sid_ << " exited\n";
              return;
            }
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
                  res_->set_event_id(event_queue_.front().first);
                  res_->set_fh(event_queue_.front().second);
                  event_queue_.pop();
                  reactor_->Finish(grpc::Status::OK);
                  reactor_ = nullptr;
                  res_ = nullptr;
                }
                // wake due to new keep alive => do nothing
              } else if (ret == std::cv_status::timeout) {
                // no active keepalive and timeout => call expired callback
                cv_lock_.unlock();
                std::invoke(expire_cb_, sid_);
                cv_lock_.lock();
              }
              // Other cases:
              // 1. no active keep alive but got event => do nothing
              // 2. no active keep alive and received new keepalive => not gonna
              // happen
            }
          }
        }),
        res_(nullptr),
        reactor_(nullptr) {}

  ~KAThread() {
    cancel();
    t_.join();
  }

  void set_reactor(grpc::ServerUnaryReactor *reactor, skinny::Event *res,
                   int acked_eid) {
    {
      std::lock_guard lg(reactor_lock_);
      reactor_ = reactor;
      res_ = res;
    }
    {
      std::lock_guard lg(us_lock_);
      acked_events.insert(acked_eid);
      ack_event_cv_.notify_all();
    }
    cv_.notify_one();
  }

  int enqueue_event(int fh) {
    int new_eid = event_id++;
    {
      std::lock_guard l(event_queue_lock_);
      event_queue_.push({new_eid, fh});
    }
    cv_.notify_one();
    return new_eid;
  }

  void block_until_event_acked(int eid) {
    std::unique_lock lk(us_lock_);
    while (!acked_events.contains(eid) && !cancelled_.load()) {
      ack_event_cv_.wait(lk);
    }
    acked_events.erase(eid);
  }

 private:
  std::thread t_;
  std::mutex reactor_lock_, event_queue_lock_, cv_lock_, us_lock_;
  std::condition_variable cv_, ack_event_cv_;
  std::atomic<bool> cancelled_;
  grpc::ServerUnaryReactor *reactor_;
  std::queue<std::pair<int, int>> event_queue_;  // <event_id, fh>
  int event_id;
  std::unordered_set<int> acked_events;
  skinny::Event *res_;
  const std::function<void(int)> &expire_cb_;
  int sid_;

  void cancel() {
    cancelled_.store(1);
    cv_.notify_one();
    ack_event_cv_.notify_all();
  }

  int pop_event() {
    std::lock_guard l(event_queue_lock_);
    assert(!event_queue_.empty());
    int fh = event_queue_.front().second;
    event_queue_.pop();
    return fh;
  }
};

class Entry {
 public:
  int id;

  Entry(const std::function<void(int)> &cb)
      : id(next_id.fetch_add(1, std::memory_order_relaxed)), cb(cb) {}

  ~Entry() { std::cout << "Destruct session entry" << std::endl; }

  void start_kathread() {
    std::unique_lock lk(kalock_);
    kathread = std::make_unique<KAThread>(id, cb);
  }

  int add_new_handle(std::string path, int instance_num) {
    v.push_back(path);
    inum.push_back(instance_num);
    return v.size() - 1;
  }
  void close_handle(int fh) { inum.at(fh) = -1; }
  int handle_count() const { return v.size(); }
  const int &handle_inum(int fh) { return inum.at(fh); }

  const std::string &fh_to_key(int fh) const { return v.at(fh); }

  std::optional<int> enqueue_event(int fh) {
    std::shared_lock lk(kalock_);
    if (kathread) return kathread->enqueue_event(fh);
    return std::nullopt;
  }

  void set_reactor(grpc::ServerUnaryReactor *reactor, skinny::Event *res,
                   int acked_eid) {
    std::shared_lock lk(kalock_);
    if (kathread) kathread->set_reactor(reactor, res, acked_eid);
  }

  void block_until_event_acked(int eid) {
    std::shared_lock lk(kalock_);
    if (kathread) kathread->block_until_event_acked(eid);
  }

 private:
  std::vector<std::string> v;
  std::vector<int> inum;  // instance_num
  static std::atomic<int> inline next_id{0};
  const std::function<void(int)> &cb;
  std::unique_ptr<KAThread> kathread;
  std::shared_mutex kalock_;
};

class Db {
  std::unordered_map<int, std::shared_ptr<Entry>> session_db;
  std::mutex db_lock;
  std::function<void(int)> expire_cb_;

 public:
  Db(const std::function<void(int)> &cb) : expire_cb_(cb) {}

  std::shared_ptr<Entry> create_session() {
    auto session = std::make_shared<Entry>(expire_cb_);
    {
      std::lock_guard lg(db_lock);
      session_db[session->id] = session;
    }
    return session;
  }

  std::shared_ptr<Entry> find_session(int id) {
    std::lock_guard lg(db_lock);
    auto it = session_db.find(id);
    return it == session_db.end() ? nullptr : it->second;
  }

  void delete_session(int id) {
    std::lock_guard lg(db_lock);
    auto it = session_db.find(id);
    assert(it != session_db.end());
    session_db.erase(it);
  }

  void start_kathread() {
    std::lock_guard lg(db_lock);
    for (auto &it : session_db) it.second->start_kathread();
  }
};
}  // namespace session
