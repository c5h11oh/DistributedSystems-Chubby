#include <experimental/propagate_const>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "includes/diagnostic.grpc.pb.h"

class SkinnyClient {
 public:
  SkinnyClient();
  ~SkinnyClient();

  int Open(const std::string &path,
           const std::optional<std::function<void(int)>> &cb = std::nullopt);
  int OpenDir(const std::string &path,
              const std::optional<std::function<void(int)>> &cb = std::nullopt);
  void Close(int fh);
  std::string GetContent(int fh);
  void SetContent(int fh, const std::string &content);
  bool TryAcquire(int fh, bool ex);
  bool Acquire(int fh, bool ex);
  void Release(int fh);
  void Delete(int fh);
  void TEST_set_no_implicit_end_session_on_destruct(bool val);

 private:
  class impl;
  std::experimental::propagate_const<std::unique_ptr<impl>> pImpl;
};

class SkinnyDiagnosticClient {
 public:
  SkinnyDiagnosticClient();
  int GetLeader();

 private:
  std::vector<std::unique_ptr<diagnostic::Diagnostic::Stub>> stubs_;
};
