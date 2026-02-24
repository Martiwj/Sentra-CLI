#include "sentra/repl.hpp"

#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <time.h>
#include <vector>
#include <cctype>
#include <sys/wait.h>

namespace sentra {
namespace {

void print_model_line(const ModelSpec& model, bool active) {
  const bool ready = std::filesystem::exists(model.m_localPath);
  std::cout << (active ? "* " : "  ") << model.m_id << " | " << model.m_name
            << " | ready=" << (ready ? "yes" : "no") << " | path=" << model.m_localPath << "\n";
}

void print_model_line(const ModelSpec& model, bool active, std::size_t index1Based) {
  const bool ready = std::filesystem::exists(model.m_localPath);
  std::cout << (active ? "* " : "  ") << "[" << index1Based << "] " << model.m_id << " | " << model.m_name
            << " | ready=" << (ready ? "yes" : "no") << " | path=" << model.m_localPath << "\n";
}

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

std::string format_epoch(long long epoch) {
  if (epoch <= 0) {
    return "unknown";
  }
  const std::time_t raw = static_cast<std::time_t>(epoch);
  std::tm tm{};
  localtime_r(&raw, &tm);
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  return out.str();
}

std::string trim(const std::string& value) {
  const auto start = value.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return "";
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(start, end - start + 1);
}

struct CodeBlock {
  std::string m_language;
  std::string m_content;
};

std::string to_lower(std::string value) {
  for (char& c : value) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return value;
}

bool is_shell_language(const std::string& language) {
  const std::string lang = to_lower(trim(language));
  return lang == "sh" || lang == "bash" || lang == "zsh" || lang == "shell" || lang == "console";
}

std::vector<CodeBlock> extract_fenced_code_blocks(const std::string& text) {
  std::vector<CodeBlock> out;
  std::size_t pos = 0;
  while (true) {
    const std::size_t fenceStart = text.find("```", pos);
    if (fenceStart == std::string::npos) {
      break;
    }
    const std::size_t langEnd = text.find('\n', fenceStart + 3);
    if (langEnd == std::string::npos) {
      break;
    }
    const std::string language = text.substr(fenceStart + 3, langEnd - (fenceStart + 3));
    const std::size_t fenceEnd = text.find("```", langEnd + 1);
    if (fenceEnd == std::string::npos) {
      break;
    }
    const std::string content = text.substr(langEnd + 1, fenceEnd - (langEnd + 1));
    out.push_back({trim(language), content});
    pos = fenceEnd + 3;
  }
  return out;
}

std::string colorize_generic_line(const std::string& line) {
  const char* reset = "\033[0m";
  const char* base = "\033[38;5;252m";
  const char* str = "\033[38;5;120m";
  const char* num = "\033[38;5;214m";
  const char* cmt = "\033[38;5;244m";

  std::string out;
  out.reserve(line.size() + 16);

  std::size_t commentPos = std::string::npos;
  bool inString = false;
  char quote = '\0';
  for (std::size_t i = 0; i + 1 < line.size(); ++i) {
    if (inString) {
      if (line[i] == '\\') {
        ++i;
      } else if (line[i] == quote) {
        inString = false;
      }
      continue;
    }
    if (line[i] == '"' || line[i] == '\'') {
      inString = true;
      quote = line[i];
      continue;
    }
    if (line[i] == '/' && line[i + 1] == '/') {
      commentPos = i;
      break;
    }
    if (line[i] == '-' && line[i + 1] == '-') {
      commentPos = i;
      break;
    }
  }
  if (commentPos == std::string::npos) {
    for (std::size_t i = 0; i < line.size(); ++i) {
      if (line[i] == '#') {
        commentPos = i;
        break;
      }
    }
  }

  const std::string code = commentPos == std::string::npos ? line : line.substr(0, commentPos);
  const std::string comment = commentPos == std::string::npos ? "" : line.substr(commentPos);

  out += base;
  for (std::size_t i = 0; i < code.size();) {
    const char ch = code[i];
    if (ch == '"' || ch == '\'') {
      const char q = ch;
      std::size_t j = i + 1;
      while (j < code.size()) {
        if (code[j] == '\\' && j + 1 < code.size()) {
          j += 2;
          continue;
        }
        if (code[j] == q) {
          ++j;
          break;
        }
        ++j;
      }
      out += str;
      out += code.substr(i, j - i);
      out += reset;
      i = j;
      continue;
    }

    if (std::isdigit(static_cast<unsigned char>(ch))) {
      std::size_t j = i + 1;
      while (j < code.size() &&
             (std::isdigit(static_cast<unsigned char>(code[j])) || code[j] == '.' || code[j] == '_' ||
              code[j] == 'x' || code[j] == 'X' || (code[j] >= 'a' && code[j] <= 'f') ||
              (code[j] >= 'A' && code[j] <= 'F'))) {
        ++j;
      }
      out += num;
      out += code.substr(i, j - i);
      out += reset;
      i = j;
      continue;
    }

    out.push_back(ch);
    ++i;
  }
  out += reset;

  if (!comment.empty()) {
    out += cmt;
    out += comment;
    out += reset;
  }
  return out;
}

std::string render_markdown_for_terminal(const std::string& text) {
  std::ostringstream out;
  std::istringstream in(text);
  std::string line;
  bool inCode = false;
  std::string codeLang;
  std::size_t codeLineNumber = 0;

  while (std::getline(in, line)) {
    if (line.rfind("```", 0) == 0) {
      if (!inCode) {
        inCode = true;
        codeLang = to_lower(trim(line.substr(3)));
        if (codeLang.empty()) {
          codeLang = "text";
        }
        codeLineNumber = 0;
        out << "\033[48;5;236;38;5;255m " << codeLang << " code \033[0m\n";
      } else {
        inCode = false;
        codeLang.clear();
        out << "\n";
      }
      continue;
    }

    if (inCode) {
      ++codeLineNumber;
      out << "\033[38;5;240m" << std::setw(4) << codeLineNumber << " |\033[0m ";
      out << colorize_generic_line(line);
      out << "\n";
    } else {
      out << line << "\n";
    }
  }

  return out.str();
}

std::optional<std::reference_wrapper<const Message>> last_assistant_message(
    const std::vector<Message>& history) {
  for (auto it = history.rbegin(); it != history.rend(); ++it) {
    if (it->m_role == Role::Assistant) {
      return std::cref(*it);
    }
  }
  return std::nullopt;
}

std::vector<CodeBlock> extract_code_blocks_from_history(const std::vector<Message>& history) {
  const auto assistant = last_assistant_message(history);
  if (!assistant.has_value()) {
    return {};
  }
  return extract_fenced_code_blocks(assistant->get().m_content);
}

std::vector<CodeBlock> extract_shell_blocks_from_history(const std::vector<Message>& history) {
  std::vector<CodeBlock> shellBlocks;
  const auto blocks = extract_code_blocks_from_history(history);
  for (const auto& block : blocks) {
    if (is_shell_language(block.m_language)) {
      shellBlocks.push_back(block);
    }
  }
  return shellBlocks;
}

int execute_shell_block(const std::string& scriptContent) {
  const std::filesystem::path tempPath =
      std::filesystem::temp_directory_path() /
      ("sentra-shell-" + std::to_string(static_cast<long long>(std::time(nullptr))) + ".sh");
  {
    std::ofstream out(tempPath);
    if (!out.is_open()) {
      std::cout << "error: failed to create temporary shell script\n";
      return 1;
    }
    out << "#!/usr/bin/env bash\nset -euo pipefail\n";
    out << scriptContent << "\n";
  }
  std::filesystem::permissions(
      tempPath, std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read,
      std::filesystem::perm_options::add);

  const std::string command = "/bin/bash " + shell_escape_single_quoted(tempPath.string());
  const int status = std::system(command.c_str());
  std::filesystem::remove(tempPath);
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  return 1;
}

bool try_copy_text_to_clipboard(const std::string& text, std::string& methodUsed) {
  const std::vector<std::pair<std::string, std::string>> copyCommands = {
      {"pbcopy", "pbcopy"},
      {"xclip", "xclip -selection clipboard"},
      {"xsel", "xsel --clipboard --input"},
  };

  for (const auto& entry : copyCommands) {
    const std::string detect = "command -v " + entry.first + " >/dev/null 2>&1";
    if (std::system(detect.c_str()) != 0) {
      continue;
    }
    const std::filesystem::path tempPath =
        std::filesystem::temp_directory_path() /
        ("sentra-clipboard-" + std::to_string(static_cast<long long>(std::time(nullptr))) + ".txt");
    {
      std::ofstream out(tempPath, std::ios::trunc);
      if (!out.is_open()) {
        continue;
      }
      out << text;
    }

    const std::string copyCmd =
        "cat " + shell_escape_single_quoted(tempPath.string()) + " | " + entry.second + " >/dev/null 2>&1";
    const int status = std::system(copyCmd.c_str());
    std::filesystem::remove(tempPath);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
      methodUsed = entry.first;
      return true;
    }
  }

