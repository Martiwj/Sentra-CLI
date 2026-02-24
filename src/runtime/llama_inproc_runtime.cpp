#include "sentra/runtime.hpp"

#include <chrono>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(SENTRA_HAS_LLAMA_CPP)
#include <llama.h>
#endif

namespace sentra {
namespace {

std::string render_prompt(const GenerationRequest& request) {
  std::ostringstream prompt;
  for (const auto& message : request.m_messages) {
    prompt << role_to_string(message.m_role) << ": " << message.m_content << "\n";
  }
  prompt << "assistant: ";
  return prompt.str();
}

std::string normalize_profile(std::string profile) {
  for (char& c : profile) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  if (profile != "fast" && profile != "quality") {
    return "balanced";
  }
  return profile;
}

#if defined(SENTRA_HAS_LLAMA_CPP)
void llama_silent_log_callback(enum ggml_log_level, const char*, void*) {}

struct ModelDeleter {
  void operator()(llama_model* model) const {
    if (model != nullptr) {
      llama_model_free(model);
    }
  }
};

struct ContextDeleter {
  void operator()(llama_context* ctx) const {
    if (ctx != nullptr) {
      llama_free(ctx);
    }
  }
};

class LlamaInprocRuntime final : public IModelRuntime {
 public:
  explicit LlamaInprocRuntime(LlamaRuntimeOptions options) : m_options(std::move(options)) {
    m_options.m_profile = normalize_profile(m_options.m_profile);
  }

  std::string name() const override { return "llama-inproc"; }

  bool is_available() const override { return true; }

