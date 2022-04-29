cmake_minimum_required(VERSION 3.15)
project(Chubby)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -fsanitize=address")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" ${CMAKE_MODULE_PATH})
include(FetchContent)

fetchcontent_declare(
  gRPC
  GIT_REPOSITORY https://github.com/grpc/grpc
  GIT_TAG v1.45.2
)

fetchcontent_declare(
  nuraft
  GIT_REPOSITORY https://github.com/eBay/NuRaft
  GIT_TAG v1.3.0
)

fetchcontent_declare(
  asio
  GIT_REPOSITORY https://github.com/chriskohlhoff/asio
  GIT_TAG asio-1-22-1
)

set(gRPC_SSL_PROVIDER package)
set(FETCHCONTENT_QUIET OFF)
fetchcontent_makeavailable(gRPC)
fetchcontent_makeavailable(asio)
set(ASIO_INCLUDE_DIR ${asio_SOURCE_DIR}/asio/include)
fetchcontent_makeavailable(nuraft)


set(PROTOBUF_SRC "protos/skinny.proto")
set(PROTOBUF_DST "")
set(PROTOBUF_ABS_SRC "")
set(PROTOBUF_EXT ".grpc.pb.cc" ".grpc.pb.h" ".pb.cc" ".pb.h")
foreach(file ${PROTOBUF_SRC})
  foreach(ext ${PROTOBUF_EXT})
    get_filename_component(fileh ${file} NAME_WE)
    string(PREPEND fileh "${CMAKE_CURRENT_SOURCE_DIR}/includes/")
    string(APPEND fileh ${ext})
    list(APPEND PROTOBUF_DST ${fileh})
  endforeach()
  list(APPEND PROTOBUF_ABS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/${file}")
endforeach()


add_custom_command(
  OUTPUT ${PROTOBUF_DST}
  COMMAND ${grpc_BINARY_DIR}/third_party/protobuf/protoc
  --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/protos
  --cpp_out=${CMAKE_CURRENT_SOURCE_DIR}/includes
  --grpc_out=${CMAKE_CURRENT_SOURCE_DIR}/includes
  --plugin=protoc-gen-grpc=${grpc_BINARY_DIR}/grpc_cpp_plugin
  ${PROTOBUF_ABS_SRC}
  DEPENDS ${PROTOBUF_SRC} protoc grpc_cpp_plugin grpc++_reflection
)

add_executable(client client.cpp ${PROTOBUF_DST})
target_link_libraries(client grpc++)

add_executable(client2 client2.cpp ${PROTOBUF_DST})
target_link_libraries(client2 grpc++)

add_executable(client_sub client_sub.cpp ${PROTOBUF_DST})
target_link_libraries(client_sub grpc++)

add_executable(codegen codegen.cpp)
target_link_libraries(codegen libprotobuf)
target_include_directories(codegen PUBLIC ${grpc_SOURCE_DIR}/third_party/protobuf/src)

add_custom_target(
  action
  COMMAND ${CMAKE_CURRENT_BINARY_DIR}/codegen ${CMAKE_CURRENT_SOURCE_DIR}/protos/raft.proto > ${CMAKE_CURRENT_SOURCE_DIR}/includes/action.cpp
  DEPENDS codegen ${CMAKE_CURRENT_SOURCE_DIR}/protos/raft.proto
)

find_package(OpenSSL REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
add_executable(server server.cpp ${nuraft_SOURCE_DIR}/examples/in_memory_log_store.cxx ${PROTOBUF_DST})
add_dependencies(server action)
target_include_directories(server PUBLIC ${nuraft_SOURCE_DIR}/include)
target_include_directories(server PUBLIC ${nuraft_SOURCE_DIR}/include/libnuraft)
target_include_directories(server PUBLIC ${nuraft_SOURCE_DIR}/examples)
target_link_libraries(server static_lib Threads::Threads grpc++ OpenSSL::SSL)
