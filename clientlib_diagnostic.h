#include "includes/diagnostic.grpc.pb.h"

class SkinnyDiagnosticClient {
 public:
  SkinnyDiagnosticClient();
  int GetLeader();

 private:
  std::vector<std::unique_ptr<diagnostic::Diagnostic::Stub>> stubs_;
};