  GenerationResult generate(const GenerationRequest& request, StreamCallback on_token) override {
    if (request.m_modelPath.empty()) {
      throw std::runtime_error("llama-inproc requires a non-empty model_path");
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    ensure_backend_init();
    ensure_model_loaded(request.m_modelPath);
    ensure_context();

    const llama_vocab* vocab = llama_model_get_vocab(m_model.get());
    if (!vocab) {
      throw std::runtime_error("llama-inproc failed to get model vocab");
    }

    const std::string prompt = render_prompt(request);
    std::vector<llama_token> promptTokens = tokenize(vocab, prompt);
    if (promptTokens.empty()) {
      throw std::runtime_error("llama-inproc tokenization produced zero tokens");
    }

    const auto tStart = std::chrono::steady_clock::now();
    const std::size_t prefix = common_prefix(m_cachedPromptTokens, promptTokens);
    if (prefix != m_cachedPromptTokens.size()) {
      llama_memory_clear(llama_get_memory(m_context.get()), true);
      m_cachedPromptTokens.clear();
    }

    if (promptTokens.size() > m_cachedPromptTokens.size()) {
      std::vector<llama_token> suffix(
          promptTokens.begin() + static_cast<std::ptrdiff_t>(m_cachedPromptTokens.size()), promptTokens.end());
      llama_batch promptBatch = llama_batch_get_one(suffix.data(), static_cast<int32_t>(suffix.size()));
      const int promptRc = llama_decode(m_context.get(), promptBatch);
      if (promptRc != 0) {
        throw std::runtime_error("llama-inproc prompt decode failed: code " + std::to_string(promptRc));
      }
      m_cachedPromptTokens = promptTokens;
    }

    llama_sampler* sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
    if (!sampler) {
      throw std::runtime_error("llama-inproc failed to initialize sampler chain");
    }

    const int topK = m_options.m_profile == "fast" ? 20 : (m_options.m_profile == "quality" ? 60 : 40);
    const float topP = m_options.m_profile == "quality" ? 0.98f : 0.95f;
    const float temp = m_options.m_profile == "fast" ? 0.6f : (m_options.m_profile == "quality" ? 0.8f : 0.7f);
    llama_sampler_chain_add(sampler, llama_sampler_init_top_k(topK));
    llama_sampler_chain_add(sampler, llama_sampler_init_top_p(topP, 1));
    llama_sampler_chain_add(sampler, llama_sampler_init_temp(temp));
    llama_sampler_chain_add(sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    std::string output;
    output.reserve(request.m_maxTokens * 4);
    std::size_t generatedTokens = 0;
    bool firstTokenRecorded = false;
    double firstTokenMs = 0.0;

    for (std::size_t i = 0; i < request.m_maxTokens; ++i) {
      const llama_token token = llama_sampler_sample(sampler, m_context.get(), -1);
      if (token == LLAMA_TOKEN_NULL || llama_vocab_is_eog(vocab, token)) {
        break;
      }
      llama_sampler_accept(sampler, token);
      ++generatedTokens;

      const std::string piece = token_to_text(vocab, token);
      if (!piece.empty()) {
        if (!firstTokenRecorded) {
          firstTokenRecorded = true;
          const auto now = std::chrono::steady_clock::now();
          firstTokenMs =
              std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(now - tStart).count();
        }
        output += piece;
        on_token(piece);
      }

      llama_token next = token;
      llama_batch nextBatch = llama_batch_get_one(&next, 1);
      const int rc = llama_decode(m_context.get(), nextBatch);
      if (rc != 0) {
        llama_sampler_free(sampler);
        throw std::runtime_error("llama-inproc token decode failed: code " + std::to_string(rc));
      }
      m_cachedPromptTokens.push_back(token);
    }

    llama_sampler_free(sampler);
    const auto tEnd = std::chrono::steady_clock::now();
    const double totalMs =
        std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(tEnd - tStart).count();
    const double tokensPerSecond =
        totalMs > 0.0 ? (static_cast<double>(generatedTokens) * 1000.0 / totalMs) : 0.0;
    return {.m_text = output,
            .m_contextTruncated = false,
            .m_warning = "",
            .m_firstTokenMs = firstTokenMs,
            .m_totalMs = totalMs,
            .m_generatedTokens = generatedTokens,
            .m_tokensPerSecond = tokensPerSecond};
  }

 private:
  static void ensure_backend_init() {
    static std::once_flag once;
    std::call_once(once, []() {
      ggml_backend_load_all();
      llama_backend_init();
      llama_log_set(llama_silent_log_callback, nullptr);
    });
  }

  void ensure_model_loaded(const std::string& modelPath) {
    if (m_model && m_loadedModelPath == modelPath) {
      return;
    }

    llama_model_params modelParams = llama_model_default_params();
    modelParams.use_mmap = true;
    modelParams.use_mlock = false;
    modelParams.n_gpu_layers = 0;
    ggml_backend_dev_t cpuDevice = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_CPU);
    ggml_backend_dev_t devices[2] = {cpuDevice, nullptr};
    if (cpuDevice != nullptr) {
      modelParams.devices = devices;
    }

    std::unique_ptr<llama_model, ModelDeleter> nextModel(
        llama_model_load_from_file(modelPath.c_str(), modelParams));
    if (!nextModel) {
      throw std::runtime_error("llama-inproc failed to load model file: " + modelPath);
    }

    m_model = std::move(nextModel);
    m_loadedModelPath = modelPath;
    m_context.reset();
    m_cachedPromptTokens.clear();
  }

  void ensure_context() {
    if (m_context) {
      return;
    }

    llama_context_params ctxParams = llama_context_default_params();
    const uint32_t batch = static_cast<uint32_t>(m_options.m_nBatch > 0 ? m_options.m_nBatch : 512);
    ctxParams.n_ctx = 0;
    ctxParams.n_batch = batch;
    ctxParams.n_ubatch = batch;
    ctxParams.offload_kqv = m_options.m_offloadKqv;
    ctxParams.op_offload = m_options.m_opOffload;

    m_context = std::unique_ptr<llama_context, ContextDeleter>(llama_init_from_model(m_model.get(), ctxParams));
    if (!m_context) {
      throw std::runtime_error("llama-inproc failed to create context");
    }

    if (m_options.m_nThreads > 0 || m_options.m_nThreadsBatch > 0) {
      const int nt = m_options.m_nThreads > 0 ? m_options.m_nThreads : llama_n_threads(m_context.get());
      const int ntb =
          m_options.m_nThreadsBatch > 0 ? m_options.m_nThreadsBatch : llama_n_threads_batch(m_context.get());
      llama_set_n_threads(m_context.get(), nt, ntb);
    }
  }

  static std::vector<llama_token> tokenize(const llama_vocab* vocab, const std::string& text) {
    std::vector<llama_token> tokens(static_cast<std::size_t>(text.size()) + 16);
    int32_t n = llama_tokenize(vocab, text.c_str(), static_cast<int32_t>(text.size()), tokens.data(),
                               static_cast<int32_t>(tokens.size()), true, true);
    if (n < 0) {
      const int32_t required = -n;
      tokens.assign(static_cast<std::size_t>(required), 0);
      n = llama_tokenize(vocab, text.c_str(), static_cast<int32_t>(text.size()), tokens.data(), required, true, true);
    }
    if (n < 0) {
      throw std::runtime_error("llama-inproc tokenization failed");
    }
    tokens.resize(static_cast<std::size_t>(n));
    return tokens;
  }

  static std::string token_to_text(const llama_vocab* vocab, llama_token token) {
    std::vector<char> buffer(32);
    int32_t n = llama_token_to_piece(vocab, token, buffer.data(), static_cast<int32_t>(buffer.size()), 0, true);
    if (n < 0) {
      buffer.assign(static_cast<std::size_t>(-n), 0);
      n = llama_token_to_piece(vocab, token, buffer.data(), static_cast<int32_t>(buffer.size()), 0, true);
    }
    if (n <= 0) {
      return "";
    }
    return std::string(buffer.data(), static_cast<std::size_t>(n));
  }

  static std::size_t common_prefix(const std::vector<llama_token>& a, const std::vector<llama_token>& b) {
    const std::size_t n = std::min(a.size(), b.size());
    std::size_t i = 0;
    while (i < n && a[i] == b[i]) {
      ++i;
    }
    return i;
  }

  std::mutex m_mutex;
  LlamaRuntimeOptions m_options;
  std::unique_ptr<llama_model, ModelDeleter> m_model{nullptr};
  std::unique_ptr<llama_context, ContextDeleter> m_context{nullptr};
  std::string m_loadedModelPath;
  std::vector<llama_token> m_cachedPromptTokens;
};

#else
class LlamaInprocRuntime final : public IModelRuntime {
 public:
  explicit LlamaInprocRuntime(LlamaRuntimeOptions) {}
  std::string name() const override { return "llama-inproc"; }
  bool is_available() const override { return false; }
  GenerationResult generate(const GenerationRequest&, StreamCallback) override {
    throw std::runtime_error("llama-inproc runtime unavailable: Sentra was built without llama.cpp headers/libs");
  }
};
#endif

}  // namespace

std::unique_ptr<IModelRuntime> make_llama_inproc_runtime(const LlamaRuntimeOptions& options) {
  return std::make_unique<LlamaInprocRuntime>(options);
}

}  // namespace sentra
