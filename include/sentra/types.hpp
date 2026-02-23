#pragma once

#include <string>
#include <vector>

namespace sentra {

enum class Role {
  System,
  User,
  Assistant,
};

std::string role_to_string(Role role);
Role role_from_string(const std::string& value);

struct Message {
  Role role;
  std::string content;
};

struct GenerationRequest {
  std::vector<Message> messages;
  std::string model_id;
  std::string model_path;
  std::size_t max_tokens{256};
};

struct GenerationResult {
  std::string text;
  bool context_truncated{false};
  std::string warning;
};

struct ModelSpec {
  std::string id;
  std::string name;
  std::string hf_repo;
  std::string hf_file;
  std::string local_path;
};

struct SessionMetadata {
  std::string session_id;
  long long created_at_epoch{0};
  std::string active_model_id;
  std::string runtime_name;
};

}  // namespace sentra
