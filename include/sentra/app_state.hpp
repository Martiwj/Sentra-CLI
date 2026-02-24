#pragma once

#include <string>

namespace sentra {

class AppState {
 public:
  explicit AppState(std::string statePath);

  std::string load_active_model_id() const;
  void save_active_model_id(const std::string& modelId) const;

 private:
  std::string m_statePath;
};

}  // namespace sentra
