#pragma once

#include <string>

#include "sentra/orchestrator.hpp"
#include "sentra/session_store.hpp"

namespace sentra {

class Repl {
 public:
  Repl(std::string sessionId, SessionStore&& sessionStore, Orchestrator&& orchestrator,
       std::string systemPrompt);

  int run();

 private:
  std::string m_sessionId;
  SessionStore m_sessionStore;
  Orchestrator m_orchestrator;
  std::string m_systemPrompt;
};

}  // namespace sentra
