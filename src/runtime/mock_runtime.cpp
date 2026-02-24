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
    const auto tStart = std::chrono::steady_clock::now();
    std::string lastUser;
    for (auto it = request.m_messages.rbegin(); it != request.m_messages.rend(); ++it) {
      if (it->m_role == Role::User) {
        lastUser = it->m_content;
        break;
      }
    }

    std::ostringstream response;
    response << "[MOCK] Sentra received: " << lastUser
             << " | This is a local-first scaffold. Connect a real runtime via config.";

    const std::string text = response.str();
    for (char c : text) {
      on_token(std::string(1, c));
    }
    const auto tEnd = std::chrono::steady_clock::now();
    const double totalMs =
        std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(tEnd - tStart).count();
    return {.m_text = text,
            .m_contextTruncated = false,
            .m_warning = "",
            .m_firstTokenMs = 0.0,
            .m_totalMs = totalMs,
            .m_generatedTokens = text.empty() ? static_cast<std::size_t>(0) : static_cast<std::size_t>(1),
            .m_tokensPerSecond = totalMs > 0.0 ? (1000.0 / totalMs) : 0.0};
  }
};

}  // namespace

std::unique_ptr<IModelRuntime> make_mock_runtime() {
  return std::make_unique<MockRuntime>();
}

}  // namespace sentra
