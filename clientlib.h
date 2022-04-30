#include <experimental/propagate_const>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace grpc {
class Status;
}
class SkinnyClient {
 public:
  SkinnyClient();
  ~SkinnyClient();

  int Open(const std::string &path,
           const std::optional<std::function<void()>> &cb = std::nullopt);
  std::string GetContent(int fh);
  void SetContent(int fh, const std::string &content);
  bool TryAcquire(int fh, bool ex);
  bool Acquire(int fh, bool ex);
  void Release(int fh);

 private:
  class impl;
  std::experimental::propagate_const<std::unique_ptr<impl>> pImpl;
};
