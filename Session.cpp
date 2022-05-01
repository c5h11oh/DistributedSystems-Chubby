#pragma once

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "includes/skinny.pb.h"
namespace session {
class Entry {
 public:
  int id;

  Entry() { id = next_id.fetch_add(1, std::memory_order_relaxed); }

  int add_new_handle(std::string path, int instance_num) {
    v.push_back(path);
    inum.push_back(instance_num);
    return v.size() - 1;
  }
  void close_handle(int fh) { inum.at(fh) = -1; }
  int handle_count() { return v.size(); }
  const int &handle_inum(int fh) { return inum.at(fh); }

  void enqueue_event(int fh) {
    {
      std::lock_guard l(event_queue_lock);
      event_queue.push(fh);
    }
    if (kathread) kathread->request_stop();
  }

  const std::string &fh_to_key(int fh) { return v.at(fh); }

  void setup_kathread(grpc::ServerUnaryReactor *reactor, skinny::Event *res) {
    if (!event_queue.empty()) {
      res->set_fh(pop_event());
      reactor->Finish(grpc::Status::OK);
      return;
    }
    kathread = std::jthread([this, reactor, res](std::stop_token stoken) {
      using namespace std::chrono_literals;
      std::mutex mutex;
      std::unique_lock lk(mutex);
      if (std::condition_variable_any().wait_for(
              lk, stoken, 1s, [this]() { return !event_queue.empty(); })) {
        res->set_fh(pop_event());
        event_queue.pop();
      }
      reactor->Finish(grpc::Status::OK);
    });
  }

 private:
  std::vector<std::string> v;
  std::vector<int> inum;  // instance_num
  static std::atomic<int> inline next_id{0};
  std::queue<int> event_queue;  // queue<fh>
  std::optional<std::jthread> kathread;
  std::mutex event_queue_lock;

  int pop_event() {
    std::lock_guard l(event_queue_lock);
    assert(!event_queue.empty());
    int fh = event_queue.front();
    event_queue.pop();
    return fh;
  }
};

class Db {
  std::unordered_map<int, std::shared_ptr<Entry>> session_db;

 public:
  std::shared_ptr<Entry> create_session() {
    auto session = std::make_shared<Entry>();
    session_db[session->id] = session;
    return session;
  }

  std::shared_ptr<Entry> find_session(int id) {
    auto it = session_db.find(id);
    assert(it != session_db.end());
    return it->second;
  }

  void delete_session(int id) {
    auto it = session_db.find(id);
    assert(it != session_db.end());
    session_db.erase(it);
  }
};
}  // namespace session
