#include <barrier>
#include <random>
#include <string>
#include <thread>

#include "../clientlib.h"
using namespace std::chrono_literals;

const int num_file = 10;
const std::string dirname = "/perf";
const std::string filename_prefix = "/file";

int main(int argc, char** argv) {
  // in: #threads, duration, write_ratio, ---start_time---
  // out: #ops (read/write) to server, #ops in cache
  const int num_thd = std::stoi(std::string(argv[1]));
  const int duration = std::stoi(std::string(argv[2]));
  const float write_ratio = std::stof(std::string(argv[3]));

  int read_ops[num_thd], write_ops[num_thd];
  int sum_read_ops = 0, sum_write_ops = 0;
  memset(read_ops, 0, sizeof(int) * num_thd);
  memset(write_ops, 0, sizeof(int) * num_thd);

  std::barrier barrier(num_thd);
  auto f = [&](int thd_num) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> write_lottery(0.0, 1.0);
    std::uniform_int_distribution<> file_lottery(0, num_file - 1);
    SkinnyClient sc;
    int fh[num_file];
    for (int i = 0; i < num_file; ++i)
      fh[i] = sc.Open(dirname + filename_prefix + std::to_string(i));
    barrier.arrive_and_wait();
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
    std::cout << "thd" << thd_num << std::endl;
  };
  SkinnyClient sc;
  int dirfh = sc.OpenDir(dirname);
  std::cout << "dirfh=" << dirfh << std::endl;
  std::vector<std::thread> vt;
  for (int i = 0; i < num_thd; ++i) vt.emplace_back(f, i);
  for (auto& t : vt) t.join();
  for (int i = 0; i < num_thd; ++i) {
    sum_read_ops += read_ops[i];
    sum_write_ops += write_ops[i];
  }

  std::cout << "sum_read_ops=" << sum_read_ops
            << ", sum_write_ops=" << sum_write_ops << std::endl;
  return 0;
}