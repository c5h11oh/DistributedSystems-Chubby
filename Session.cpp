#include <atomic>
#include <condition_variable>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace session {
class Entry {
 public:
  int id;

  std::unique_ptr<std::thread> kathread;
  std::condition_variable ecv;
  std::mutex emu;
  std::queue<int> event_queue;  // queue<fh>

  Entry() { id = next_id.fetch_add(1, std::memory_order_relaxed); }

  ~Entry() {
    if (kathread && kathread->joinable()) kathread->join();
  }

  int add_new_handle(std::string path, int instance_num) {
    v.push_back(path);
    inum.push_back(instance_num);
    return v.size() - 1;
  }
  void close_handle(int fh) { inum.at(fh) = -1; }
  int handle_count() { return v.size(); }
  const int &handle_inum(int fh) { return inum.at(fh); }

  void enqueue_event(int fh) {
    event_queue.push(fh);
    ecv.notify_one();
  }

  const std::string &fh_to_key(int fh) { return v.at(fh); }

 private:
  std::vector<std::string> v;
  std::vector<int> inum;  // instance_num
  static std::atomic<int> inline next_id{0};
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
