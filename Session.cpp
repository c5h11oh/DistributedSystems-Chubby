#include <string>
#include <vector>

class Session {
public:
  int id;
  Session() : id(123) {}
  int add_new_handle(std::string path) {
    v.push_back(path);
    return v.size() - 1;
  }

  const std::string &fh_to_key(int fh) { return v.at(fh); }

private:
  std::vector<std::string> v;
};
