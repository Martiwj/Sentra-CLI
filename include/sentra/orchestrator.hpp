#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sentra/app_state.hpp"
#include "sentra/config.hpp"
#include "sentra/model_registry.hpp"
#include "sentra/runtime.hpp"
#include "sentra/types.hpp"

namespace sentra {

class Orchestrator {
 public:
  Orchestrator(AppConfig config, ModelRegistry modelRegistry, AppState appState,
               std::vector<std::unique_ptr<IModelRuntime>> runtimes);

  std::string active_runtime_name() const;
  std::string runtime_selection_note() const;
  std::string models_file_path() const;
  std::optional<std::reference_wrapper<const ModelSpec>> active_model() const;
  const std::vector<ModelSpec>& models() const;
  std::optional<std::reference_wrapper<const ModelSpec>> find_model(const std::string& modelId) const;
  bool add_model(const ModelSpec& model, std::string& error);
  bool set_active_model(const std::string& modelId, std::string& error);
  bool validate_active_model(std::string& report) const;
  std::size_t max_tokens() const;
  std::size_t context_window_tokens() const;
  void set_max_tokens(std::size_t value);
  void set_context_window_tokens(std::size_t value);
  std::string profile() const;
  bool set_profile(const std::string& profile, std::string& error);
  GenerationResult respond(const std::vector<Message>& history, StreamCallback on_token);

 private:
  AppConfig m_config;
  ModelRegistry m_modelRegistry;
  AppState m_appState;
  std::vector<std::unique_ptr<IModelRuntime>> m_runtimes;
  std::string m_runtimeSelectionNote;
  std::optional<std::size_t> m_activeRuntimeIndex;

  std::optional<std::size_t> pick_runtime_index(std::string& note) const;
};

}  // namespace sentra
