#pragma once

#include <memory>
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
  Orchestrator(AppConfig config, ModelRegistry model_registry, AppState app_state,
               std::vector<std::shared_ptr<IModelRuntime>> runtimes);

  std::string active_runtime_name() const;
  std::string runtime_selection_note() const;
  std::string models_file_path() const;
  const ModelSpec* active_model() const;
  const std::vector<ModelSpec>& models() const;
  const ModelSpec* find_model(const std::string& model_id) const;
  bool set_active_model(const std::string& model_id, std::string& error);
  bool validate_active_model(std::string& report) const;
  GenerationResult respond(const std::vector<Message>& history, StreamCallback on_token);

 private:
  AppConfig config_;
  ModelRegistry model_registry_;
  AppState app_state_;
  std::vector<std::shared_ptr<IModelRuntime>> runtimes_;
  std::shared_ptr<IModelRuntime> active_runtime_;
  std::string runtime_selection_note_;

  std::shared_ptr<IModelRuntime> pick_runtime(std::string& note) const;
};

}  // namespace sentra
