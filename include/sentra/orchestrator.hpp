#pragma once

#include <memory>
#include <string>
#include <vector>

#include "sentra/config.hpp"
#include "sentra/model_registry.hpp"
#include "sentra/runtime.hpp"
#include "sentra/types.hpp"

namespace sentra {

class Orchestrator {
 public:
  Orchestrator(AppConfig config, ModelRegistry model_registry,
               std::vector<std::shared_ptr<IModelRuntime>> runtimes);

  std::string active_runtime_name() const;
  const ModelSpec* active_model() const;
  const std::vector<ModelSpec>& models() const;
  bool set_active_model(const std::string& model_id, std::string& error);
  GenerationResult respond(const std::vector<Message>& history, StreamCallback on_token);

 private:
  AppConfig config_;
  ModelRegistry model_registry_;
  std::vector<std::shared_ptr<IModelRuntime>> runtimes_;
  std::shared_ptr<IModelRuntime> active_runtime_;

  std::shared_ptr<IModelRuntime> pick_runtime() const;
};

}  // namespace sentra
