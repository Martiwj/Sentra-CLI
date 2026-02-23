#include "sentra/runtime.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace sentra {
namespace {

std::string shell_escape_single_quoted(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 2);
  out.push_back('\'');
  for (char c : value) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out.push_back(c);
    }
  }
  out.push_back('\'');
  return out;
}

std::string render_prompt(const GenerationRequest& request) {
  std::ostringstream prompt;
  for (const auto& message : request.messages) {
    prompt << role_to_string(message.role) << ": " << message.content << "\n";
  }
  prompt << "assistant: ";
  return prompt.str();
}

void replace_all(std::string& target, const std::string& needle, const std::string& replacement) {
  std::size_t pos = 0;
  while ((pos = target.find(needle, pos)) != std::string::npos) {
    target.replace(pos, needle.size(), replacement);
    pos += replacement.size();
  }
}

class LocalBinaryRuntime final : public IModelRuntime {
 public:
  explicit LocalBinaryRuntime(std::string command_template)
      : command_template_(std::move(command_template)) {}

  std::string name() const override { return "local-binary"; }

  bool is_available() const override {
    return command_template_.find("{prompt}") != std::string::npos;
  }

  GenerationResult generate(const GenerationRequest& request, StreamCallback on_token) override {
    if (!is_available()) {
      throw std::runtime_error("local-binary runtime unavailable: missing {prompt} placeholder");
    }
    if (request.model_path.empty()) {
      throw std::runtime_error("local-binary runtime requires a non-empty model_path");
    }

    std::string command = command_template_;
    replace_all(command, "{prompt}", shell_escape_single_quoted(render_prompt(request)));
    replace_all(command, "{model_path}", shell_escape_single_quoted(request.model_path));
    replace_all(command, "{max_tokens}", std::to_string(request.max_tokens));

    std::array<char, 256> buffer{};
    std::string output;

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
      throw std::runtime_error("failed to execute local runtime command");
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
      const std::string chunk(buffer.data());
      output += chunk;
      on_token(chunk);
    }

    pclose(pipe);
    return {.text = output};
  }

 private:
  std::string command_template_;
};

}  // namespace

std::shared_ptr<IModelRuntime> make_local_binary_runtime(const std::string& command_template) {
  return std::make_shared<LocalBinaryRuntime>(command_template);
}

}  // namespace sentra
