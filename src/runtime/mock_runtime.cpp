#include "sentra/runtime.hpp"

#include <chrono>
#include <memory>
#include <sstream>

namespace sentra {
namespace {

class MockRuntime final : public IModelRuntime {
 public:
  std::string name() const override { return "mock"; }

  bool is_available() const override { return true; }

  GenerationResult generate(const GenerationRequest& request, StreamCallback on_token) override {
    const auto t_start = std::chrono::steady_clock::now();
    std::string last_user;
    for (auto it = request.messages.rbegin(); it != request.messages.rend(); ++it) {
      if (it->role == Role::User) {
        last_user = it->content;
        break;
      }
    }

    std::ostringstream response;
    response << "[MOCK] Sentra received: " << last_user
             << " | This is a local-first scaffold. Connect a real runtime via config.";

    const std::string text = response.str();
    for (char c : text) {
      on_token(std::string(1, c));
    }
    const auto t_end = std::chrono::steady_clock::now();
    const double total_ms =
        std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(t_end - t_start).count();
    return {.text = text,
            .context_truncated = false,
            .warning = "",
            .first_token_ms = 0.0,
            .total_ms = total_ms,
            .generated_tokens = text.empty() ? static_cast<std::size_t>(0) : static_cast<std::size_t>(1),
            .tokens_per_second = total_ms > 0.0 ? (1000.0 / total_ms) : 0.0};
  }
};

}  // namespace

std::unique_ptr<IModelRuntime> make_mock_runtime() {
  return std::make_unique<MockRuntime>();
}

}  // namespace sentra
