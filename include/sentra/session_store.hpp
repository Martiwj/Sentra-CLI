#pragma once

#include <string>
#include <vector>

#include "sentra/types.hpp"

namespace sentra {

class SessionStore {
 public:
  explicit SessionStore(std::string base_dir);

  std::string create_session_id() const;
  std::vector<Message> load(const std::string& session_id) const;
  void append(const std::string& session_id, const Message& message) const;

 private:
  std::string base_dir_;

  std::string path_for(const std::string& session_id) const;
  static std::string escape(const std::string& input);
  static std::string unescape(const std::string& input);
};

}  // namespace sentra
