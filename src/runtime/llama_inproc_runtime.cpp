#include "sentra/runtime.hpp"

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
  std::string name() const override { return "llama-inproc"; }

  bool is_available() const override { return true; }

  GenerationResult generate(const GenerationRequest& request, StreamCallback on_token) override {
    if (request.model_path.empty()) {
      throw std::runtime_error("llama-inproc requires a non-empty model_path");
    }

    std::lock_guard<std::mutex> lock(mu_);
    ensure_backend_init();
    ensure_model_loaded(request.model_path);

    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = 0;
    ctx_params.n_batch = 512;
    ctx_params.n_ubatch = 512;
    ctx_params.offload_kqv = false;
    ctx_params.op_offload = false;
    std::unique_ptr<llama_context, ContextDeleter> ctx(llama_init_from_model(model_.get(), ctx_params));
    if (!ctx) {
      throw std::runtime_error("llama-inproc failed to create context");
    }

    const llama_vocab* vocab = llama_model_get_vocab(model_.get());
    if (!vocab) {
      throw std::runtime_error("llama-inproc failed to get model vocab");
    }

    const std::string prompt = render_prompt(request);
    std::vector<llama_token> prompt_tokens = tokenize(vocab, prompt);
    if (prompt_tokens.empty()) {
      throw std::runtime_error("llama-inproc tokenization produced zero tokens");
    }

    llama_batch prompt_batch = llama_batch_get_one(prompt_tokens.data(), static_cast<int32_t>(prompt_tokens.size()));
    const int prompt_rc = llama_decode(ctx.get(), prompt_batch);
    if (prompt_rc != 0) {
      throw std::runtime_error("llama-inproc prompt decode failed: code " + std::to_string(prompt_rc));
    }

    llama_sampler* sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
    if (!sampler) {
      throw std::runtime_error("llama-inproc failed to initialize sampler chain");
    }
    llama_sampler_chain_add(sampler, llama_sampler_init_top_k(40));
    llama_sampler_chain_add(sampler, llama_sampler_init_top_p(0.95f, 1));
    llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.7f));
    llama_sampler_chain_add(sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    std::string output;
    output.reserve(request.max_tokens * 4);
    for (std::size_t i = 0; i < request.max_tokens; ++i) {
      const llama_token token = llama_sampler_sample(sampler, ctx.get(), -1);
      if (token == LLAMA_TOKEN_NULL || llama_vocab_is_eog(vocab, token)) {
        break;
      }

      llama_sampler_accept(sampler, token);

      std::string piece = token_to_text(vocab, token);
      if (!piece.empty()) {
        output += piece;
        on_token(piece);
      }

      llama_token next = token;
      llama_batch next_batch = llama_batch_get_one(&next, 1);
      const int rc = llama_decode(ctx.get(), next_batch);
      if (rc != 0) {
        llama_sampler_free(sampler);
        throw std::runtime_error("llama-inproc token decode failed: code " + std::to_string(rc));
      }
    }

    llama_sampler_free(sampler);
    return {.text = output};
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

  std::mutex mu_;
  std::unique_ptr<llama_model, ModelDeleter> model_{nullptr};
  std::string loaded_model_path_;
};

#else
class LlamaInprocRuntime final : public IModelRuntime {
 public:
  std::string name() const override { return "llama-inproc"; }
  bool is_available() const override { return false; }
  GenerationResult generate(const GenerationRequest&, StreamCallback) override {
    throw std::runtime_error("llama-inproc runtime unavailable: Sentra was built without llama.cpp headers/libs");
  }
};
#endif

}  // namespace

std::shared_ptr<IModelRuntime> make_llama_inproc_runtime() {
  return std::make_shared<LlamaInprocRuntime>();
}

}  // namespace sentra
