#include "sentra/orchestrator.hpp"

#include <stdexcept>

namespace sentra {

Orchestrator::Orchestrator(AppConfig config, std::vector<std::shared_ptr<IModelRuntime>> runtimes)
    : config_(std::move(config)), runtimes_(std::move(runtimes)), active_runtime_(pick_runtime()) {}

std::string Orchestrator::active_runtime_name() const {
  return active_runtime_ ? active_runtime_->name() : "none";
}

GenerationResult Orchestrator::respond(const std::vector<Message>& history, StreamCallback on_token) {
  if (!active_runtime_) {
    throw std::runtime_error("no available runtime");
  }

  GenerationRequest req;
  req.messages = history;
  req.max_tokens = config_.max_tokens;
  return active_runtime_->generate(req, std::move(on_token));
}

std::shared_ptr<IModelRuntime> Orchestrator::pick_runtime() const {
  if (runtimes_.empty()) {
    return nullptr;
  }

  for (const auto& runtime : runtimes_) {
    if (runtime->name() == config_.runtime_preference && runtime->is_available()) {
      return runtime;
    }
  }

  for (const auto& runtime : runtimes_) {
    if (runtime->is_available()) {
      return runtime;
    }
  }

  return nullptr;
}

}  // namespace sentra