  methodUsed.clear();
  return false;
}

std::vector<std::string> split_whitespace(const std::string& input) {
  std::istringstream in(input);
  std::vector<std::string> tokens;
  std::string token;
  while (in >> token) {
    tokens.push_back(token);
  }
  return tokens;
}

void print_main_menu() {
  std::cout << "Sentra Menu\n";
  std::cout << "  1. Show Status\n";
  std::cout << "  2. List Models\n";
  std::cout << "  3. Choose Active Model\n";
  std::cout << "  4. Download Model\n";
  std::cout << "  5. Validate Active Model\n";
  std::cout << "  6. Session Info\n";
  std::cout << "  7. List Sessions\n";
  std::cout << "  8. List Generated Code Blocks\n";
  std::cout << "  9. Copy Code Block\n";
  std::cout << "  10. Run Shell Code Block\n";
  std::cout << "  11. Help\n";
  std::cout << "  0. Exit\n";
  std::cout << "Use /menu run <number> to execute an action.\n\n";
}

std::optional<std::reference_wrapper<const ModelSpec>> resolve_model_selector(
    const Orchestrator& orchestrator, const std::string& selector) {
  const std::string value = trim(selector);
  if (value.empty()) {
    return std::nullopt;
  }

  bool allDigits = true;
  for (char c : value) {
    if (c < '0' || c > '9') {
      allDigits = false;
      break;
    }
  }

  if (allDigits) {
    const std::size_t index = static_cast<std::size_t>(std::stoul(value));
    if (index == 0 || index > orchestrator.models().size()) {
      return std::nullopt;
    }
    return std::cref(orchestrator.models()[index - 1]);
  }

  return orchestrator.find_model(value);
}

bool is_positive_integer(const std::string& text) {
  const std::string value = trim(text);
  if (value.empty()) {
    return false;
  }
  for (char c : value) {
    if (c < '0' || c > '9') {
      return false;
    }
  }
  return true;
}

void print_status_line_items(const Orchestrator& orchestrator, const std::string& sessionId, bool rawStreamMode) {
  std::cout << "session: " << sessionId << "\n";
  std::cout << "runtime: " << orchestrator.active_runtime_name() << "\n";
  if (const auto model = orchestrator.active_model(); model.has_value()) {
    std::cout << "model: " << model->get().m_id << "\n";
  } else {
    std::cout << "model: none\n";
  }
  std::cout << "profile: " << orchestrator.profile() << "\n";
  std::cout << "max_tokens: " << orchestrator.max_tokens() << "\n";
  std::cout << "context_window_tokens: " << orchestrator.context_window_tokens() << "\n";
  std::cout << "stream_mode: " << (rawStreamMode ? "raw" : "render") << "\n";
  if (!orchestrator.runtime_selection_note().empty()) {
    std::cout << "note: " << orchestrator.runtime_selection_note() << "\n";
  }
  std::cout << "\n";
}

std::string shorten_for_prompt(const std::string& value, std::size_t maxLen) {
  if (value.size() <= maxLen) {
    return value;
  }
  if (maxLen < 4) {
    return value.substr(0, maxLen);
  }
  return value.substr(0, maxLen - 3) + "...";
}

std::string make_user_prompt(const Orchestrator& orchestrator, bool menuMode) {
  if (menuMode) {
    return "\033[1;38;5;39mmenu>\033[0m ";
  }
  const std::string runtime = shorten_for_prompt(orchestrator.active_runtime_name(), 10);
  std::string model = "none";
  if (const auto active = orchestrator.active_model(); active.has_value()) {
    model = shorten_for_prompt(active->get().m_id, 14);
  }
  return "\033[1;38;5;75msentra\033[0m[\033[38;5;245m" + runtime + "|" + model + "\033[0m]> ";
}

std::string normalize_user_shortcut(const std::string& input) {
  const std::string raw = trim(input);
  if (raw.empty()) {
    return raw;
  }
  const std::string lower = to_lower(raw);

  if (lower == "help" || lower == "h" || lower == "?") {
    return "/help";
  }
  if (lower == "menu" || lower == "m") {
    return "/menu";
  }
  if (lower == "status" || lower == "s") {
    return "/status";
  }
  if (lower == "clear" || lower == "cls") {
    return "/clear";
  }
  if (lower == "quit" || lower == "exit" || lower == "q") {
    return "/exit";
  }
  if (lower == "models") {
    return "/model list";
  }
  if (lower == "current model") {
    return "/model current";
  }
  if (lower.rfind("use ", 0) == 0) {
    return "/model use " + trim(raw.substr(4));
  }
  if (lower.rfind("download ", 0) == 0) {
    return "/model download " + trim(raw.substr(9));
  }
  if (lower.rfind("remove ", 0) == 0) {
    return "/model remove " + trim(raw.substr(7));
  }
  return raw;
}

}  // namespace

