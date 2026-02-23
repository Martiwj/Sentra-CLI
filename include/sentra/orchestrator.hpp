#pragma once

#include <memory>
#include <string>
#include <vector>

#include "sentra/config.hpp"
#include "sentra/runtime.hpp"
#include "sentra/types.hpp"

namespace sentra {

class Orchestrator {
 public:
  Orchestrator(AppConfig config, std::vector<std::shared_ptr<IModelRuntime>> runtimes);

  std::string active_runtime_name() const;
  GenerationResult respond(const std::vector<Message>& history, StreamCallback on_token);

 private:
  AppConfig config_;
  std::vector<std::shared_ptr<IModelRuntime>> runtimes_;
  std::shared_ptr<IModelRuntime> active_runtime_;

  std::shared_ptr<IModelRuntime> pick_runtime() const;
};

}  // namespace sentra
