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
  for (const auto& message : request.messages) {
    prompt << role_to_string(message.role) << ": " << message.content << "\n";
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
  explicit LlamaInprocRuntime(LlamaRuntimeOptions options) : options_(std::move(options)) {
    options_.profile = normalize_profile(options_.profile);
  }

  std::string name() const override { return "llama-inproc"; }

  bool is_available() const override { return true; }

  GenerationResult generate(const GenerationRequest& request, StreamCallback on_token) override {
    if (request.model_path.empty()) {
      throw std::runtime_error("llama-inproc requires a non-empty model_path");
    }

    std::lock_guard<std::mutex> lock(mu_);
    ensure_backend_init();
    ensure_model_loaded(request.model_path);
    ensure_context();

    const llama_vocab* vocab = llama_model_get_vocab(model_.get());
    if (!vocab) {
      throw std::runtime_error("llama-inproc failed to get model vocab");
    }

    const std::string prompt = render_prompt(request);
    std::vector<llama_token> prompt_tokens = tokenize(vocab, prompt);
    if (prompt_tokens.empty()) {
      throw std::runtime_error("llama-inproc tokenization produced zero tokens");
    }

    const auto t_start = std::chrono::steady_clock::now();
    const std::size_t prefix = common_prefix(cached_prompt_tokens_, prompt_tokens);
    if (prefix != cached_prompt_tokens_.size()) {
      llama_memory_clear(llama_get_memory(ctx_.get()), true);
      cached_prompt_tokens_.clear();
    }

    if (prompt_tokens.size() > cached_prompt_tokens_.size()) {
      std::vector<llama_token> suffix(prompt_tokens.begin() + static_cast<std::ptrdiff_t>(cached_prompt_tokens_.size()),
                                      prompt_tokens.end());
      llama_batch prompt_batch = llama_batch_get_one(suffix.data(), static_cast<int32_t>(suffix.size()));
      const int prompt_rc = llama_decode(ctx_.get(), prompt_batch);
      if (prompt_rc != 0) {
        throw std::runtime_error("llama-inproc prompt decode failed: code " + std::to_string(prompt_rc));
      }
      cached_prompt_tokens_ = prompt_tokens;
    }

    llama_sampler* sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
    if (!sampler) {
      throw std::runtime_error("llama-inproc failed to initialize sampler chain");
    }

    const int top_k = options_.profile == "fast" ? 20 : (options_.profile == "quality" ? 60 : 40);
    const float top_p = options_.profile == "quality" ? 0.98f : 0.95f;
    const float temp = options_.profile == "fast" ? 0.6f : (options_.profile == "quality" ? 0.8f : 0.7f);
    llama_sampler_chain_add(sampler, llama_sampler_init_top_k(top_k));
    llama_sampler_chain_add(sampler, llama_sampler_init_top_p(top_p, 1));
    llama_sampler_chain_add(sampler, llama_sampler_init_temp(temp));
    llama_sampler_chain_add(sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    std::string output;
    output.reserve(request.max_tokens * 4);
    std::size_t generated_tokens = 0;
    bool first_token_recorded = false;
    double first_token_ms = 0.0;

    for (std::size_t i = 0; i < request.max_tokens; ++i) {
      const llama_token token = llama_sampler_sample(sampler, ctx_.get(), -1);
      if (token == LLAMA_TOKEN_NULL || llama_vocab_is_eog(vocab, token)) {
        break;
      }
      llama_sampler_accept(sampler, token);
      ++generated_tokens;

      const std::string piece = token_to_text(vocab, token);
      if (!piece.empty()) {
        if (!first_token_recorded) {
          first_token_recorded = true;
          const auto now = std::chrono::steady_clock::now();
          first_token_ms =
              std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(now - t_start).count();
        }
        output += piece;
        on_token(piece);
      }

      llama_token next = token;
      llama_batch next_batch = llama_batch_get_one(&next, 1);
      const int rc = llama_decode(ctx_.get(), next_batch);
      if (rc != 0) {
        llama_sampler_free(sampler);
        throw std::runtime_error("llama-inproc token decode failed: code " + std::to_string(rc));
      }
      cached_prompt_tokens_.push_back(token);
    }

    llama_sampler_free(sampler);
    const auto t_end = std::chrono::steady_clock::now();
    const double total_ms =
        std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(t_end - t_start).count();
    const double tps = total_ms > 0.0 ? (static_cast<double>(generated_tokens) * 1000.0 / total_ms) : 0.0;
    return {.text = output,
            .context_truncated = false,
            .warning = "",
            .first_token_ms = first_token_ms,
            .total_ms = total_ms,
            .generated_tokens = generated_tokens,
            .tokens_per_second = tps};
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

  void ensure_model_loaded(const std::string& model_path) {
    if (model_ && loaded_model_path_ == model_path) {
      return;
    }

    llama_model_params model_params = llama_model_default_params();
    model_params.use_mmap = true;
    model_params.use_mlock = false;
    model_params.n_gpu_layers = 0;
    ggml_backend_dev_t cpu_device = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_CPU);
    ggml_backend_dev_t devices[2] = {cpu_device, nullptr};
    if (cpu_device != nullptr) {
      model_params.devices = devices;
    }

    std::unique_ptr<llama_model, ModelDeleter> next_model(llama_model_load_from_file(model_path.c_str(), model_params));
    if (!next_model) {
      throw std::runtime_error("llama-inproc failed to load model file: " + model_path);
    }

    model_ = std::move(next_model);
    loaded_model_path_ = model_path;
    ctx_.reset();
    cached_prompt_tokens_.clear();
  }

  void ensure_context() {
    if (ctx_) {
      return;
    }

    llama_context_params ctx_params = llama_context_default_params();
    const uint32_t batch = static_cast<uint32_t>(options_.n_batch > 0 ? options_.n_batch : 512);
    ctx_params.n_ctx = 0;
    ctx_params.n_batch = batch;
    ctx_params.n_ubatch = batch;
    ctx_params.offload_kqv = options_.offload_kqv;
    ctx_params.op_offload = options_.op_offload;

    ctx_ = std::unique_ptr<llama_context, ContextDeleter>(llama_init_from_model(model_.get(), ctx_params));
    if (!ctx_) {
      throw std::runtime_error("llama-inproc failed to create context");
    }

    if (options_.n_threads > 0 || options_.n_threads_batch > 0) {
      const int nt = options_.n_threads > 0 ? options_.n_threads : llama_n_threads(ctx_.get());
      const int ntb =
          options_.n_threads_batch > 0 ? options_.n_threads_batch : llama_n_threads_batch(ctx_.get());
      llama_set_n_threads(ctx_.get(), nt, ntb);
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

  std::mutex mu_;
  LlamaRuntimeOptions options_;
  std::unique_ptr<llama_model, ModelDeleter> model_{nullptr};
  std::unique_ptr<llama_context, ContextDeleter> ctx_{nullptr};
  std::string loaded_model_path_;
  std::vector<llama_token> cached_prompt_tokens_;
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
