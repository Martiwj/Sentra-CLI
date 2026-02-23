#include "sentra/runtime.hpp"

#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>

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

bool has_token(const std::string& text, const std::string& token) {
  return text.find(token) != std::string::npos;
}

bool has_balanced_braces(const std::string& text) {
  int depth = 0;
  for (char c : text) {
    if (c == '{') {
      ++depth;
    } else if (c == '}') {
      --depth;
      if (depth < 0) {
        return false;
      }
    }
  }
  return depth == 0;
}

std::string first_command_token(const std::string& command_template) {
  const auto first_non_space = command_template.find_first_not_of(" \t");
  if (first_non_space == std::string::npos) {
    return "";
  }
  const auto end = command_template.find_first_of(" \t", first_non_space);
  return command_template.substr(first_non_space, end - first_non_space);
}

bool executable_exists_on_path(const std::string& executable) {
  if (executable.empty()) {
    return false;
  }
  if (executable.find('/') != std::string::npos) {
    return access(executable.c_str(), X_OK) == 0;
  }

  const char* path_env = std::getenv("PATH");
  if (!path_env) {
    return false;
  }
  std::istringstream paths(path_env);
  std::string path;
  while (std::getline(paths, path, ':')) {
    const std::filesystem::path candidate = std::filesystem::path(path) / executable;
    if (access(candidate.c_str(), X_OK) == 0) {
      return true;
    }
  }
  return false;
}

int command_exit_code(int status) {
  if (status == -1) {
    return errno != 0 ? errno : 1;
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status)) {
    return 128 + WTERMSIG(status);
  }
  return status;
}

class LocalBinaryRuntime final : public IModelRuntime {
 public:
  explicit LocalBinaryRuntime(std::string command_template)
      : command_template_(std::move(command_template)) {}

  std::string name() const override { return "local-binary"; }

  bool is_available() const override {
    if (command_template_.empty()) {
      return false;
    }
    if (!has_token(command_template_, "{prompt}") || !has_token(command_template_, "{model_path}") ||
        !has_token(command_template_, "{max_tokens}")) {
      return false;
    }
    if (!has_balanced_braces(command_template_)) {
      return false;
    }
    const std::string executable = first_command_token(command_template_);
    return executable_exists_on_path(executable);
  }

GenerationResult generate(const GenerationRequest& request, StreamCallback on_token) override {
    if (command_template_.empty()) {
      throw std::runtime_error("local-binary runtime unavailable: empty command template");
    }
    if (!has_token(command_template_, "{prompt}") || !has_token(command_template_, "{model_path}") ||
        !has_token(command_template_, "{max_tokens}")) {
      throw std::runtime_error(
          "local-binary runtime unavailable: template requires {prompt}, {model_path}, and {max_tokens}");
    }
    if (!has_balanced_braces(command_template_)) {
      throw std::runtime_error("local-binary runtime unavailable: malformed template placeholders");
    }
    const std::string executable = first_command_token(command_template_);
    if (!executable_exists_on_path(executable)) {
      throw std::runtime_error("local-binary runtime unavailable: executable not found: " + executable);
    }
    if (request.model_path.empty()) {
      throw std::runtime_error("local-binary runtime requires a non-empty model_path");
    }

    const auto t_start = std::chrono::steady_clock::now();
    std::string command = command_template_;
    replace_all(command, "{prompt}", shell_escape_single_quoted(render_prompt(request)));
    replace_all(command, "{model_path}", shell_escape_single_quoted(request.model_path));
    replace_all(command, "{max_tokens}", std::to_string(request.max_tokens));

    std::string output;

    const std::filesystem::path output_path =
        std::filesystem::temp_directory_path() /
        ("sentra-local-binary-" + std::to_string(static_cast<long long>(std::time(nullptr))) + ".log");
    command += " > " + shell_escape_single_quoted(output_path.string()) + " 2>&1";
    const int exit_code = command_exit_code(std::system(command.c_str()));

    {
      std::ifstream in(output_path);
      std::ostringstream collected;
      collected << in.rdbuf();
      output = collected.str();
    }
    std::filesystem::remove(output_path);

    if (!output.empty()) {
      on_token(output);
    }
    if (exit_code != 0) {
      throw std::runtime_error("local-binary runtime failed with exit code " + std::to_string(exit_code) +
                               ": " + output);
    }
    const auto t_end = std::chrono::steady_clock::now();
    const double total_ms =
        std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(t_end - t_start).count();
    std::size_t approx_tokens = 0;
    bool in_word = false;
    for (char c : output) {
      if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
        in_word = false;
      } else if (!in_word) {
        in_word = true;
        ++approx_tokens;
      }
    }
    const double tps = total_ms > 0.0 ? (static_cast<double>(approx_tokens) * 1000.0 / total_ms) : 0.0;
    return {.text = output,
            .context_truncated = false,
            .warning = "",
            .first_token_ms = total_ms,
            .total_ms = total_ms,
            .generated_tokens = approx_tokens,
            .tokens_per_second = tps};
  }

 private:
  std::string command_template_;
};

}  // namespace

std::unique_ptr<IModelRuntime> make_local_binary_runtime(const std::string& command_template) {
  return std::make_unique<LocalBinaryRuntime>(command_template);
}

}  // namespace sentra
