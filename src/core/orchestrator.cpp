#include "sentra/orchestrator.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "sentra/context_window.hpp"

namespace sentra {

Orchestrator::Orchestrator(AppConfig config, ModelRegistry model_registry, AppState app_state,
                           std::vector<std::unique_ptr<IModelRuntime>> runtimes)
    : config_(std::move(config)),
      model_registry_(std::move(model_registry)),
      app_state_(std::move(app_state)),
      runtimes_(std::move(runtimes)),
      active_runtime_index_(pick_runtime_index(runtime_selection_note_)) {}

std::string Orchestrator::active_runtime_name() const {
  if (!active_runtime_index_.has_value() || *active_runtime_index_ >= runtimes_.size()) {
    return "none";
  }
  return runtimes_[*active_runtime_index_]->name();
}

std::string Orchestrator::runtime_selection_note() const { return runtime_selection_note_; }

std::string Orchestrator::models_file_path() const { return config_.models_file; }

std::optional<std::reference_wrapper<const ModelSpec>> Orchestrator::active_model() const {
  return model_registry_.active_model();
}

const std::vector<ModelSpec>& Orchestrator::models() const { return model_registry_.models(); }

std::optional<std::reference_wrapper<const ModelSpec>> Orchestrator::find_model(
    const std::string& model_id) const {
  return model_registry_.find_model(model_id);
}

bool Orchestrator::add_model(const ModelSpec& model, std::string& error) {
  if (model.id.empty() || model.hf_repo.empty() || model.hf_file.empty() || model.local_path.empty()) {
    error = "model requires non-empty id, hf_repo, hf_file, and local_path";
    return false;
  }
  if (model_registry_.find_model(model.id).has_value()) {
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
  const auto model = model_registry_.active_model();
  if (!model.has_value()) {
    report = "no active model configured";
    return false;
  }
  const ModelSpec& active = model->get();
  if (active.id.empty() || active.hf_repo.empty() || active.hf_file.empty() || active.local_path.empty()) {
    report = "active model metadata is incomplete for id: " + active.id;
    return false;
  }
  if (!std::filesystem::exists(active.local_path)) {
    report = "model file not found at " + active.local_path + " (run /model download " + active.id + ")";
    return false;
  }
  std::ifstream in(active.local_path);
  if (!in.good()) {
    report = "model file exists but is not readable: " + active.local_path;
    return false;
  }
  report = "model valid: " + active.id + " @ " + active.local_path;
  return true;
}

std::size_t Orchestrator::max_tokens() const { return config_.max_tokens; }

std::size_t Orchestrator::context_window_tokens() const { return config_.context_window_tokens; }

void Orchestrator::set_max_tokens(std::size_t value) { config_.max_tokens = std::max<std::size_t>(1, value); }

void Orchestrator::set_context_window_tokens(std::size_t value) {
  config_.context_window_tokens = std::max<std::size_t>(64, value);
}

std::string Orchestrator::profile() const { return config_.profile; }

bool Orchestrator::set_profile(const std::string& profile, std::string& error) {
  const std::string normalized = [&]() {
    std::string out = profile;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
      if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c - 'A' + 'a');
      }
      return static_cast<char>(c);
    });
    return out;
  }();

  if (normalized == "fast") {
    config_.profile = normalized;
    config_.max_tokens = 128;
    config_.context_window_tokens = 1024;
    error.clear();
    return true;
  }
  if (normalized == "balanced") {
    config_.profile = normalized;
    config_.max_tokens = 256;
    config_.context_window_tokens = 2048;
    error.clear();
    return true;
  }
  if (normalized == "quality") {
    config_.profile = normalized;
    config_.max_tokens = 512;
    config_.context_window_tokens = 4096;
    error.clear();
    return true;
  }

  error = "unknown profile: " + profile + " (use fast|balanced|quality)";
  return false;
}

GenerationResult Orchestrator::respond(const std::vector<Message>& history, StreamCallback on_token) {
  if (!active_runtime_index_.has_value() || *active_runtime_index_ >= runtimes_.size()) {
    throw std::runtime_error("no available runtime");
  }

  const auto model = model_registry_.active_model();
  if (!model.has_value()) {
    throw std::runtime_error("no active model configured");
  }
  const ModelSpec& active = model->get();
  if (!std::filesystem::exists(active.local_path)) {
    throw std::runtime_error("active model path is missing: " + active.local_path +
                             " (run /model validate or /model download " + active.id + ")");
  }
  std::ifstream in(active.local_path);
  if (!in.good()) {
    throw std::runtime_error("active model path is not readable: " + active.local_path);
  }

  GenerationRequest req;
  const std::size_t prompt_budget =
      config_.context_window_tokens > config_.max_tokens ? config_.context_window_tokens - config_.max_tokens : 0;
  const ContextPruneResult pruned = prune_context_window(history, prompt_budget);
  req.messages = pruned.messages;
  req.model_id = active.id;
  req.model_path = active.local_path;
  req.max_tokens = config_.max_tokens;

  GenerationResult result = runtimes_[*active_runtime_index_]->generate(req, std::move(on_token));
  if (pruned.truncated) {
    result.context_truncated = true;
    result.warning = "context truncated to fit token budget (kept approx " +
                     std::to_string(pruned.tokens_kept) + " tokens)";
  }
  return result;
}

std::optional<std::size_t> Orchestrator::pick_runtime_index(std::string& note) const {
  if (runtimes_.empty()) {
    note = "no runtimes configured";
    return std::nullopt;
  }

  for (std::size_t i = 0; i < runtimes_.size(); ++i) {
    if (runtimes_[i]->name() == config_.runtime_preference && runtimes_[i]->is_available()) {
      note.clear();
      return i;
    }
  }

  for (std::size_t i = 0; i < runtimes_.size(); ++i) {
    if (runtimes_[i]->is_available()) {
      note = "runtime '" + config_.runtime_preference + "' unavailable; using '" + runtimes_[i]->name() + "'";
      return i;
    }
  }

  note = "no runtime is available";
  return std::nullopt;
}

}  // namespace sentra
