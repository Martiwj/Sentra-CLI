#pragma once

#include <string>

namespace sentra {

class AppState {
 public:
  explicit AppState(std::string state_path);

  std::string load_active_model_id() const;
  void save_active_model_id(const std::string& model_id) const;

 private:
  std::string state_path_;
};

}  // namespace sentra
