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

  ~Session() {
    if (kathread && kathread->joinable()) kathread->join();
  }

  int add_new_handle(std::string path, int instance_num) {
    v.push_back(path);
    inum.push_back(instance_num);
    return v.size() - 1;
  }

  void enqueue_event(int fh) {
    event_queue.push(fh);
    ecv.notify_one();
  }

  const std::string &fh_to_key(int fh) { return v.at(fh); }
  const int &fh_to_inum(int fh) { return inum.at(fh); }

 private:
  std::vector<std::string> v;
  std::vector<int> inum;  // instance_num
};
