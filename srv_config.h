#include <string>
#include <tuple>
#include <variant>
#include <vector>

// GRPC will be using raft port + 1
static const std::vector<std::tuple<std::string, int>> SRV_CONFIG{
    {"localhost", 10000}, {"localhost", 10010}, {"localhost", 10020}};
