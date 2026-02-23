#include "sentra/app_state.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace sentra {
namespace {

std::string trim(const std::string& value) {
  const auto start = value.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return "";
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(start, end - start + 1);
}

}  // namespace

AppState::AppState(std::string state_path) : state_path_(std::move(state_path)) {}

std::string AppState::load_active_model_id() const {
  std::ifstream in(state_path_);
  if (!in.is_open()) {
    return "";
  }

  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    const std::string key = trim(line.substr(0, eq));
    const std::string value = trim(line.substr(eq + 1));
    if (key == "active_model_id") {
      return value;
    }
  }

  return "";
}

void AppState::save_active_model_id(const std::string& model_id) const {
  const std::filesystem::path path(state_path_);
  const std::filesystem::path parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }

  std::ofstream out(state_path_, std::ios::trunc);
  if (!out.is_open()) {
    return;
  }
  out << "active_model_id=" << model_id << "\n";
}

}  // namespace sentra
