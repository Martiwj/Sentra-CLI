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
  const bool ready = std::filesystem::exists(model.local_path);
  std::cout << (active ? "* " : "  ") << model.id << " | " << model.name
            << " | ready=" << (ready ? "yes" : "no") << " | path=" << model.local_path << "\n";
}

void print_model_line(const ModelSpec& model, bool active, std::size_t index_1based) {
  const bool ready = std::filesystem::exists(model.local_path);
  std::cout << (active ? "* " : "  ") << "[" << index_1based << "] " << model.id << " | " << model.name
            << " | ready=" << (ready ? "yes" : "no") << " | path=" << model.local_path << "\n";
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
  std::string language;
  std::string content;
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
    const std::size_t fence_start = text.find("```", pos);
    if (fence_start == std::string::npos) {
      break;
    }
    const std::size_t lang_end = text.find('\n', fence_start + 3);
    if (lang_end == std::string::npos) {
      break;
    }
    const std::string language = text.substr(fence_start + 3, lang_end - (fence_start + 3));
    const std::size_t fence_end = text.find("```", lang_end + 1);
    if (fence_end == std::string::npos) {
      break;
    }
    const std::string content = text.substr(lang_end + 1, fence_end - (lang_end + 1));
    out.push_back({trim(language), content});
    pos = fence_end + 3;
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

  std::size_t comment_pos = std::string::npos;
  bool in_string = false;
  char quote = '\0';
  for (std::size_t i = 0; i + 1 < line.size(); ++i) {
    if (in_string) {
      if (line[i] == '\\') {
        ++i;
      } else if (line[i] == quote) {
        in_string = false;
      }
      continue;
    }
    if (line[i] == '"' || line[i] == '\'') {
      in_string = true;
      quote = line[i];
      continue;
    }
    if (line[i] == '/' && line[i + 1] == '/') {
      comment_pos = i;
      break;
    }
    if (line[i] == '-' && line[i + 1] == '-') {
      comment_pos = i;
      break;
    }
  }
  if (comment_pos == std::string::npos) {
    for (std::size_t i = 0; i < line.size(); ++i) {
      if (line[i] == '#') {
        comment_pos = i;
        break;
      }
    }
  }

  const std::string code = comment_pos == std::string::npos ? line : line.substr(0, comment_pos);
  const std::string comment = comment_pos == std::string::npos ? "" : line.substr(comment_pos);

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
  bool in_code = false;
  std::string code_lang;
  std::size_t code_line_number = 0;

  while (std::getline(in, line)) {
    if (line.rfind("```", 0) == 0) {
      if (!in_code) {
        in_code = true;
        code_lang = to_lower(trim(line.substr(3)));
        if (code_lang.empty()) {
          code_lang = "text";
        }
        code_line_number = 0;
        out << "\033[48;5;236;38;5;255m " << code_lang << " code \033[0m\n";
      } else {
        in_code = false;
        code_lang.clear();
        out << "\n";
      }
      continue;
    }

    if (in_code) {
      ++code_line_number;
      out << "\033[38;5;240m" << std::setw(4) << code_line_number << " |\033[0m ";
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
    if (it->role == Role::Assistant) {
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
  return extract_fenced_code_blocks(assistant->get().content);
}

std::vector<CodeBlock> extract_shell_blocks_from_history(const std::vector<Message>& history) {
  std::vector<CodeBlock> shell_blocks;
  const auto blocks = extract_code_blocks_from_history(history);
  for (const auto& block : blocks) {
    if (is_shell_language(block.language)) {
      shell_blocks.push_back(block);
    }
  }
  return shell_blocks;
}

int execute_shell_block(const std::string& script_content) {
  const std::filesystem::path temp_path =
      std::filesystem::temp_directory_path() /
      ("sentra-shell-" + std::to_string(static_cast<long long>(std::time(nullptr))) + ".sh");
  {
    std::ofstream out(temp_path);
    if (!out.is_open()) {
      std::cout << "error: failed to create temporary shell script\n";
      return 1;
    }
    out << "#!/usr/bin/env bash\nset -euo pipefail\n";
    out << script_content << "\n";
  }
  std::filesystem::permissions(
      temp_path, std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read,
      std::filesystem::perm_options::add);

  const std::string command = "/bin/bash " + shell_escape_single_quoted(temp_path.string());
  const int status = std::system(command.c_str());
  std::filesystem::remove(temp_path);
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  return 1;
}

bool try_copy_text_to_clipboard(const std::string& text, std::string& method_used) {
  const std::vector<std::pair<std::string, std::string>> copy_commands = {
      {"pbcopy", "pbcopy"},
      {"xclip", "xclip -selection clipboard"},
      {"xsel", "xsel --clipboard --input"},
  };

  for (const auto& entry : copy_commands) {
    const std::string detect = "command -v " + entry.first + " >/dev/null 2>&1";
    if (std::system(detect.c_str()) != 0) {
      continue;
    }
    const std::filesystem::path temp_path =
        std::filesystem::temp_directory_path() /
        ("sentra-clipboard-" + std::to_string(static_cast<long long>(std::time(nullptr))) + ".txt");
    {
      std::ofstream out(temp_path, std::ios::trunc);
      if (!out.is_open()) {
        continue;
      }
      out << text;
    }

    const std::string copy_cmd =
        "cat " + shell_escape_single_quoted(temp_path.string()) + " | " + entry.second + " >/dev/null 2>&1";
    const int status = std::system(copy_cmd.c_str());
    std::filesystem::remove(temp_path);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
      method_used = entry.first;
      return true;
    }
  }

  method_used.clear();
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

  bool all_digits = true;
  for (char c : value) {
    if (c < '0' || c > '9') {
      all_digits = false;
      break;
    }
  }

  if (all_digits) {
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

void print_status_line_items(const Orchestrator& orchestrator, const std::string& session_id, bool raw_stream_mode) {
  std::cout << "session: " << session_id << "\n";
  std::cout << "runtime: " << orchestrator.active_runtime_name() << "\n";
  if (const auto model = orchestrator.active_model(); model.has_value()) {
    std::cout << "model: " << model->get().id << "\n";
  } else {
    std::cout << "model: none\n";
  }
  std::cout << "profile: " << orchestrator.profile() << "\n";
  std::cout << "max_tokens: " << orchestrator.max_tokens() << "\n";
  std::cout << "context_window_tokens: " << orchestrator.context_window_tokens() << "\n";
  std::cout << "stream_mode: " << (raw_stream_mode ? "raw" : "render") << "\n";
  if (!orchestrator.runtime_selection_note().empty()) {
    std::cout << "note: " << orchestrator.runtime_selection_note() << "\n";
  }
  std::cout << "\n";
}

std::string shorten_for_prompt(const std::string& value, std::size_t max_len) {
  if (value.size() <= max_len) {
    return value;
  }
  if (max_len < 4) {
    return value.substr(0, max_len);
  }
  return value.substr(0, max_len - 3) + "...";
}

std::string make_user_prompt(const Orchestrator& orchestrator, bool menu_mode) {
  if (menu_mode) {
    return "\033[1;38;5;39mmenu>\033[0m ";
  }
  const std::string runtime = shorten_for_prompt(orchestrator.active_runtime_name(), 10);
  std::string model = "none";
  if (const auto active = orchestrator.active_model(); active.has_value()) {
    model = shorten_for_prompt(active->get().id, 14);
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

Repl::Repl(std::string session_id, SessionStore&& session_store, Orchestrator&& orchestrator,
           std::string system_prompt)
    : session_id_(std::move(session_id)),
      session_store_(std::move(session_store)),
      orchestrator_(std::move(orchestrator)),
      system_prompt_(std::move(system_prompt)) {}

int Repl::run() {
  std::vector<Message> history = session_store_.load(session_id_);
  const auto startup_model = orchestrator_.active_model();
  const std::string startup_model_id = startup_model.has_value() ? startup_model->get().id : "";
  session_store_.ensure_session(session_id_, startup_model_id, orchestrator_.active_runtime_name());

  if (history.empty()) {
    const Message system_msg{Role::System, system_prompt_};
    history.push_back(system_msg);
    session_store_.append(session_id_, system_msg);
  }

  std::cout << "Sentra CLI MVP\n";
  std::cout << "session: " << session_id_ << "\n";
  std::cout << "runtime: " << orchestrator_.active_runtime_name() << "\n";
  if (!orchestrator_.runtime_selection_note().empty()) {
    std::cout << "note: " << orchestrator_.runtime_selection_note() << "\n";
  }
  if (const auto model = orchestrator_.active_model(); model.has_value()) {
    std::cout << "model: " << model->get().id << "\n";
  }
  std::cout << "type /help for commands\n\n";

  std::string line;
  bool menu_shortcut_mode = false;
  bool raw_stream_mode = (orchestrator_.profile() == "fast");
  while (true) {
    std::cout << make_user_prompt(orchestrator_, menu_shortcut_mode);
    if (!std::getline(std::cin, line)) {
      std::cout << "\n";
      break;
    }

    if (menu_shortcut_mode) {
      const std::string shortcut = trim(line);
      if (shortcut == "q" || shortcut == "quit" || shortcut == "exit") {
        line = "/menu run 0";
      } else if (is_positive_integer(shortcut)) {
        line = "/menu run " + shortcut;
      } else if (!shortcut.empty() && shortcut[0] == '/') {
        menu_shortcut_mode = false;
      } else if (shortcut.empty()) {
        continue;
      } else {
        std::cout << "enter a menu number, or use a slash command to leave menu mode\n\n";
        continue;
      }
    }

    if (!menu_shortcut_mode && !line.empty() && line[0] != '/') {
      line = normalize_user_shortcut(line);
    }

    if (line == "/exit" || line == "/quit") {
      break;
    }

    if (line == "/status") {
      print_status_line_items(orchestrator_, session_id_, raw_stream_mode);
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
      menu_shortcut_mode = true;
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
        print_status_line_items(orchestrator_, session_id_, raw_stream_mode);
        continue;
      }
      if (action == 2) {
        const auto active = orchestrator_.active_model();
        std::size_t idx = 1;
        for (const auto& model : orchestrator_.models()) {
          const bool is_active = active.has_value() && model.id == active->get().id;
          print_model_line(model, is_active, idx);
          ++idx;
        }
        std::cout << "\n";
        continue;
      }
      if (action == 3) {
        std::cout << "model id or number: ";
        std::string model_selector;
        std::getline(std::cin, model_selector);
        const auto selected = resolve_model_selector(orchestrator_, model_selector);
        if (!selected.has_value()) {
          std::cout << "error: unknown model selector: " << model_selector << "\n\n";
          continue;
        }
        std::string error;
        if (!orchestrator_.set_active_model(selected->get().id, error)) {
          std::cout << "error: " << error << "\n\n";
        } else {
          session_store_.update_metadata(session_id_, selected->get().id, orchestrator_.active_runtime_name());
          std::cout << "active model: " << selected->get().id << "\n\n";
        }
        continue;
      }
      if (action == 4) {
        std::cout << "model id or number to download: ";
        std::string model_selector;
        std::getline(std::cin, model_selector);
        const auto selected = resolve_model_selector(orchestrator_, model_selector);
        if (!selected.has_value()) {
          std::cout << "error: unknown model selector: " << model_selector << "\n\n";
          continue;
        }
        const std::string command =
            "./scripts/download_model.sh " + shell_escape_single_quoted(selected->get().id) + " " +
            shell_escape_single_quoted(orchestrator_.models_file_path());
        const int code = std::system(command.c_str());
        if (code != 0) {
          std::cout << "download failed with exit code: " << code << "\n\n";
        } else {
          std::cout << "download complete for model: " << selected->get().id << "\n\n";
        }
        continue;
      }
      if (action == 5) {
        std::string report;
        if (orchestrator_.validate_active_model(report)) {
          std::cout << report << "\n\n";
        } else {
          std::cout << "validation failed: " << report << "\n\n";
        }
        continue;
      }
      if (action == 6) {
        const auto metadata = session_store_.load_metadata(session_id_);
        if (!metadata.has_value()) {
          std::cout << "session metadata not found\n\n";
        } else {
          std::cout << "session_id: " << metadata->session_id << "\n";
          std::cout << "created_at: " << format_epoch(metadata->created_at_epoch) << "\n";
          std::cout << "active_model_id: " << metadata->active_model_id << "\n";
          std::cout << "runtime_name: " << metadata->runtime_name << "\n\n";
        }
        continue;
      }
      if (action == 7) {
        const auto sessions = session_store_.list_sessions();
        if (sessions.empty()) {
          std::cout << "no sessions found\n\n";
        } else {
          for (const auto& session : sessions) {
            std::cout << session.session_id << " | created=" << format_epoch(session.created_at_epoch)
                      << " | model=" << session.active_model_id << " | runtime=" << session.runtime_name << "\n";
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
            std::string lang = trim(blocks[i].language);
            if (lang.empty()) {
              lang = "text";
            }
            std::cout << "[" << (i + 1) << "] lang=" << lang << " bytes=" << blocks[i].content.size() << "\n";
          }
          std::cout << "\n";
        }
        continue;
      }
      if (action == 9) {
        std::cout << "code block number (default 1): ";
        std::string code_selector;
        std::getline(std::cin, code_selector);
        const auto blocks = extract_code_blocks_from_history(history);
        if (blocks.empty()) {
          std::cout << "no code block found in latest assistant reply\n\n";
          continue;
        }
        std::size_t index = 1;
        if (!trim(code_selector).empty()) {
          try {
            index = static_cast<std::size_t>(std::stoul(trim(code_selector)));
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
        if (try_copy_text_to_clipboard(blocks[index - 1].content, method)) {
          std::cout << "copied code block [" << index << "] to clipboard via " << method << "\n\n";
        } else {
          std::cout << "clipboard tool not found (install pbcopy/xclip/xsel)\n\n";
        }
        continue;
      }
      if (action == 10) {
        std::cout << "shell code block number (default 1): ";
        std::string shell_selector;
        std::getline(std::cin, shell_selector);
        const auto blocks = extract_shell_blocks_from_history(history);
        if (blocks.empty()) {
          std::cout << "no shell code block found in latest assistant reply\n\n";
          continue;
        }
        std::size_t index = 1;
        if (!trim(shell_selector).empty()) {
          try {
            index = static_cast<std::size_t>(std::stoul(trim(shell_selector)));
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
        std::cout << blocks[index - 1].content << "\n";
        std::cout << "type RUN to confirm: ";
        std::string confirmation;
        std::getline(std::cin, confirmation);
        if (confirmation != "RUN") {
          std::cout << "execution cancelled\n\n";
          continue;
        }
        const int exit_code = execute_shell_block(blocks[index - 1].content);
        std::cout << "\ncommand exit code: " << exit_code << "\n\n";
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
      if (!orchestrator_.set_profile(mode, error)) {
        std::cout << "error: " << error << "\n\n";
        continue;
      }
      raw_stream_mode = (orchestrator_.profile() == "fast");
      std::cout << "profile set: " << orchestrator_.profile() << "\n";
      std::cout << "max_tokens: " << orchestrator_.max_tokens() << ", context_window_tokens: "
                << orchestrator_.context_window_tokens() << ", stream_mode: "
                << (raw_stream_mode ? "raw" : "render") << "\n\n";
      continue;
    }

    if (line.rfind("/set max_tokens ", 0) == 0) {
      const std::string value = trim(line.substr(std::string("/set max_tokens ").size()));
      try {
        const std::size_t n = static_cast<std::size_t>(std::stoull(value));
        orchestrator_.set_max_tokens(n);
        std::cout << "max_tokens set to " << orchestrator_.max_tokens() << "\n\n";
      } catch (...) {
        std::cout << "error: invalid max_tokens value: " << value << "\n\n";
      }
      continue;
    }

    if (line.rfind("/set context ", 0) == 0) {
      const std::string value = trim(line.substr(std::string("/set context ").size()));
      try {
        const std::size_t n = static_cast<std::size_t>(std::stoull(value));
        orchestrator_.set_context_window_tokens(n);
        std::cout << "context_window_tokens set to " << orchestrator_.context_window_tokens() << "\n\n";
      } catch (...) {
        std::cout << "error: invalid context token value: " << value << "\n\n";
      }
      continue;
    }

    if (line.rfind("/set stream ", 0) == 0) {
      const std::string value = to_lower(trim(line.substr(std::string("/set stream ").size())));
      if (value == "raw") {
        raw_stream_mode = true;
        std::cout << "stream mode set to raw\n\n";
      } else if (value == "render" || value == "pretty") {
        raw_stream_mode = false;
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
        std::string lang = trim(blocks[i].language);
        if (lang.empty()) {
          lang = "text";
        }
        std::cout << "[" << (i + 1) << "] lang=" << lang << " bytes=" << blocks[i].content.size() << "\n";
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
      if (try_copy_text_to_clipboard(blocks[index - 1].content, method)) {
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
        std::cout << "[" << (i + 1) << "] ```" << blocks[i].language << "```\n";
        std::cout << blocks[i].content << "\n";
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
      std::cout << block.content << "\n";
      std::cout << "type RUN to confirm: ";
      std::string confirmation;
      std::getline(std::cin, confirmation);
      if (confirmation != "RUN") {
        std::cout << "execution cancelled\n\n";
        continue;
      }

      const int exit_code = execute_shell_block(block.content);
      std::cout << "\ncommand exit code: " << exit_code << "\n\n";
      continue;
    }

    if (line == "/session") {
      std::cout << "session: " << session_id_ << "\n\n";
      continue;
    }

    if (line == "/session info") {
      const auto metadata = session_store_.load_metadata(session_id_);
      if (!metadata.has_value()) {
        std::cout << "session metadata not found\n\n";
      } else {
        std::cout << "session_id: " << metadata->session_id << "\n";
        std::cout << "created_at: " << format_epoch(metadata->created_at_epoch) << "\n";
        std::cout << "active_model_id: " << metadata->active_model_id << "\n";
        std::cout << "runtime_name: " << metadata->runtime_name << "\n\n";
      }
      continue;
    }

    if (line == "/session list") {
      const auto sessions = session_store_.list_sessions();
      if (sessions.empty()) {
        std::cout << "no sessions found\n\n";
      } else {
        for (const auto& session : sessions) {
          std::cout << session.session_id << " | created=" << format_epoch(session.created_at_epoch)
                    << " | model=" << session.active_model_id << " | runtime=" << session.runtime_name << "\n";
        }
        std::cout << "\n";
      }
      continue;
    }

    if (line == "/model list") {
      const auto active = orchestrator_.active_model();
      std::size_t idx = 1;
      for (const auto& model : orchestrator_.models()) {
        const bool is_active = active.has_value() && model.id == active->get().id;
        print_model_line(model, is_active, idx);
        ++idx;
      }
      std::cout << "\n";
      continue;
    }

    if (line == "/model current") {
      if (const auto active = orchestrator_.active_model(); active.has_value()) {
        print_model_line(active->get(), true);
      } else {
        std::cout << "no active model\n";
      }
      std::cout << "\n";
      continue;
    }

    if (line.rfind("/model use ", 0) == 0) {
      const std::string selector = trim(line.substr(std::string("/model use ").size()));
      const auto selected = resolve_model_selector(orchestrator_, selector);
      if (!selected.has_value()) {
        std::cout << "error: unknown model selector: " << selector << " (use /model list)\n\n";
        continue;
      }
      std::string error;
      if (!orchestrator_.set_active_model(selected->get().id, error)) {
        std::cout << "error: " << error << "\n\n";
      } else {
        const auto active = orchestrator_.active_model();
        if (active.has_value()) {
          session_store_.update_metadata(session_id_, active->get().id, orchestrator_.active_runtime_name());
          std::cout << "active model: " << active->get().id << "\n\n";
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
      const auto selected = resolve_model_selector(orchestrator_, selector);
      if (!selected.has_value()) {
        std::cout << "error: unknown model selector: " << selector << " (use /model list)\n\n";
        continue;
      }
      const std::string& model_id = selected->get().id;

      const std::string command =
          "./scripts/download_model.sh " + shell_escape_single_quoted(model_id) + " " +
          shell_escape_single_quoted(orchestrator_.models_file_path());
      const int code = std::system(command.c_str());
      if (code != 0) {
        std::cout << "download failed with exit code: " << code << "\n\n";
      } else {
        std::cout << "download complete for model: " << model_id << "\n\n";
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
      model.id = parts[0];
      model.name = parts[0];
      model.hf_repo = parts[1];
      model.hf_file = parts[2];
      model.local_path = parts.size() >= 4 ? parts[3] : ("./models/" + parts[2]);

      std::string error;
      if (!orchestrator_.add_model(model, error)) {
        std::cout << "error: " << error << "\n\n";
        continue;
      }
      std::cout << "added model: " << model.id << " -> " << model.local_path << "\n";
      std::cout << "next: /model download " << model.id << "\n\n";
      continue;
    }

    if (line == "/model validate") {
      std::string report;
      if (orchestrator_.validate_active_model(report)) {
        std::cout << report << "\n\n";
      } else {
        std::cout << "validation failed: " << report << "\n\n";
      }
      continue;
    }

    if (line.rfind("/model remove ", 0) == 0) {
      const std::string selector = trim(line.substr(std::string("/model remove ").size()));
      const auto model = resolve_model_selector(orchestrator_, selector);
      if (!model.has_value()) {
        std::cout << "error: unknown model selector: " << selector << " (use /model list)\n\n";
        continue;
      }
      const std::string model_id = model->get().id;
      std::cout << "confirm remove local file for model '" << model_id << "' at " << model->get().local_path
                << "? [y/N] ";
      std::string confirmation;
      std::getline(std::cin, confirmation);
      if (confirmation != "y" && confirmation != "Y") {
        std::cout << "remove cancelled\n\n";
        continue;
      }

      std::error_code ec;
      const bool removed = std::filesystem::remove(model->get().local_path, ec);
      if (ec) {
        std::cout << "error removing file: " << ec.message() << "\n\n";
        continue;
      }
      if (!removed) {
        std::cout << "no file removed (already absent): " << model->get().local_path << "\n\n";
      } else {
        std::cout << "removed: " << model->get().local_path << "\n";
      }

      const auto active = orchestrator_.active_model();
      if (active.has_value() && active->get().id == model_id) {
        for (const auto& candidate : orchestrator_.models()) {
          if (candidate.id != model_id) {
            std::string error;
            if (orchestrator_.set_active_model(candidate.id, error)) {
              std::cout << "active model switched to: " << candidate.id << "\n";
              session_store_.update_metadata(session_id_, candidate.id, orchestrator_.active_runtime_name());
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

    Message user_msg{Role::User, line};
    history.push_back(user_msg);
    session_store_.append(session_id_, user_msg);

    std::cout << "sentra> ";
    try {
      std::string streamed;
      auto result = orchestrator_.respond(history, [&](const std::string& token) {
        streamed += token;
        if (raw_stream_mode) {
          std::cout << token;
          std::cout.flush();
        }
      });
      if (!raw_stream_mode) {
        std::cout << render_markdown_for_terminal(result.text);
      }
      std::cout << "\n";
      if (result.context_truncated && !result.warning.empty()) {
        std::cout << "[warn] " << result.warning << "\n";
      }
      if (result.total_ms > 0.0) {
        std::cout << "[perf] first_token=" << std::fixed << std::setprecision(1) << result.first_token_ms
                  << "ms total=" << result.total_ms << "ms tokens=" << result.generated_tokens
                  << " tps=" << result.tokens_per_second << "\n";
      }
      std::cout << "\n";

      Message assistant_msg{Role::Assistant, result.text};
      history.push_back(assistant_msg);
      session_store_.append(session_id_, assistant_msg);
      if (!extract_shell_blocks_from_history(history).empty()) {
        std::cout << "[tip] assistant included shell code. review with /code shell\n\n";
      }
      if (const auto active = orchestrator_.active_model(); active.has_value()) {
        session_store_.update_metadata(session_id_, active->get().id, orchestrator_.active_runtime_name());
      }
    } catch (const std::exception& ex) {
      std::cout << "\nerror: " << ex.what() << "\n\n";
    }
  }

  return 0;
}

}  // namespace sentra
