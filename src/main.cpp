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
      config.runtime_preference = value;
    } else if (key == "sessions_dir") {
      config.sessions_dir = value;
    } else if (key == "state_file") {
      config.state_file = value;
    } else if (key == "models_file") {
      config.models_file = value;
    } else if (key == "default_model_id") {
      config.default_model_id = value;
    } else if (key == "system_prompt") {
      config.system_prompt = value;
    } else if (key == "local_command_template") {
      config.local_command_template = value;
    } else if (key == "max_tokens") {
      config.max_tokens = static_cast<std::size_t>(std::stoul(value));
    } else if (key == "context_window_tokens") {
      config.context_window_tokens = static_cast<std::size_t>(std::stoul(value));
    } else if (key == "llama_n_threads") {
      config.llama_n_threads = std::stoi(value);
    } else if (key == "llama_n_threads_batch") {
      config.llama_n_threads_batch = std::stoi(value);
    } else if (key == "llama_n_batch") {
      config.llama_n_batch = std::stoi(value);
    } else if (key == "llama_offload_kqv") {
      config.llama_offload_kqv = (value == "1" || value == "true" || value == "yes");
    } else if (key == "llama_op_offload") {
      config.llama_op_offload = (value == "1" || value == "true" || value == "yes");
    } else if (key == "profile") {
      config.profile = value;
    }
  }

  return config;
}

}  // namespace sentra

int main(int argc, char** argv) {
  try {
    std::string config_path = "sentra.conf";
    std::string session_id;

    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--config" && i + 1 < argc) {
        config_path = argv[++i];
      } else if (arg == "--session" && i + 1 < argc) {
        session_id = argv[++i];
      }
    }

    sentra::AppConfig config = sentra::AppConfig::load_from_file(config_path);
    sentra::SessionStore session_store(config.sessions_dir);
    sentra::AppState app_state(config.state_file);
    const std::string persisted_model_id = app_state.load_active_model_id();
    const std::string preferred_model_id =
        persisted_model_id.empty() ? config.default_model_id : persisted_model_id;
    sentra::ModelRegistry model_registry =
        sentra::ModelRegistry::load_from_tsv(config.models_file, preferred_model_id);

    if (session_id.empty()) {
      session_id = session_store.create_session_id();
    }

    std::vector<std::unique_ptr<sentra::IModelRuntime>> runtimes;
    sentra::LlamaRuntimeOptions llama_options;
    llama_options.n_threads = config.llama_n_threads;
    llama_options.n_threads_batch = config.llama_n_threads_batch;
    llama_options.n_batch = config.llama_n_batch;
    llama_options.offload_kqv = config.llama_offload_kqv;
    llama_options.op_offload = config.llama_op_offload;
    llama_options.profile = config.profile;
    runtimes.push_back(sentra::make_llama_inproc_runtime(llama_options));
    runtimes.push_back(sentra::make_local_binary_runtime(config.local_command_template));
    runtimes.push_back(sentra::make_mock_runtime());

    sentra::Repl repl(
        session_id, std::move(session_store),
        sentra::Orchestrator(config, std::move(model_registry), std::move(app_state), std::move(runtimes)),
        config.system_prompt);
    return repl.run();
  } catch (const std::exception& ex) {
    std::cerr << "fatal: " << ex.what() << "\n";
    return 1;
  }
}