Repl::Repl(std::string sessionId, SessionStore&& sessionStore, Orchestrator&& orchestrator,
           std::string systemPrompt)
    : m_sessionId(std::move(sessionId)),
      m_sessionStore(std::move(sessionStore)),
      m_orchestrator(std::move(orchestrator)),
      m_systemPrompt(std::move(systemPrompt)) {}

int Repl::run() {
  std::vector<Message> history = m_sessionStore.load(m_sessionId);
  const auto startupModel = m_orchestrator.active_model();
  const std::string startupModelId = startupModel.has_value() ? startupModel->get().m_id : "";
  m_sessionStore.ensure_session(m_sessionId, startupModelId, m_orchestrator.active_runtime_name());

  if (history.empty()) {
    const Message systemMsg{Role::System, m_systemPrompt};
    history.push_back(systemMsg);
    m_sessionStore.append(m_sessionId, systemMsg);
  }

  std::cout << "Sentra CLI MVP\n";
  std::cout << "session: " << m_sessionId << "\n";
  std::cout << "runtime: " << m_orchestrator.active_runtime_name() << "\n";
  if (!m_orchestrator.runtime_selection_note().empty()) {
    std::cout << "note: " << m_orchestrator.runtime_selection_note() << "\n";
  }
  if (const auto model = m_orchestrator.active_model(); model.has_value()) {
    std::cout << "model: " << model->get().m_id << "\n";
  }
  std::cout << "type /help for commands\n\n";

  std::string line;
  bool menuShortcutMode = false;
  bool rawStreamMode = (m_orchestrator.profile() == "fast");
  while (true) {
    std::cout << make_user_prompt(m_orchestrator, menuShortcutMode);
    if (!std::getline(std::cin, line)) {
      std::cout << "\n";
      break;
    }

    if (menuShortcutMode) {
      const std::string shortcut = trim(line);
      if (shortcut == "q" || shortcut == "quit" || shortcut == "exit") {
        line = "/menu run 0";
      } else if (is_positive_integer(shortcut)) {
        line = "/menu run " + shortcut;
      } else if (!shortcut.empty() && shortcut[0] == '/') {
        menuShortcutMode = false;
      } else if (shortcut.empty()) {
        continue;
      } else {
        std::cout << "enter a menu number, or use a slash command to leave menu mode\n\n";
        continue;
      }
    }

    if (!menuShortcutMode && !line.empty() && line[0] != '/') {
      line = normalize_user_shortcut(line);
    }

    if (line == "/exit" || line == "/quit") {
      break;
    }

    if (line == "/status") {
      print_status_line_items(m_orchestrator, m_sessionId, rawStreamMode);
      continue;
    }

    if (line == "/clear") {
      std::cout << "\033[2J\033[H";
      continue;
    }

    if (line == "/help") {
      std::cout << "/help                 Show commands\n";
      std::cout << "/status               Show current session/runtime/model\n";
      std::cout << "/clear                Clear terminal\n";
      std::cout << "/profile <mode>       Set profile: fast|balanced|quality\n";
      std::cout << "/set max_tokens <n>   Set max output tokens\n";
      std::cout << "/set context <n>      Set context window tokens\n";
      std::cout << "/set stream <mode>    Set stream mode: raw|render\n";
      std::cout << "/menu                 Show numbered menu\n";
      std::cout << "/menu run <n>         Run menu action by number\n";
      std::cout << "/exit                 Exit Sentra\n";
      std::cout << "/session              Print session id\n";
      std::cout << "/session info         Print current session metadata\n";
      std::cout << "/session list         List known sessions\n";
      std::cout << "/code list            List latest assistant code blocks\n";
      std::cout << "/code copy [n]        Copy code block n to clipboard (default first)\n";
      std::cout << "/code shell           Show latest shell code blocks from assistant\n";
      std::cout << "/code shell run [n]   Execute shell block n (default first) with confirmation\n";
      std::cout << "/model list           List configured models\n";
      std::cout << "/model current        Print active model\n";
      std::cout << "/model use <id|num>   Switch active model by ID or list number\n\n";
      std::cout << "/model add <id> <hf-repo> <hf-file> [local-path]\n";
      std::cout << "/model download <id|num> Download configured model preset\n";
      std::cout << "/model validate       Validate active model path and metadata\n";
      std::cout << "/model remove <id|num> Remove local model file with confirmation\n\n";
      continue;
    }

    if (line == "/menu") {
      print_main_menu();
      menuShortcutMode = true;
      continue;
    }

    if (line.rfind("/menu run ", 0) == 0) {
      const std::string selector = trim(line.substr(std::string("/menu run ").size()));
      std::size_t action = 0;
      try {
        action = static_cast<std::size_t>(std::stoul(selector));
      } catch (...) {
        std::cout << "error: invalid menu number: " << selector << "\n\n";
        continue;
      }

      if (action == 0) {
        break;
      }
      if (action == 1) {
        print_status_line_items(m_orchestrator, m_sessionId, rawStreamMode);
        continue;
      }
      if (action == 2) {
        const auto active = m_orchestrator.active_model();
        std::size_t idx = 1;
        for (const auto& model : m_orchestrator.models()) {
          const bool isActive = active.has_value() && model.m_id == active->get().m_id;
          print_model_line(model, isActive, idx);
          ++idx;
        }
        std::cout << "\n";
        continue;
      }
      if (action == 3) {
        std::cout << "model id or number: ";
        std::string modelSelector;
        std::getline(std::cin, modelSelector);
        const auto selected = resolve_model_selector(m_orchestrator, modelSelector);
        if (!selected.has_value()) {
          std::cout << "error: unknown model selector: " << modelSelector << "\n\n";
          continue;
        }
        std::string error;
        if (!m_orchestrator.set_active_model(selected->get().m_id, error)) {
          std::cout << "error: " << error << "\n\n";
        } else {
          m_sessionStore.update_metadata(m_sessionId, selected->get().m_id, m_orchestrator.active_runtime_name());
          std::cout << "active model: " << selected->get().m_id << "\n\n";
        }
        continue;
      }
      if (action == 4) {
        std::cout << "model id or number to download: ";
        std::string modelSelector;
        std::getline(std::cin, modelSelector);
        const auto selected = resolve_model_selector(m_orchestrator, modelSelector);
        if (!selected.has_value()) {
          std::cout << "error: unknown model selector: " << modelSelector << "\n\n";
          continue;
        }
        const std::string command =
            "./scripts/download_model.sh " + shell_escape_single_quoted(selected->get().m_id) + " " +
            shell_escape_single_quoted(m_orchestrator.models_file_path());
        const int code = std::system(command.c_str());
        if (code != 0) {
          std::cout << "download failed with exit code: " << code << "\n\n";
        } else {
          std::cout << "download complete for model: " << selected->get().m_id << "\n\n";
        }
        continue;
      }
      if (action == 5) {
        std::string report;
        if (m_orchestrator.validate_active_model(report)) {
          std::cout << report << "\n\n";
        } else {
          std::cout << "validation failed: " << report << "\n\n";
        }
        continue;
      }
      if (action == 6) {
        const auto metadata = m_sessionStore.load_metadata(m_sessionId);
        if (!metadata.has_value()) {
          std::cout << "session metadata not found\n\n";
        } else {
          std::cout << "session_id: " << metadata->m_sessionId << "\n";
          std::cout << "created_at: " << format_epoch(metadata->m_createdAtEpoch) << "\n";
          std::cout << "active_model_id: " << metadata->m_activeModelId << "\n";
          std::cout << "runtime_name: " << metadata->m_runtimeName << "\n\n";
        }
        continue;
      }
      if (action == 7) {
        const auto sessions = m_sessionStore.list_sessions();
        if (sessions.empty()) {
          std::cout << "no sessions found\n\n";
        } else {
          for (const auto& session : sessions) {
            std::cout << session.m_sessionId << " | created=" << format_epoch(session.m_createdAtEpoch)
                      << " | model=" << session.m_activeModelId << " | runtime=" << session.m_runtimeName << "\n";
          }
          std::cout << "\n";
        }
        continue;
      }
      if (action == 8) {
        const auto blocks = extract_code_blocks_from_history(history);
        if (blocks.empty()) {
          std::cout << "no code block found in latest assistant reply\n\n";
        } else {
          for (std::size_t i = 0; i < blocks.size(); ++i) {
            std::string lang = trim(blocks[i].m_language);
            if (lang.empty()) {
              lang = "text";
            }
            std::cout << "[" << (i + 1) << "] lang=" << lang << " bytes=" << blocks[i].m_content.size() << "\n";
          }
          std::cout << "\n";
        }
        continue;
      }
      if (action == 9) {
        std::cout << "code block number (default 1): ";
        std::string codeSelector;
        std::getline(std::cin, codeSelector);
        const auto blocks = extract_code_blocks_from_history(history);
        if (blocks.empty()) {
          std::cout << "no code block found in latest assistant reply\n\n";
          continue;
        }
        std::size_t index = 1;
        if (!trim(codeSelector).empty()) {
          try {
            index = static_cast<std::size_t>(std::stoul(trim(codeSelector)));
          } catch (...) {
            std::cout << "error: invalid code block index\n\n";
            continue;
          }
        }
        if (index == 0 || index > blocks.size()) {
          std::cout << "error: code block index out of range (1.." << blocks.size() << ")\n\n";
          continue;
        }
        std::string method;
        if (try_copy_text_to_clipboard(blocks[index - 1].m_content, method)) {
          std::cout << "copied code block [" << index << "] to clipboard via " << method << "\n\n";
        } else {
          std::cout << "clipboard tool not found (install pbcopy/xclip/xsel)\n\n";
        }
        continue;
      }
      if (action == 10) {
        std::cout << "shell code block number (default 1): ";
        std::string shellSelector;
        std::getline(std::cin, shellSelector);
        const auto blocks = extract_shell_blocks_from_history(history);
        if (blocks.empty()) {
          std::cout << "no shell code block found in latest assistant reply\n\n";
          continue;
        }
        std::size_t index = 1;
        if (!trim(shellSelector).empty()) {
          try {
            index = static_cast<std::size_t>(std::stoul(trim(shellSelector)));
          } catch (...) {
            std::cout << "error: invalid shell block index\n\n";
            continue;
          }
        }
        if (index == 0 || index > blocks.size()) {
          std::cout << "error: shell block index out of range (1.." << blocks.size() << ")\n\n";
          continue;
        }
        std::cout << "about to execute shell block [" << index << "]:\n";
        std::cout << blocks[index - 1].m_content << "\n";
        std::cout << "type RUN to confirm: ";
        std::string confirmation;
        std::getline(std::cin, confirmation);
        if (confirmation != "RUN") {
          std::cout << "execution cancelled\n\n";
          continue;
        }
        const int exitCode = execute_shell_block(blocks[index - 1].m_content);
        std::cout << "\ncommand exit code: " << exitCode << "\n\n";
        continue;
      }
      if (action == 11) {
        std::cout << "/help                 Show commands\n";
        std::cout << "/status               Show current session/runtime/model\n";
        std::cout << "/clear                Clear terminal\n";
        std::cout << "/profile <mode>       Set profile: fast|balanced|quality\n";
        std::cout << "/set max_tokens <n>   Set max output tokens\n";
        std::cout << "/set context <n>      Set context window tokens\n";
        std::cout << "/set stream <mode>    Set stream mode: raw|render\n";
        std::cout << "/menu                 Show numbered menu\n";
        std::cout << "/menu run <n>         Run menu action by number\n";
        std::cout << "/exit                 Exit Sentra\n";
        std::cout << "/session              Print session id\n";
        std::cout << "/session info         Print current session metadata\n";
        std::cout << "/session list         List known sessions\n";
        std::cout << "/code list            List latest assistant code blocks\n";
        std::cout << "/code copy [n]        Copy code block n to clipboard (default first)\n";
        std::cout << "/code shell           Show latest shell code blocks from assistant\n";
        std::cout << "/code shell run [n]   Execute shell block n (default first) with confirmation\n";
        std::cout << "/model list           List configured models\n";
        std::cout << "/model current        Print active model\n";
        std::cout << "/model use <id|num>   Switch active model by ID or list number\n\n";
        std::cout << "/model add <id> <hf-repo> <hf-file> [local-path]\n";
        std::cout << "/model download <id|num> Download configured model preset\n";
        std::cout << "/model validate       Validate active model path and metadata\n";
        std::cout << "/model remove <id|num> Remove local model file with confirmation\n\n";
        continue;
      }
      std::cout << "error: unknown menu action: " << action << "\n\n";
      continue;
    }

    if (line.rfind("/profile ", 0) == 0) {
      const std::string mode = trim(line.substr(std::string("/profile ").size()));
      std::string error;
      if (!m_orchestrator.set_profile(mode, error)) {
        std::cout << "error: " << error << "\n\n";
        continue;
      }
      rawStreamMode = (m_orchestrator.profile() == "fast");
      std::cout << "profile set: " << m_orchestrator.profile() << "\n";
      std::cout << "max_tokens: " << m_orchestrator.max_tokens() << ", context_window_tokens: "
                << m_orchestrator.context_window_tokens() << ", stream_mode: "
                << (rawStreamMode ? "raw" : "render") << "\n\n";
      continue;
    }

    if (line.rfind("/set max_tokens ", 0) == 0) {
      const std::string value = trim(line.substr(std::string("/set max_tokens ").size()));
      try {
        const std::size_t n = static_cast<std::size_t>(std::stoull(value));
        m_orchestrator.set_max_tokens(n);
        std::cout << "max_tokens set to " << m_orchestrator.max_tokens() << "\n\n";
      } catch (...) {
        std::cout << "error: invalid max_tokens value: " << value << "\n\n";
      }
      continue;
    }

    if (line.rfind("/set context ", 0) == 0) {
      const std::string value = trim(line.substr(std::string("/set context ").size()));
      try {
        const std::size_t n = static_cast<std::size_t>(std::stoull(value));
        m_orchestrator.set_context_window_tokens(n);
        std::cout << "context_window_tokens set to " << m_orchestrator.context_window_tokens() << "\n\n";
      } catch (...) {
        std::cout << "error: invalid context token value: " << value << "\n\n";
      }
      continue;
    }

    if (line.rfind("/set stream ", 0) == 0) {
      const std::string value = to_lower(trim(line.substr(std::string("/set stream ").size())));
      if (value == "raw") {
        rawStreamMode = true;
        std::cout << "stream mode set to raw\n\n";
      } else if (value == "render" || value == "pretty") {
        rawStreamMode = false;
        std::cout << "stream mode set to render\n\n";
      } else {
        std::cout << "error: unknown stream mode: " << value << " (use raw|render)\n\n";
      }
      continue;
    }

    if (line == "/code list") {
      const auto blocks = extract_code_blocks_from_history(history);
      if (blocks.empty()) {
        std::cout << "no code block found in latest assistant reply\n\n";
        continue;
      }
      for (std::size_t i = 0; i < blocks.size(); ++i) {
        std::string lang = trim(blocks[i].m_language);
        if (lang.empty()) {
          lang = "text";
        }
        std::cout << "[" << (i + 1) << "] lang=" << lang << " bytes=" << blocks[i].m_content.size() << "\n";
      }
      std::cout << "copy one with: /code copy <n>\n\n";
      continue;
    }

    if (line.rfind("/code copy", 0) == 0) {
      const auto blocks = extract_code_blocks_from_history(history);
      if (blocks.empty()) {
        std::cout << "no code block found in latest assistant reply\n\n";
        continue;
      }
      std::size_t index = 1;
      const std::string suffix = trim(line.substr(std::string("/code copy").size()));
      if (!suffix.empty()) {
        try {
          index = static_cast<std::size_t>(std::stoul(suffix));
        } catch (...) {
          std::cout << "error: invalid code block index: " << suffix << "\n\n";
          continue;
        }
      }
      if (index == 0 || index > blocks.size()) {
        std::cout << "error: code block index out of range (1.." << blocks.size() << ")\n\n";
        continue;
      }
      std::string method;
      if (try_copy_text_to_clipboard(blocks[index - 1].m_content, method)) {
        std::cout << "copied code block [" << index << "] to clipboard via " << method << "\n\n";
      } else {
        std::cout << "clipboard tool not found (install pbcopy/xclip/xsel)\n\n";
      }
      continue;
    }

    if (line == "/code shell") {
      const auto blocks = extract_shell_blocks_from_history(history);
      if (blocks.empty()) {
        std::cout << "no shell code block found in latest assistant reply\n\n";
        continue;
      }
      for (std::size_t i = 0; i < blocks.size(); ++i) {
        std::cout << "[" << (i + 1) << "] ```" << blocks[i].m_language << "```\n";
        std::cout << blocks[i].m_content << "\n";
      }
      std::cout << "run one with: /code shell run <n>\n\n";
      continue;
    }

    if (line.rfind("/code shell run", 0) == 0) {
      const auto blocks = extract_shell_blocks_from_history(history);
      if (blocks.empty()) {
        std::cout << "no shell code block found in latest assistant reply\n\n";
        continue;
      }
      std::size_t index = 1;
      const std::string suffix = trim(line.substr(std::string("/code shell run").size()));
      if (!suffix.empty()) {
        try {
          index = static_cast<std::size_t>(std::stoul(suffix));
        } catch (...) {
          std::cout << "error: invalid shell block index: " << suffix << "\n\n";
          continue;
        }
      }
      if (index == 0 || index > blocks.size()) {
        std::cout << "error: shell block index out of range (1.." << blocks.size() << ")\n\n";
        continue;
      }

      const auto& block = blocks[index - 1];
      std::cout << "about to execute shell block [" << index << "]:\n";
      std::cout << block.m_content << "\n";
      std::cout << "type RUN to confirm: ";
      std::string confirmation;
      std::getline(std::cin, confirmation);
      if (confirmation != "RUN") {
        std::cout << "execution cancelled\n\n";
        continue;
      }

      const int exitCode = execute_shell_block(block.m_content);
      std::cout << "\ncommand exit code: " << exitCode << "\n\n";
      continue;
    }

    if (line == "/session") {
      std::cout << "session: " << m_sessionId << "\n\n";
      continue;
    }

    if (line == "/session info") {
      const auto metadata = m_sessionStore.load_metadata(m_sessionId);
      if (!metadata.has_value()) {
        std::cout << "session metadata not found\n\n";
      } else {
        std::cout << "session_id: " << metadata->m_sessionId << "\n";
        std::cout << "created_at: " << format_epoch(metadata->m_createdAtEpoch) << "\n";
        std::cout << "active_model_id: " << metadata->m_activeModelId << "\n";
        std::cout << "runtime_name: " << metadata->m_runtimeName << "\n\n";
      }
      continue;
    }

    if (line == "/session list") {
      const auto sessions = m_sessionStore.list_sessions();
      if (sessions.empty()) {
        std::cout << "no sessions found\n\n";
      } else {
        for (const auto& session : sessions) {
          std::cout << session.m_sessionId << " | created=" << format_epoch(session.m_createdAtEpoch)
                    << " | model=" << session.m_activeModelId << " | runtime=" << session.m_runtimeName << "\n";
        }
        std::cout << "\n";
      }
      continue;
    }

    if (line == "/model list") {
      const auto active = m_orchestrator.active_model();
      std::size_t idx = 1;
      for (const auto& model : m_orchestrator.models()) {
        const bool isActive = active.has_value() && model.m_id == active->get().m_id;
        print_model_line(model, isActive, idx);
        ++idx;
      }
      std::cout << "\n";
      continue;
    }

    if (line == "/model current") {
      if (const auto active = m_orchestrator.active_model(); active.has_value()) {
        print_model_line(active->get(), true);
      } else {
        std::cout << "no active model\n";
      }
      std::cout << "\n";
      continue;
    }

    if (line.rfind("/model use ", 0) == 0) {
      const std::string selector = trim(line.substr(std::string("/model use ").size()));
      const auto selected = resolve_model_selector(m_orchestrator, selector);
      if (!selected.has_value()) {
        std::cout << "error: unknown model selector: " << selector << " (use /model list)\n\n";
        continue;
      }
      std::string error;
      if (!m_orchestrator.set_active_model(selected->get().m_id, error)) {
        std::cout << "error: " << error << "\n\n";
      } else {
        const auto active = m_orchestrator.active_model();
        if (active.has_value()) {
          m_sessionStore.update_metadata(m_sessionId, active->get().m_id, m_orchestrator.active_runtime_name());
          std::cout << "active model: " << active->get().m_id << "\n\n";
        }
      }
      continue;
    }

    if (line.rfind("/model download ", 0) == 0) {
      const std::string selector = trim(line.substr(std::string("/model download ").size()));
      if (selector.empty()) {
        std::cout << "error: model selector required\n\n";
        continue;
      }
      const auto selected = resolve_model_selector(m_orchestrator, selector);
      if (!selected.has_value()) {
        std::cout << "error: unknown model selector: " << selector << " (use /model list)\n\n";
        continue;
      }
      const std::string& modelId = selected->get().m_id;

      const std::string command =
          "./scripts/download_model.sh " + shell_escape_single_quoted(modelId) + " " +
          shell_escape_single_quoted(m_orchestrator.models_file_path());
      const int code = std::system(command.c_str());
      if (code != 0) {
        std::cout << "download failed with exit code: " << code << "\n\n";
      } else {
        std::cout << "download complete for model: " << modelId << "\n\n";
      }
      continue;
    }

    if (line.rfind("/model add ", 0) == 0) {
      const std::string args = trim(line.substr(std::string("/model add ").size()));
      const std::vector<std::string> parts = split_whitespace(args);
      if (parts.size() < 3) {
        std::cout << "usage: /model add <id> <hf-repo> <hf-file> [local-path]\n\n";
        continue;
      }

      ModelSpec model;
      model.m_id = parts[0];
      model.m_name = parts[0];
      model.m_hfRepo = parts[1];
      model.m_hfFile = parts[2];
      model.m_localPath = parts.size() >= 4 ? parts[3] : ("./models/" + parts[2]);

      std::string error;
      if (!m_orchestrator.add_model(model, error)) {
        std::cout << "error: " << error << "\n\n";
        continue;
      }
      std::cout << "added model: " << model.m_id << " -> " << model.m_localPath << "\n";
      std::cout << "next: /model download " << model.m_id << "\n\n";
      continue;
    }

    if (line == "/model validate") {
      std::string report;
      if (m_orchestrator.validate_active_model(report)) {
        std::cout << report << "\n\n";
      } else {
        std::cout << "validation failed: " << report << "\n\n";
      }
      continue;
    }

    if (line.rfind("/model remove ", 0) == 0) {
      const std::string selector = trim(line.substr(std::string("/model remove ").size()));
      const auto model = resolve_model_selector(m_orchestrator, selector);
      if (!model.has_value()) {
        std::cout << "error: unknown model selector: " << selector << " (use /model list)\n\n";
        continue;
      }
      const std::string modelId = model->get().m_id;
      std::cout << "confirm remove local file for model '" << modelId << "' at " << model->get().m_localPath
                << "? [y/N] ";
      std::string confirmation;
      std::getline(std::cin, confirmation);
      if (confirmation != "y" && confirmation != "Y") {
        std::cout << "remove cancelled\n\n";
        continue;
      }

      std::error_code ec;
      const bool removed = std::filesystem::remove(model->get().m_localPath, ec);
      if (ec) {
        std::cout << "error removing file: " << ec.message() << "\n\n";
        continue;
      }
      if (!removed) {
        std::cout << "no file removed (already absent): " << model->get().m_localPath << "\n\n";
      } else {
        std::cout << "removed: " << model->get().m_localPath << "\n";
      }

      const auto active = m_orchestrator.active_model();
      if (active.has_value() && active->get().m_id == modelId) {
        for (const auto& candidate : m_orchestrator.models()) {
          if (candidate.m_id != modelId) {
            std::string error;
            if (m_orchestrator.set_active_model(candidate.m_id, error)) {
              std::cout << "active model switched to: " << candidate.m_id << "\n";
              m_sessionStore.update_metadata(m_sessionId, candidate.m_id, m_orchestrator.active_runtime_name());
            }
            break;
          }
        }
      }
      std::cout << "\n";
      continue;
    }

    if (line.empty()) {
      continue;
    }

    Message userMsg{Role::User, line};
    history.push_back(userMsg);
    m_sessionStore.append(m_sessionId, userMsg);

    std::cout << "sentra> ";
    try {
      std::string streamed;
      auto result = m_orchestrator.respond(history, [&](const std::string& token) {
        streamed += token;
        if (rawStreamMode) {
          std::cout << token;
          std::cout.flush();
        }
      });
      if (!rawStreamMode) {
        std::cout << render_markdown_for_terminal(result.m_text);
      }
      std::cout << "\n";
      if (result.m_contextTruncated && !result.m_warning.empty()) {
        std::cout << "[warn] " << result.m_warning << "\n";
      }
      if (result.m_totalMs > 0.0) {
        std::cout << "[perf] first_token=" << std::fixed << std::setprecision(1) << result.m_firstTokenMs
                  << "ms total=" << result.m_totalMs << "ms tokens=" << result.m_generatedTokens
                  << " tps=" << result.m_tokensPerSecond << "\n";
      }
      std::cout << "\n";

      Message assistantMsg{Role::Assistant, result.m_text};
      history.push_back(assistantMsg);
      m_sessionStore.append(m_sessionId, assistantMsg);
      if (!extract_shell_blocks_from_history(history).empty()) {
        std::cout << "[tip] assistant included shell code. review with /code shell\n\n";
      }
      if (const auto active = m_orchestrator.active_model(); active.has_value()) {
        m_sessionStore.update_metadata(m_sessionId, active->get().m_id, m_orchestrator.active_runtime_name());
      }
    } catch (const std::exception& ex) {
      std::cout << "\nerror: " << ex.what() << "\n\n";
    }
  }

  return 0;
}

}  // namespace sentra
