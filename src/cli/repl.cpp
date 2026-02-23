#include "sentra/repl.hpp"

#include <filesystem>
#include <iostream>
#include <vector>

namespace sentra {
namespace {

void print_model_line(const ModelSpec& model, bool active) {
  const bool ready = std::filesystem::exists(model.local_path);
  std::cout << (active ? "* " : "  ") << model.id << " | " << model.name
            << " | ready=" << (ready ? "yes" : "no") << " | path=" << model.local_path << "\n";
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

  if (history.empty()) {
    const Message system_msg{Role::System, system_prompt_};
    history.push_back(system_msg);
    session_store_.append(session_id_, system_msg);
  }

  std::cout << "Sentra CLI MVP\n";
  std::cout << "session: " << session_id_ << "\n";
  std::cout << "runtime: " << orchestrator_.active_runtime_name() << "\n";
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
      std::cout << "/model list           List configured models\n";
      std::cout << "/model current        Print active model\n";
      std::cout << "/model use <model-id> Switch active model\n\n";
      continue;
    }

    if (line == "/session") {
      std::cout << "session: " << session_id_ << "\n\n";
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
      const std::string model_id = line.substr(std::string("/model use ").size());
      std::string error;
      if (!orchestrator_.set_active_model(model_id, error)) {
        std::cout << "error: " << error << "\n\n";
      } else {
        const ModelSpec* active = orchestrator_.active_model();
        if (active != nullptr) {
          std::cout << "active model: " << active->id << "\n\n";
        }
      }
      continue;
    }

    if (line.empty()) {
      continue;
    }

    Message user_msg{Role::User, line};
    history.push_back(user_msg);
    session_store_.append(session_id_, user_msg);

    std::cout << "sentra> ";
    auto result = orchestrator_.respond(history, [](const std::string& token) {
      std::cout << token;
      std::cout.flush();
    });
    std::cout << "\n\n";

    Message assistant_msg{Role::Assistant, result.text};
    history.push_back(assistant_msg);
    session_store_.append(session_id_, assistant_msg);
  }

  return 0;
}

}  // namespace sentra
