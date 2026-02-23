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
  std::size_t max_tokens{256};
};

struct GenerationResult {
  std::string text;
};

}  // namespace sentra
