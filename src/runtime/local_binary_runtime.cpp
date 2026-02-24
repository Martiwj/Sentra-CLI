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
  for (const auto& message : request.m_messages) {
    prompt << role_to_string(message.m_role) << ": " << message.m_content << "\n";
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

std::string first_command_token(const std::string& commandTemplate) {
  const auto firstNonSpace = commandTemplate.find_first_not_of(" \t");
  if (firstNonSpace == std::string::npos) {
    return "";
  }
  const auto end = commandTemplate.find_first_of(" \t", firstNonSpace);
  return commandTemplate.substr(firstNonSpace, end - firstNonSpace);
}

bool executable_exists_on_path(const std::string& executable) {
  if (executable.empty()) {
    return false;
  }
  if (executable.find('/') != std::string::npos) {
    return access(executable.c_str(), X_OK) == 0;
  }

  const char* pathEnv = std::getenv("PATH");
  if (!pathEnv) {
    return false;
  }
  std::istringstream paths(pathEnv);
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
  explicit LocalBinaryRuntime(std::string commandTemplate)
      : m_commandTemplate(std::move(commandTemplate)) {}

  std::string name() const override { return "local-binary"; }

  bool is_available() const override {
    if (m_commandTemplate.empty()) {
      return false;
    }
    if (!has_token(m_commandTemplate, "{prompt}") || !has_token(m_commandTemplate, "{model_path}") ||
        !has_token(m_commandTemplate, "{max_tokens}")) {
      return false;
    }
    if (!has_balanced_braces(m_commandTemplate)) {
      return false;
    }
    const std::string executable = first_command_token(m_commandTemplate);
    return executable_exists_on_path(executable);
  }

GenerationResult generate(const GenerationRequest& request, StreamCallback on_token) override {
    if (m_commandTemplate.empty()) {
      throw std::runtime_error("local-binary runtime unavailable: empty command template");
    }
    if (!has_token(m_commandTemplate, "{prompt}") || !has_token(m_commandTemplate, "{model_path}") ||
        !has_token(m_commandTemplate, "{max_tokens}")) {
      throw std::runtime_error(
          "local-binary runtime unavailable: template requires {prompt}, {model_path}, and {max_tokens}");
    }
    if (!has_balanced_braces(m_commandTemplate)) {
      throw std::runtime_error("local-binary runtime unavailable: malformed template placeholders");
    }
    const std::string executable = first_command_token(m_commandTemplate);
    if (!executable_exists_on_path(executable)) {
      throw std::runtime_error("local-binary runtime unavailable: executable not found: " + executable);
    }
    if (request.m_modelPath.empty()) {
      throw std::runtime_error("local-binary runtime requires a non-empty model_path");
    }

    const auto tStart = std::chrono::steady_clock::now();
    std::string command = m_commandTemplate;
    replace_all(command, "{prompt}", shell_escape_single_quoted(render_prompt(request)));
    replace_all(command, "{model_path}", shell_escape_single_quoted(request.m_modelPath));
    replace_all(command, "{max_tokens}", std::to_string(request.m_maxTokens));

    std::string output;

    const std::filesystem::path outputPath =
        std::filesystem::temp_directory_path() /
        ("sentra-local-binary-" + std::to_string(static_cast<long long>(std::time(nullptr))) + ".log");
    command += " > " + shell_escape_single_quoted(outputPath.string()) + " 2>&1";
    const int exitCode = command_exit_code(std::system(command.c_str()));

    {
      std::ifstream in(outputPath);
      std::ostringstream collected;
      collected << in.rdbuf();
      output = collected.str();
    }
    std::filesystem::remove(outputPath);

    if (!output.empty()) {
      on_token(output);
    }
    if (exitCode != 0) {
      throw std::runtime_error("local-binary runtime failed with exit code " + std::to_string(exitCode) +
                               ": " + output);
    }
    const auto tEnd = std::chrono::steady_clock::now();
    const double totalMs =
        std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(tEnd - tStart).count();
    std::size_t approxTokens = 0;
    bool inWord = false;
    for (char c : output) {
      if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
        inWord = false;
      } else if (!inWord) {
        inWord = true;
        ++approxTokens;
      }
    }
    const double tokensPerSecond = totalMs > 0.0 ? (static_cast<double>(approxTokens) * 1000.0 / totalMs) : 0.0;
    return {.m_text = output,
            .m_contextTruncated = false,
            .m_warning = "",
            .m_firstTokenMs = totalMs,
            .m_totalMs = totalMs,
            .m_generatedTokens = approxTokens,
            .m_tokensPerSecond = tokensPerSecond};
  }

 private:
  std::string m_commandTemplate;
};

}  // namespace

std::unique_ptr<IModelRuntime> make_local_binary_runtime(const std::string& commandTemplate) {
  return std::make_unique<LocalBinaryRuntime>(commandTemplate);
}

}  // namespace sentra
