#include "sentra/repl.hpp"

#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <time.h>
#include <vector>

namespace sentra {
namespace {

void print_model_line(const ModelSpec& model, bool active) {
  const bool ready = std::filesystem::exists(model.local_path);
  std::cout << (active ? "* " : "  ") << model.id << " | " << model.name
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

std::vector<std::string> split_whitespace(const std::string& input) {
  std::istringstream in(input);
  std::vector<std::string> tokens;
  std::string token;
  while (in >> token) {
    tokens.push_back(token);
  }
  return tokens;
}

}  // namespace

Repl::Repl(std::string session_id, SessionStore session_store, Orchestrator orchestrator,
           std::string system_prompt)
    : session_id_(std::move(session_id)),
      session_store_(std::move(session_store)),
      orchestrator_(std::move(orchestrator)),
      system_prompt_(std::move(system_prompt)) {}

int Repl::run() {
  std::vector<Message> history = session_store_.load(session_id_);
  const std::string startup_model_id =
      orchestrator_.active_model() ? orchestrator_.active_model()->id : "";
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
  if (const ModelSpec* model = orchestrator_.active_model(); model != nullptr) {
    std::cout << "model: " << model->id << "\n";
  }
  std::cout << "type /help for commands\n\n";

  std::string line;
  while (true) {
    std::cout << "you> ";
    if (!std::getline(std::cin, line)) {
      std::cout << "\n";
      break;
    }

    if (line == "/exit" || line == "/quit") {
      break;
    }

    if (line == "/help") {
      std::cout << "/help                 Show commands\n";
      std::cout << "/exit                 Exit Sentra\n";
      std::cout << "/session              Print session id\n";
      std::cout << "/session info         Print current session metadata\n";
      std::cout << "/session list         List known sessions\n";
      std::cout << "/model list           List configured models\n";
      std::cout << "/model current        Print active model\n";
      std::cout << "/model use <model-id> Switch active model\n\n";
      std::cout << "/model add <id> <hf-repo> <hf-file> [local-path]\n";
      std::cout << "/model download <id>  Download configured model preset\n";
      std::cout << "/model validate       Validate active model path and metadata\n";
      std::cout << "/model remove <id>    Remove local model file with confirmation\n\n";
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
      const ModelSpec* active = orchestrator_.active_model();
      for (const auto& model : orchestrator_.models()) {
        print_model_line(model, active != nullptr && model.id == active->id);
      }
      std::cout << "\n";
      continue;
    }

    if (line == "/model current") {
      if (const ModelSpec* active = orchestrator_.active_model(); active != nullptr) {
        print_model_line(*active, true);
      } else {
        std::cout << "no active model\n";
      }
      std::cout << "\n";
      continue;
    }

    if (line.rfind("/model use ", 0) == 0) {
      const std::string model_id = trim(line.substr(std::string("/model use ").size()));
      std::string error;
      if (!orchestrator_.set_active_model(model_id, error)) {
        std::cout << "error: " << error << "\n\n";
      } else {
        const ModelSpec* active = orchestrator_.active_model();
        if (active != nullptr) {
          session_store_.update_metadata(session_id_, active->id, orchestrator_.active_runtime_name());
          std::cout << "active model: " << active->id << "\n\n";
        }
      }
      continue;
    }

    if (line.rfind("/model download ", 0) == 0) {
      const std::string model_id = trim(line.substr(std::string("/model download ").size()));
      if (model_id.empty()) {
        std::cout << "error: model id required\n\n";
        continue;
      }
      if (orchestrator_.find_model(model_id) == nullptr) {
        std::cout << "error: unknown model id: " << model_id << "\n\n";
        continue;
      }

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
      const std::string model_id = trim(line.substr(std::string("/model remove ").size()));
      const ModelSpec* model = orchestrator_.find_model(model_id);
      if (model == nullptr) {
        std::cout << "error: unknown model id: " << model_id << "\n\n";
        continue;
      }
      std::cout << "confirm remove local file for model '" << model_id << "' at " << model->local_path
                << "? [y/N] ";
      std::string confirmation;
      std::getline(std::cin, confirmation);
      if (confirmation != "y" && confirmation != "Y") {
        std::cout << "remove cancelled\n\n";
        continue;
      }

      std::error_code ec;
      const bool removed = std::filesystem::remove(model->local_path, ec);
      if (ec) {
        std::cout << "error removing file: " << ec.message() << "\n\n";
        continue;
      }
      if (!removed) {
        std::cout << "no file removed (already absent): " << model->local_path << "\n\n";
      } else {
        std::cout << "removed: " << model->local_path << "\n";
      }

      const ModelSpec* active = orchestrator_.active_model();
      if (active != nullptr && active->id == model_id) {
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
      auto result = orchestrator_.respond(history, [](const std::string& token) {
        std::cout << token;
        std::cout.flush();
      });
      std::cout << "\n";
      if (result.context_truncated && !result.warning.empty()) {
        std::cout << "[warn] " << result.warning << "\n";
      }
      std::cout << "\n";

      Message assistant_msg{Role::Assistant, result.text};
      history.push_back(assistant_msg);
      session_store_.append(session_id_, assistant_msg);
      if (const ModelSpec* active = orchestrator_.active_model(); active != nullptr) {
        session_store_.update_metadata(session_id_, active->id, orchestrator_.active_runtime_name());
      }
    } catch (const std::exception& ex) {
      std::cout << "\nerror: " << ex.what() << "\n\n";
    }
  }

  return 0;
}

}  // namespace sentra
