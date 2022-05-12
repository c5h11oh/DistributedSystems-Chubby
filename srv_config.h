#pragma once
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "Session.cpp"

std::string host_suffix{".skinnyclient.advosuwmadison-pg0.clemson.cloudlab.us"};

// GRPC will be using raft port + 1
static const std::vector<std::tuple<std::string, int>> SRV_CONFIG{
    {"node0" + host_suffix, 10200}, {"node1" + host_suffix, 12010},
    {"node2" + host_suffix, 10220}, {"node3" + host_suffix, 10220},
    {"node4" + host_suffix, 10220},
};

namespace skinny {
enum class ErrorCode { NOT_LEADER = 100, LOCK_RELATED = 101 };
}

class FileMetaData {
 public:
  FileMetaData()
      : file_exists(false),
        instance_num(0),
        content_gen_num(0),
        lock_gen_num(0),
        is_directory(0) {}

  std::unordered_set<int> lock_owners;
  bool is_locked_ex;
  std::mutex mutex;
  std::condition_variable cv;
  std::unordered_map<int, int> subscribers;  // sessionid: fh

  bool file_exists;
  int instance_num;
  int content_gen_num;
  int lock_gen_num;
  bool is_directory;
  bool is_ephemeral;
};

using DataStore =
    std::unordered_map<std::string, std::pair<FileMetaData, std::string>>;

const std::string SESSION_NOT_FOUND_STR = "Session Not Found";
const grpc::Status SESSION_NOT_FOUND_STATUS =
    grpc::Status(grpc::StatusCode::CANCELLED, SESSION_NOT_FOUND_STR);