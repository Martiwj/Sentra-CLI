#include "sentra/orchestrator.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "sentra/context_window.hpp"

namespace sentra {

Orchestrator::Orchestrator(AppConfig config, ModelRegistry modelRegistry, AppState appState,
                           std::vector<std::unique_ptr<IModelRuntime>> runtimes)
    : m_config(std::move(config)),
      m_modelRegistry(std::move(modelRegistry)),
      m_appState(std::move(appState)),
      m_runtimes(std::move(runtimes)),
      m_activeRuntimeIndex(pick_runtime_index(m_runtimeSelectionNote)) {}

std::string Orchestrator::active_runtime_name() const {
  if (!m_activeRuntimeIndex.has_value() || *m_activeRuntimeIndex >= m_runtimes.size()) {
    return "none";
  }
  return m_runtimes[*m_activeRuntimeIndex]->name();
}

std::string Orchestrator::runtime_selection_note() const { return m_runtimeSelectionNote; }

std::string Orchestrator::models_file_path() const { return m_config.m_modelsFile; }

std::optional<std::reference_wrapper<const ModelSpec>> Orchestrator::active_model() const {
  return m_modelRegistry.active_model();
}

const std::vector<ModelSpec>& Orchestrator::models() const { return m_modelRegistry.models(); }

std::optional<std::reference_wrapper<const ModelSpec>> Orchestrator::find_model(
    const std::string& modelId) const {
  return m_modelRegistry.find_model(modelId);
}

bool Orchestrator::add_model(const ModelSpec& model, std::string& error) {
  if (model.m_id.empty() || model.m_hfRepo.empty() || model.m_hfFile.empty() || model.m_localPath.empty()) {
    error = "model requires non-empty id, hf_repo, hf_file, and local_path";
    return false;
  }
  if (m_modelRegistry.find_model(model.m_id).has_value()) {
    error = "model id already exists: " + model.m_id;
    return false;
  }

  std::ofstream out(m_config.m_modelsFile, std::ios::app);
  if (!out.is_open()) {
    error = "failed to append models file: " + m_config.m_modelsFile;
    return false;
  }
  const std::string displayName = model.m_name.empty() ? model.m_id : model.m_name;
  out << model.m_id << '\t' << displayName << '\t' << model.m_hfRepo << '\t' << model.m_hfFile << '\t'
      << model.m_localPath << '\n';
  if (!out.good()) {
    error = "failed writing model entry to: " + m_config.m_modelsFile;
    return false;
  }

  ModelSpec copy = model;
  copy.m_name = displayName;
  if (!m_modelRegistry.add_model(std::move(copy), error)) {
    return false;
  }
  error.clear();
  return true;
}

bool Orchestrator::set_active_model(const std::string& modelId, std::string& error) {
  const bool ok = m_modelRegistry.set_active_model(modelId, error);
  if (ok) {
    m_appState.save_active_model_id(modelId);
  }
  return ok;
}

bool Orchestrator::validate_active_model(std::string& report) const {
  const auto model = m_modelRegistry.active_model();
  if (!model.has_value()) {
    report = "no active model configured";
    return false;
  }
  const ModelSpec& active = model->get();
  if (active.m_id.empty() || active.m_hfRepo.empty() || active.m_hfFile.empty() || active.m_localPath.empty()) {
    report = "active model metadata is incomplete for id: " + active.m_id;
    return false;
  }
  if (!std::filesystem::exists(active.m_localPath)) {
    report = "model file not found at " + active.m_localPath + " (run /model download " + active.m_id + ")";
    return false;
  }
  std::ifstream in(active.m_localPath);
  if (!in.good()) {
    report = "model file exists but is not readable: " + active.m_localPath;
    return false;
  }
  report = "model valid: " + active.m_id + " @ " + active.m_localPath;
  return true;
}

std::size_t Orchestrator::max_tokens() const { return m_config.m_maxTokens; }

std::size_t Orchestrator::context_window_tokens() const { return m_config.m_contextWindowTokens; }

void Orchestrator::set_max_tokens(std::size_t value) { m_config.m_maxTokens = std::max<std::size_t>(1, value); }

void Orchestrator::set_context_window_tokens(std::size_t value) {
  m_config.m_contextWindowTokens = std::max<std::size_t>(64, value);
}

std::string Orchestrator::profile() const { return m_config.m_profile; }

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
    m_config.m_profile = normalized;
    m_config.m_maxTokens = 128;
    m_config.m_contextWindowTokens = 1024;
    error.clear();
    return true;
  }
  if (normalized == "balanced") {
    m_config.m_profile = normalized;
    m_config.m_maxTokens = 256;
    m_config.m_contextWindowTokens = 2048;
    error.clear();
    return true;
  }
  if (normalized == "quality") {
    m_config.m_profile = normalized;
    m_config.m_maxTokens = 512;
    m_config.m_contextWindowTokens = 4096;
    error.clear();
    return true;
  }

  error = "unknown profile: " + profile + " (use fast|balanced|quality)";
  return false;
}

GenerationResult Orchestrator::respond(const std::vector<Message>& history, StreamCallback on_token) {
  if (!m_activeRuntimeIndex.has_value() || *m_activeRuntimeIndex >= m_runtimes.size()) {
    throw std::runtime_error("no available runtime");
  }

  const auto model = m_modelRegistry.active_model();
  if (!model.has_value()) {
    throw std::runtime_error("no active model configured");
  }
  const ModelSpec& active = model->get();
  if (!std::filesystem::exists(active.m_localPath)) {
    throw std::runtime_error("active model path is missing: " + active.m_localPath +
                             " (run /model validate or /model download " + active.m_id + ")");
  }
  std::ifstream in(active.m_localPath);
  if (!in.good()) {
    throw std::runtime_error("active model path is not readable: " + active.m_localPath);
  }

  GenerationRequest req;
  const std::size_t promptBudget =
      m_config.m_contextWindowTokens > m_config.m_maxTokens ? m_config.m_contextWindowTokens - m_config.m_maxTokens : 0;
  const ContextPruneResult pruned = prune_context_window(history, promptBudget);
  req.m_messages = pruned.m_messages;
  req.m_modelId = active.m_id;
  req.m_modelPath = active.m_localPath;
  req.m_maxTokens = m_config.m_maxTokens;

  GenerationResult result = m_runtimes[*m_activeRuntimeIndex]->generate(req, std::move(on_token));
  if (pruned.m_truncated) {
    result.m_contextTruncated = true;
    result.m_warning = "context truncated to fit token budget (kept approx " +
                      std::to_string(pruned.m_tokensKept) + " tokens)";
  }
  return result;
}

std::optional<std::size_t> Orchestrator::pick_runtime_index(std::string& note) const {
  if (m_runtimes.empty()) {
    note = "no runtimes configured";
    return std::nullopt;
  }

  for (std::size_t i = 0; i < m_runtimes.size(); ++i) {
    if (m_runtimes[i]->name() == m_config.m_runtimePreference && m_runtimes[i]->is_available()) {
      note.clear();
      return i;
    }
  }

  for (std::size_t i = 0; i < m_runtimes.size(); ++i) {
    if (m_runtimes[i]->is_available()) {
      note = "runtime '" + m_config.m_runtimePreference + "' unavailable; using '" + m_runtimes[i]->name() + "'";
      return i;
    }
  }

  note = "no runtime is available";
  return std::nullopt;
}

}  // namespace sentra
