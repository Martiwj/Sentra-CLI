#include "sentra/session_store.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace sentra {

std::string role_to_string(Role role) {
  switch (role) {
    case Role::System:
      return "system";
    case Role::User:
      return "user";
    case Role::Assistant:
      return "assistant";
  }
  return "user";
}

Role role_from_string(const std::string& value) {
  if (value == "system") {
    return Role::System;
  }
  if (value == "assistant") {
    return Role::Assistant;
  }
  return Role::User;
}

SessionStore::SessionStore(std::string base_dir) : base_dir_(std::move(base_dir)) {
  std::filesystem::create_directories(base_dir_);
}

std::string SessionStore::create_session_id() const {
  const auto now = std::chrono::system_clock::now();
  const auto t = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
  return "session-" + std::to_string(t);
}

std::vector<Message> SessionStore::load(const std::string& session_id) const {
  std::vector<Message> messages;
  std::ifstream in(path_for(session_id));
  if (!in.is_open()) {
    return messages;
  }

  std::string line;
  while (std::getline(in, line)) {
    const auto sep = line.find('\t');
    if (sep == std::string::npos) {
      continue;
    }
    const std::string role_text = line.substr(0, sep);
    const std::string content = unescape(line.substr(sep + 1));
    messages.push_back({role_from_string(role_text), content});
  }

  return messages;
}

void SessionStore::append(const std::string& session_id, const Message& message) const {
  std::ofstream out(path_for(session_id), std::ios::app);
  if (!out.is_open()) {
    throw std::runtime_error("failed to open session file for append");
  }
  out << role_to_string(message.role) << '\t' << escape(message.content) << '\n';
}

std::string SessionStore::path_for(const std::string& session_id) const {
  return base_dir_ + "/" + session_id + ".log";
}

std::string SessionStore::escape(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  for (char c : input) {
    if (c == '\\') {
      out += "\\\\";
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\t') {
      out += "\\t";
    } else {
      out.push_back(c);
    }
  }
  return out;
}

std::string SessionStore::unescape(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  for (std::size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '\\' && i + 1 < input.size()) {
      const char next = input[i + 1];
      if (next == 'n') {
        out.push_back('\n');
        ++i;
        continue;
      }
      if (next == 't') {
        out.push_back('\t');
        ++i;
        continue;
      }
      if (next == '\\') {
        out.push_back('\\');
        ++i;
        continue;
      }
    }
    out.push_back(input[i]);
  }
  return out;
}

}  // namespace sentra
