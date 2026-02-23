#pragma once

#include <string>

namespace sentra {

struct AppConfig {
  std::string runtime_preference{"llama-inproc"};
  std::string sessions_dir{".sentra/sessions"};
  std::string state_file{".sentra/state.conf"};
  std::string models_file{"models.tsv"};
  std::string default_model_id{"llama31_8b_q4km"};
  std::string system_prompt{"You are Sentra, a local-first terminal AI assistant."};
  std::string local_command_template{""};
  std::size_t max_tokens{256};
  std::size_t context_window_tokens{2048};

  static AppConfig load_from_file(const std::string& path);
};

}  // namespace sentra
