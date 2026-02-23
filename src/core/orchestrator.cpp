#include "sentra/orchestrator.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "sentra/context_window.hpp"

namespace sentra {

Orchestrator::Orchestrator(AppConfig config, ModelRegistry model_registry, AppState app_state,
                           std::vector<std::shared_ptr<IModelRuntime>> runtimes)
    : config_(std::move(config)),
      model_registry_(std::move(model_registry)),
      app_state_(std::move(app_state)),
      runtimes_(std::move(runtimes)),
      active_runtime_(pick_runtime(runtime_selection_note_)) {}

std::string Orchestrator::active_runtime_name() const {
  return active_runtime_ ? active_runtime_->name() : "none";
}

std::string Orchestrator::runtime_selection_note() const { return runtime_selection_note_; }

std::string Orchestrator::models_file_path() const { return config_.models_file; }

const ModelSpec* Orchestrator::active_model() const { return model_registry_.active_model(); }

const std::vector<ModelSpec>& Orchestrator::models() const { return model_registry_.models(); }

const ModelSpec* Orchestrator::find_model(const std::string& model_id) const {
  return model_registry_.find_model(model_id);
}

bool Orchestrator::add_model(const ModelSpec& model, std::string& error) {
  if (model.id.empty() || model.hf_repo.empty() || model.hf_file.empty() || model.local_path.empty()) {
    error = "model requires non-empty id, hf_repo, hf_file, and local_path";
    return false;
  }
  if (model_registry_.find_model(model.id) != nullptr) {
    error = "model id already exists: " + model.id;
    return false;
  }

  std::ofstream out(config_.models_file, std::ios::app);
  if (!out.is_open()) {
    error = "failed to append models file: " + config_.models_file;
    return false;
  }
  const std::string display_name = model.name.empty() ? model.id : model.name;
  out << model.id << '\t' << display_name << '\t' << model.hf_repo << '\t' << model.hf_file << '\t'
      << model.local_path << '\n';
  if (!out.good()) {
    error = "failed writing model entry to: " + config_.models_file;
    return false;
  }

  ModelSpec copy = model;
  copy.name = display_name;
  if (!model_registry_.add_model(std::move(copy), error)) {
    return false;
  }
  error.clear();
  return true;
}

bool Orchestrator::set_active_model(const std::string& model_id, std::string& error) {
  const bool ok = model_registry_.set_active_model(model_id, error);
  if (ok) {
    app_state_.save_active_model_id(model_id);
  }
  return ok;
}

bool Orchestrator::validate_active_model(std::string& report) const {
  const ModelSpec* model = model_registry_.active_model();
  if (!model) {
    report = "no active model configured";
    return false;
  }
  if (model->id.empty() || model->hf_repo.empty() || model->hf_file.empty() || model->local_path.empty()) {
    report = "active model metadata is incomplete for id: " + model->id;
    return false;
  }
  if (!std::filesystem::exists(model->local_path)) {
    report = "model file not found at " + model->local_path + " (run /model download " + model->id + ")";
    return false;
  }
  std::ifstream in(model->local_path);
  if (!in.good()) {
    report = "model file exists but is not readable: " + model->local_path;
    return false;
  }
  report = "model valid: " + model->id + " @ " + model->local_path;
  return true;
}

GenerationResult Orchestrator::respond(const std::vector<Message>& history, StreamCallback on_token) {
  if (!active_runtime_) {
    throw std::runtime_error("no available runtime");
  }

  const ModelSpec* model = model_registry_.active_model();
  if (!model) {
    throw std::runtime_error("no active model configured");
  }
  if (!std::filesystem::exists(model->local_path)) {
    throw std::runtime_error("active model path is missing: " + model->local_path +
                             " (run /model validate or /model download " + model->id + ")");
  }
  std::ifstream in(model->local_path);
  if (!in.good()) {
    throw std::runtime_error("active model path is not readable: " + model->local_path);
  }

  GenerationRequest req;
  const std::size_t prompt_budget =
      config_.context_window_tokens > config_.max_tokens ? config_.context_window_tokens - config_.max_tokens : 0;
  const ContextPruneResult pruned = prune_context_window(history, prompt_budget);
  req.messages = pruned.messages;
  req.model_id = model->id;
  req.model_path = model->local_path;
  req.max_tokens = config_.max_tokens;

  GenerationResult result = active_runtime_->generate(req, std::move(on_token));
  if (pruned.truncated) {
    result.context_truncated = true;
    result.warning = "context truncated to fit token budget (kept approx " +
                     std::to_string(pruned.tokens_kept) + " tokens)";
  }
  return result;
}

std::shared_ptr<IModelRuntime> Orchestrator::pick_runtime(std::string& note) const {
  if (runtimes_.empty()) {
    note = "no runtimes configured";
    return nullptr;
  }

  for (const auto& runtime : runtimes_) {
    if (runtime->name() == config_.runtime_preference && runtime->is_available()) {
      note.clear();
      return runtime;
    }
  }

  for (const auto& runtime : runtimes_) {
    if (runtime->is_available()) {
      note = "runtime '" + config_.runtime_preference + "' unavailable; using '" + runtime->name() + "'";
      return runtime;
    }
  }

  note = "no runtime is available";
  return nullptr;
}

}  // namespace sentra
