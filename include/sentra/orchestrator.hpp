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
  Orchestrator(AppConfig config, ModelRegistry model_registry, AppState app_state,
               std::vector<std::unique_ptr<IModelRuntime>> runtimes);

  std::string active_runtime_name() const;
  std::string runtime_selection_note() const;
  std::string models_file_path() const;
  std::optional<std::reference_wrapper<const ModelSpec>> active_model() const;
  const std::vector<ModelSpec>& models() const;
  std::optional<std::reference_wrapper<const ModelSpec>> find_model(const std::string& model_id) const;
  bool add_model(const ModelSpec& model, std::string& error);
  bool set_active_model(const std::string& model_id, std::string& error);
  bool validate_active_model(std::string& report) const;
  GenerationResult respond(const std::vector<Message>& history, StreamCallback on_token);

 private:
  AppConfig config_;
  ModelRegistry model_registry_;
  AppState app_state_;
  std::vector<std::unique_ptr<IModelRuntime>> runtimes_;
  std::optional<std::size_t> active_runtime_index_;
  std::string runtime_selection_note_;

  std::optional<std::size_t> pick_runtime_index(std::string& note) const;
};

}  // namespace sentra
