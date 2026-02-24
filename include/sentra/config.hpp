#pragma once

#include <string>

namespace sentra {

struct AppConfig {
  std::string m_runtimePreference{"llama-inproc"};
  std::string m_sessionsDir{".sentra/sessions"};
  std::string m_stateFile{".sentra/state.conf"};
  std::string m_modelsFile{"models.tsv"};
  std::string m_defaultModelId{"llama31_8b_q4km"};
  std::string m_systemPrompt{"You are Sentra, a local-first terminal AI assistant."};
  std::string m_localCommandTemplate{""};
  std::size_t m_maxTokens{256};
  std::size_t m_contextWindowTokens{2048};
  int m_llamaNThreads{0};
  int m_llamaNThreadsBatch{0};
  int m_llamaNBatch{512};
  bool m_llamaOffloadKqv{false};
  bool m_llamaOpOffload{false};
  std::string m_profile{"balanced"};

  static AppConfig load_from_file(const std::string& path);
};

}  // namespace sentra
