#include <cassert>
#include <memory>
#include <unordered_map>

#include "Session.cpp"

class SessionDb {
  std::unordered_map<int, std::shared_ptr<Session>> session_db;

 public:
  std::shared_ptr<Session> create_session() {
    auto session = std::make_shared<Session>();
    session_db[session->id] = session;
    return session;
  }

  std::shared_ptr<Session> find_session(int id) {
    auto it = session_db.find(id);
    assert(it != session_db.end());
    return it->second;
  }

  void delete_session(int id) {
    auto it = session_db.find(id);
    assert(it != session_db.end());
    session_db.erase(it);
  }
};
