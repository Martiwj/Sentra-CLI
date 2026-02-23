#include "sentra/orchestrator.hpp"

#include <stdexcept>

namespace sentra {

Orchestrator::Orchestrator(AppConfig config, ModelRegistry model_registry,
                           std::vector<std::shared_ptr<IModelRuntime>> runtimes)
    : config_(std::move(config)),
      model_registry_(std::move(model_registry)),
      runtimes_(std::move(runtimes)),
      active_runtime_(pick_runtime()) {}

std::string Orchestrator::active_runtime_name() const {
  return active_runtime_ ? active_runtime_->name() : "none";
}

const ModelSpec* Orchestrator::active_model() const { return model_registry_.active_model(); }

const std::vector<ModelSpec>& Orchestrator::models() const { return model_registry_.models(); }

bool Orchestrator::set_active_model(const std::string& model_id, std::string& error) {
  return model_registry_.set_active_model(model_id, error);
}

GenerationResult Orchestrator::respond(const std::vector<Message>& history, StreamCallback on_token) {
  if (!active_runtime_) {
    throw std::runtime_error("no available runtime");
  }

  const ModelSpec* model = model_registry_.active_model();
  if (!model) {
    throw std::runtime_error("no active model configured");
  }

  GenerationRequest req;
  req.messages = history;
  req.model_id = model->id;
  req.model_path = model->local_path;
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
