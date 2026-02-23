#pragma once

#include <functional>
#include <memory>
#include <string>

#include "sentra/types.hpp"

namespace sentra {

using StreamCallback = std::function<void(const std::string&)>;

struct LlamaRuntimeOptions {
  int n_threads{0};
  int n_threads_batch{0};
  int n_batch{512};
  bool offload_kqv{false};
  bool op_offload{false};
  std::string profile{"balanced"};
};

class IModelRuntime {
 public:
  virtual ~IModelRuntime() = default;
  virtual std::string name() const = 0;
  virtual bool is_available() const = 0;
  virtual GenerationResult generate(const GenerationRequest& request, StreamCallback on_token) = 0;
};

std::unique_ptr<IModelRuntime> make_mock_runtime();
std::unique_ptr<IModelRuntime> make_local_binary_runtime(const std::string& command_template);
std::unique_ptr<IModelRuntime> make_llama_inproc_runtime(const LlamaRuntimeOptions& options);

}  // namespace sentra
