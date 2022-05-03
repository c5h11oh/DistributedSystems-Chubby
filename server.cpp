#include <fcntl.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/server_callback.h>
#include <grpcpp/support/sync_stream.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <condition_variable>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

#include "SkinnyImpl.cpp"
#include "StateMachine.cpp"
#include "in_memory_state_mgr.hxx"
#include "libnuraft/nuraft.hxx"
#include "libnuraft/srv_config.hxx"
#include "raft_server.hxx"
#include "srv_config.h"

auto init_grpc(int node_id, std::shared_ptr<nuraft::raft_server> raft,
               std::shared_ptr<DataStore> ds,
               std::shared_ptr<session::Db> sdb) {
  std::string server_address(
      "localhost:" + std::to_string(std::get<1>(SRV_CONFIG[node_id]) + 1));
  SkinnyImpl service(raft, ds, sdb);
  SkinnyCbImpl cbservice(raft, sdb);
  grpc::ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.SetMaxSendMessageSize(INT_MAX);
  builder.SetMaxReceiveMessageSize(INT_MAX);
  builder.SetMaxMessageSize(INT_MAX);

  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  builder.RegisterService(&cbservice);
  // Finally assemble the server.
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;
  server->Wait();
  return server;
}

auto init_raft(int node_id, std::shared_ptr<DataStore> ds,
               std::shared_ptr<session::Db> sdb) {
  using namespace nuraft;
  const auto [host, port] = SRV_CONFIG[node_id];
  const auto endpoint = host + ":" + std::to_string(port);
  // Replace with your logger, state machine, and state manager.
  ptr<logger> my_logger = nullptr;
  ptr<state_machine> my_state_machine =
      cs_new<StateMachine::StateMachine>(ds, sdb);
  ptr<state_mgr> my_state_manager = cs_new<inmem_state_mgr>(node_id, endpoint);

  asio_service::options asio_opt;  // your Asio options
  raft_params params;              // your Raft parameters
  params.return_method_ = raft_params::blocking;
  params.client_req_timeout_ = INT_MAX;

  raft_launcher launcher;
  ptr<raft_server> server = launcher.init(my_state_machine, my_state_manager,
                                          my_logger, port, asio_opt, params);

  // Need to wait for initialization.
  while (!server->is_initialized()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  for (int i = 0; i < SRV_CONFIG.size(); i++) {
    if (i == node_id) continue;
    std::cout << "Waiting for node " << i << std::endl;
    const auto [fhost, fport] = SRV_CONFIG[i];
    const auto fendpoint = fhost + ":" + std::to_string(fport);
    ptr<srv_config> ret;
    do {
      server->add_srv({i, fendpoint})->get_result_code();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      ret = server->get_srv_config(i);
    } while (ret == nullptr);
  }

  std::cout << "Leader: " << server->get_leader() << std::endl;
  return launcher;
}
int main(int argc, char **argv) {
  assert(argc >= 2);
  const int node_id = atoi(argv[1]);
  nuraft::ptr<nuraft::raft_server> raft = nullptr;
  auto sdb = std::make_shared<session::Db>([&raft](int sid) {
    if (!raft || !raft->is_leader()) return;
    std::thread t([&raft, sid]() {
      action::EndSessionAction a(sid);
      auto ret = raft->append_entries({a.serialize()});
    });
    t.detach();
    return;
  });
  auto datastore = std::make_shared<DataStore>();
  auto launcher = init_raft(node_id, datastore, sdb);
  raft = launcher.get_raft_server();
  auto server = init_grpc(node_id, launcher.get_raft_server(), datastore, sdb);
  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  launcher.shutdown();
}
