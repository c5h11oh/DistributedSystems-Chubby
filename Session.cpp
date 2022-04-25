#include <condition_variable>
#include <queue>
#include <string>
#include <thread>
#include <vector>

class Session {
 public:
  int id;
  std::unique_ptr<std::thread> kathread;
  std::condition_variable ecv;
  std::mutex emu;
  std::queue<int> event_queue;  // queue<fh>
  Session() : id(123) {}

  int add_new_handle(std::string path) {
    v.push_back(path);
    return v.size() - 1;
  }

  void enqueue_event(int fh) { event_queue.push(fh); }

  const std::string &fh_to_key(int fh) { return v.at(fh); }

 private:
  std::vector<std::string> v;
};
