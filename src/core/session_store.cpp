#include "sentra/session_store.hpp"

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
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
    std::istringstream row(line);
    std::vector<std::string> cols;
    std::string col;
    while (std::getline(row, col, '\t')) {
      cols.push_back(col);
    }

    if (cols.size() == 2) {
      messages.push_back({role_from_string(cols[0]), unescape(cols[1])});
      continue;
    }
    if (cols.size() >= 4 && cols[0] == "v1" && cols[1] == "msg") {
      messages.push_back({role_from_string(cols[2]), unescape(cols[3])});
      continue;
    }
  }

  return messages;
}

void SessionStore::append(const std::string& session_id, const Message& message) const {
  std::ofstream out(path_for(session_id), std::ios::app);
  if (!out.is_open()) {
    throw std::runtime_error("failed to open session file for append");
  }
  out << "v1\tmsg\t" << role_to_string(message.role) << '\t' << escape(message.content) << '\n';
}

void SessionStore::ensure_session(const std::string& session_id, const std::string& active_model_id,
                                  const std::string& runtime_name) const {
  const std::string log_path = path_for(session_id);
  if (!std::filesystem::exists(log_path)) {
    std::ofstream out(log_path, std::ios::app);
    if (!out.is_open()) {
      throw std::runtime_error("failed to create session log: " + log_path);
    }
  }

  if (!std::filesystem::exists(metadata_path_for(session_id))) {
    update_metadata(session_id, active_model_id, runtime_name);
  }
}

void SessionStore::update_metadata(const std::string& session_id, const std::string& active_model_id,
                                   const std::string& runtime_name) const {
  long long created = 0;
  if (const std::optional<SessionMetadata> existing = load_metadata(session_id); existing.has_value()) {
    created = existing->created_at_epoch;
  }
  if (created == 0) {
    const auto now = std::chrono::system_clock::now();
    created = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
  }

  std::ofstream out(metadata_path_for(session_id), std::ios::trunc);
  if (!out.is_open()) {
    throw std::runtime_error("failed to open session metadata for write");
  }
  out << "session_id=" << session_id << "\n";
  out << "created_at_epoch=" << created << "\n";
  out << "active_model_id=" << active_model_id << "\n";
  out << "runtime_name=" << runtime_name << "\n";
}

std::optional<SessionMetadata> SessionStore::load_metadata(const std::string& session_id) const {
  std::ifstream in(metadata_path_for(session_id));
  if (!in.is_open()) {
    return std::nullopt;
  }

  SessionMetadata metadata;
  metadata.session_id = session_id;

  std::string line;
  while (std::getline(in, line)) {
    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    const std::string key = line.substr(0, eq);
    const std::string value = line.substr(eq + 1);
    if (key == "session_id") {
      metadata.session_id = value;
    } else if (key == "created_at_epoch") {
      metadata.created_at_epoch = std::stoll(value);
    } else if (key == "active_model_id") {
      metadata.active_model_id = value;
    } else if (key == "runtime_name") {
      metadata.runtime_name = value;
    }
  }

  return metadata;
}

std::vector<SessionMetadata> SessionStore::list_sessions() const {
  std::vector<SessionMetadata> out;
  for (const auto& entry : std::filesystem::directory_iterator(base_dir_)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::filesystem::path path = entry.path();
    if (path.extension() != ".log") {
      continue;
    }

    SessionMetadata metadata;
    metadata.session_id = path.stem().string();
    if (const std::optional<SessionMetadata> loaded = load_metadata(metadata.session_id); loaded.has_value()) {
      metadata = *loaded;
    } else {
      const auto now = std::chrono::system_clock::now();
      metadata.created_at_epoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    }
    out.push_back(std::move(metadata));
  }

  std::sort(out.begin(), out.end(), [](const SessionMetadata& a, const SessionMetadata& b) {
    if (a.created_at_epoch != b.created_at_epoch) {
      return a.created_at_epoch > b.created_at_epoch;
    }
    return a.session_id < b.session_id;
  });
  return out;
}

std::string SessionStore::path_for(const std::string& session_id) const {
  return base_dir_ + "/" + session_id + ".log";
}

std::string SessionStore::metadata_path_for(const std::string& session_id) const {
  return base_dir_ + "/" + session_id + ".meta";
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
