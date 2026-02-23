#include "sentra/model_registry.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

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

std::vector<std::string> split_tsv(const std::string& line) {
  std::vector<std::string> cols;
  std::string col;
  std::istringstream in(line);
  while (std::getline(in, col, '\t')) {
    cols.push_back(trim(col));
  }
  return cols;
}

}  // namespace

ModelRegistry ModelRegistry::load_from_tsv(const std::string& path, const std::string& preferred_model_id) {
  ModelRegistry registry;

  std::ifstream in(path);
  if (!in.is_open()) {
    throw std::runtime_error("failed to open models registry: " + path);
  }

  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }

    const std::vector<std::string> cols = split_tsv(line);
    if (cols.size() < 5) {
      continue;
    }

    ModelSpec model;
    model.id = cols[0];
    model.name = cols[1];
    model.hf_repo = cols[2];
    model.hf_file = cols[3];
    model.local_path = cols[4];

    if (!model.id.empty()) {
      registry.models_.push_back(std::move(model));
    }
  }

  if (registry.models_.empty()) {
    throw std::runtime_error("models registry is empty: " + path);
  }

  registry.active_index_ = 0;
  if (!preferred_model_id.empty()) {
    for (std::size_t i = 0; i < registry.models_.size(); ++i) {
      if (registry.models_[i].id == preferred_model_id) {
        registry.active_index_ = i;
        break;
      }
    }
  }

  return registry;
}

const std::vector<ModelSpec>& ModelRegistry::models() const { return models_; }

const ModelSpec* ModelRegistry::active_model() const {
  if (models_.empty() || active_index_ >= models_.size()) {
    return nullptr;
  }
  return &models_[active_index_];
}

bool ModelRegistry::set_active_model(const std::string& model_id, std::string& error) {
  for (std::size_t i = 0; i < models_.size(); ++i) {
    if (models_[i].id == model_id) {
      active_index_ = i;
      error.clear();
      return true;
    }
  }

  error = "unknown model id: " + model_id;
  return false;
}

}  // namespace sentra
