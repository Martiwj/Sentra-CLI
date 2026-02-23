#pragma once

#include <string>
#include <vector>
#include <optional>

#include "sentra/types.hpp"

namespace sentra {

class SessionStore {
 public:
  explicit SessionStore(std::string base_dir);

  std::string create_session_id() const;
  std::vector<Message> load(const std::string& session_id) const;
  void append(const std::string& session_id, const Message& message) const;
  void ensure_session(const std::string& session_id, const std::string& active_model_id,
                      const std::string& runtime_name) const;
  void update_metadata(const std::string& session_id, const std::string& active_model_id,
                       const std::string& runtime_name) const;
  std::optional<SessionMetadata> load_metadata(const std::string& session_id) const;
  std::vector<SessionMetadata> list_sessions() const;

 private:
  std::string base_dir_;

  std::string path_for(const std::string& session_id) const;
  std::string metadata_path_for(const std::string& session_id) const;
  static std::string escape(const std::string& input);
  static std::string unescape(const std::string& input);
};

}  // namespace sentra
