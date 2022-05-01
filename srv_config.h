#pragma once
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "Session.cpp"

// GRPC will be using raft port + 1
static const std::vector<std::tuple<std::string, int>> SRV_CONFIG{
    {"localhost", 10000}, {"localhost", 10010}, {"localhost", 10020}};

namespace skinny {
enum class ErrorCode { NOT_LEADER = 100, LOCK_RELATED = 101 };
}

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

using DataStore =
    std::unordered_map<std::string, std::pair<FileMetaData, std::string>>;
