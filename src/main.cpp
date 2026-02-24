#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "sentra/config.hpp"
#include "sentra/app_state.hpp"
#include "sentra/model_registry.hpp"
#include "sentra/orchestrator.hpp"
#include "sentra/repl.hpp"
#include "sentra/runtime.hpp"
#include "sentra/session_store.hpp"

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

AppConfig AppConfig::load_from_file(const std::string& path) {
  AppConfig config;

  std::ifstream in(path);
  if (!in.is_open()) {
    return config;
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

    if (key == "runtime_preference") {
      config.m_runtimePreference = value;
    } else if (key == "sessions_dir") {
      config.m_sessionsDir = value;
    } else if (key == "state_file") {
      config.m_stateFile = value;
    } else if (key == "models_file") {
      config.m_modelsFile = value;
    } else if (key == "default_model_id") {
      config.m_defaultModelId = value;
    } else if (key == "system_prompt") {
      config.m_systemPrompt = value;
    } else if (key == "local_command_template") {
      config.m_localCommandTemplate = value;
    } else if (key == "max_tokens") {
      config.m_maxTokens = static_cast<std::size_t>(std::stoul(value));
    } else if (key == "context_window_tokens") {
      config.m_contextWindowTokens = static_cast<std::size_t>(std::stoul(value));
    } else if (key == "llama_n_threads") {
      config.m_llamaNThreads = std::stoi(value);
    } else if (key == "llama_n_threads_batch") {
      config.m_llamaNThreadsBatch = std::stoi(value);
    } else if (key == "llama_n_batch") {
      config.m_llamaNBatch = std::stoi(value);
    } else if (key == "llama_offload_kqv") {
      config.m_llamaOffloadKqv = (value == "1" || value == "true" || value == "yes");
    } else if (key == "llama_op_offload") {
      config.m_llamaOpOffload = (value == "1" || value == "true" || value == "yes");
    } else if (key == "profile") {
      config.m_profile = value;
    }
  }

  return config;
}

}  // namespace sentra

int main(int argc, char** argv) {
  try {
    std::string configPath = "sentra.conf";
    std::string sessionId;

    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--config" && i + 1 < argc) {
        configPath = argv[++i];
      } else if (arg == "--session" && i + 1 < argc) {
        sessionId = argv[++i];
      }
    }

    sentra::AppConfig config = sentra::AppConfig::load_from_file(configPath);
    sentra::SessionStore sessionStore(config.m_sessionsDir);
    sentra::AppState appState(config.m_stateFile);
    const std::string persistedModelId = appState.load_active_model_id();
    const std::string preferredModelId =
        persistedModelId.empty() ? config.m_defaultModelId : persistedModelId;
    sentra::ModelRegistry modelRegistry =
        sentra::ModelRegistry::load_from_tsv(config.m_modelsFile, preferredModelId);

    if (sessionId.empty()) {
      sessionId = sessionStore.create_session_id();
    }

    std::vector<std::unique_ptr<sentra::IModelRuntime>> runtimes;
    sentra::LlamaRuntimeOptions llamaOptions;
    llamaOptions.m_nThreads = config.m_llamaNThreads;
    llamaOptions.m_nThreadsBatch = config.m_llamaNThreadsBatch;
    llamaOptions.m_nBatch = config.m_llamaNBatch;
    llamaOptions.m_offloadKqv = config.m_llamaOffloadKqv;
    llamaOptions.m_opOffload = config.m_llamaOpOffload;
    llamaOptions.m_profile = config.m_profile;
    runtimes.push_back(sentra::make_llama_inproc_runtime(llamaOptions));
    runtimes.push_back(sentra::make_local_binary_runtime(config.m_localCommandTemplate));
    runtimes.push_back(sentra::make_mock_runtime());

    sentra::Repl repl(
        sessionId, std::move(sessionStore),
        sentra::Orchestrator(config, std::move(modelRegistry), std::move(appState), std::move(runtimes)),
        config.m_systemPrompt);
    return repl.run();
  } catch (const std::exception& ex) {
    std::cerr << "fatal: " << ex.what() << "\n";
    return 1;
  }
}
