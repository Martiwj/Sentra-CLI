#pragma once

#include <string>

#include "sentra/orchestrator.hpp"
#include "sentra/session_store.hpp"

namespace sentra {

class Repl {
 public:
  Repl(std::string session_id, SessionStore session_store, Orchestrator orchestrator, std::string system_prompt);

  int run();

 private:
  std::string session_id_;
  SessionStore session_store_;
  Orchestrator orchestrator_;
  std::string system_prompt_;
};

}  // namespace sentra
