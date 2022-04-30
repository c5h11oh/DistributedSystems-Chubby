#include <string>

#include "debug/_deps/nuraft-src/include/libnuraft/buffer.hxx"
#include "grpcpp/impl/codegen/config_protobuf.h"
#include "includes/action.pb.h"
#include "libnuraft/buffer.hxx"
#include "libnuraft/nuraft.hxx"

namespace action {
nuraft::ptr<nuraft::buffer> serialize(const grpc::protobuf::Message &msg) {
  std::string str;
  msg.SerializeToString(&str);
  auto buf = nuraft::buffer::alloc(str.size() + 4);
  buf->put(str.data(), str.size());
  return buf;
}

action::Response deserialize_to_res(nuraft::buffer &buf) {
  action::Response res;
  size_t len;
  auto bytes = buf.get_bytes(len);
  res.ParseFromArray(bytes, len);
  return res;
}
}  // namespace action
