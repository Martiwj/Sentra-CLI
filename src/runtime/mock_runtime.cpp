#include "sentra/runtime.hpp"

#include <memory>
#include <sstream>

namespace sentra {
namespace {

class MockRuntime final : public IModelRuntime {
 public:
  std::string name() const override { return "mock"; }

  bool is_available() const override { return true; }

  GenerationResult generate(const GenerationRequest& request, StreamCallback on_token) override {
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

    return {.text = text};
  }
};

}  // namespace

std::shared_ptr<IModelRuntime> make_mock_runtime() {
  return std::make_shared<MockRuntime>();
}

}  // namespace sentra
