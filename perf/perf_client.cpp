#include <condition_variable>
#include <latch>
#include <mutex>
#include <random>
#include <string>
#include <thread>

#include "../clientlib.h"
using namespace std::chrono_literals;

const int num_file = 10;
const std::string dirname = "/perf";
const std::string clientdir = dirname + "/client";
const std::string filename_prefix = "/file";

int main(int argc, char** argv) {
  // in: #threads, duration, write_ratio, ---start_time---
  // out: #ops (read/write) to server, #ops in cache
  if (argc != 6) {
    std::cerr << "usage: " << argv[0]
              << " node_num node_cnt thd_cnt duration write_ratio" << std::endl;
    exit(1);
  }
  const int node_num = std::stoi(std::string(argv[1]));
  const int node_cnt = std::stoi(std::string(argv[2]));
  const int thd_cnt = std::stoi(std::string(argv[3]));
  const int duration = std::stoi(std::string(argv[4]));
  const float write_ratio = std::stof(std::string(argv[5]));

  int read_ops[thd_cnt], write_ops[thd_cnt];
  int sum_read_ops = 0, sum_write_ops = 0;
  memset(read_ops, 0, sizeof(int) * thd_cnt);
  memset(write_ops, 0, sizeof(int) * thd_cnt);

  std::latch all_thd_ready(thd_cnt), all_node_ready(1);
  auto f = [&](int thd_num) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> write_lottery(0.0, 1.0);
    std::uniform_int_distribution<> file_lottery(0, num_file - 1);
    SkinnyClient sc;
    int fh[num_file];
    for (int i = 0; i < num_file; ++i)
      fh[i] = sc.Open(dirname + filename_prefix + std::to_string(i));
    all_thd_ready.count_down();
    all_node_ready.wait();
    auto deadline =
        std::chrono::system_clock::now() + std::chrono::seconds(duration);
    while (std::chrono::system_clock::now() <= deadline) {
      double res = write_lottery(gen);
      if (res < write_ratio) {  // write
        sc.SetContent(fh[file_lottery(gen)], "garbage" + std::to_string(res));
        ++write_ops[thd_num];
      } else {  // read
        sc.GetContent(fh[file_lottery(gen)]);
        ++read_ops[thd_num];
      }
    }
  };

  auto file_count = [](std::string&& content) {
    int cnt = 0;
    for (char c : content) cnt += (c == '\0');
    return cnt;
  };

  SkinnyClient sc;
  int dirfh = sc.OpenDir(dirname);

  // get threads in this node ready
  std::vector<std::thread> vt;
  for (int i = 0; i < thd_cnt; ++i) vt.emplace_back(f, i);
  all_thd_ready.wait();

  // wait all nodes to finish setup
  int finished_node_cnt;
  std::mutex m;
  std::condition_variable cv;
  int clientdirfh = sc.OpenDir(clientdir, [&](int fh) {
    std::cout << "callback" << std::endl;
    std::lock_guard<std::mutex> lg(m);
    finished_node_cnt = file_count(sc.GetContent(fh));
    std::cout << "callback: finished_node_cnt = " << finished_node_cnt
              << std::endl;
    if (finished_node_cnt == node_cnt) {
      cv.notify_one();
    }
  });
  int mynodefh = sc.Open(clientdir + "/node" + std::to_string(node_num));
  {
    std::unique_lock ul(m);
    finished_node_cnt = file_count(sc.GetContent(clientdirfh));
    while (finished_node_cnt != node_cnt) {
      cv.wait_for(ul, 1s);
      std::cout << "in while: finished_node_cnt=" << finished_node_cnt
                << std::endl;
    }
  }
  std::cout << "outside: finished_node_cnt=" << finished_node_cnt << std::endl;
  all_node_ready.count_down();

  for (auto& t : vt) t.join();
  for (int i = 0; i < thd_cnt; ++i) {
    sum_read_ops += read_ops[i];
    sum_write_ops += write_ops[i];
  }

  sc.Delete(mynodefh);

  std::cout << "sum_read_ops=" << sum_read_ops
            << ", sum_write_ops=" << sum_write_ops << std::endl;
  return 0;
}