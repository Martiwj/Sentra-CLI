#pragma once

#include <string>
#include <vector>
#include <optional>

#include "sentra/types.hpp"

namespace sentra {

class SessionStore {
 public:
  explicit SessionStore(std::string baseDir);

  std::string create_session_id() const;
  std::vector<Message> load(const std::string& sessionId) const;
  void append(const std::string& sessionId, const Message& message) const;
  void ensure_session(const std::string& sessionId, const std::string& activeModelId,
                      const std::string& runtimeName) const;
  void update_metadata(const std::string& sessionId, const std::string& activeModelId,
                       const std::string& runtimeName) const;
  std::optional<SessionMetadata> load_metadata(const std::string& sessionId) const;
  std::vector<SessionMetadata> list_sessions() const;

 private:
  std::string m_baseDir;

  std::string path_for(const std::string& sessionId) const;
  std::string metadata_path_for(const std::string& sessionId) const;
  static std::string escape(const std::string& input);
  static std::string unescape(const std::string& input);
};

}  // namespace sentra
