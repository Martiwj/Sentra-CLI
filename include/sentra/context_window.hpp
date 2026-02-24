#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "sentra/types.hpp"

namespace sentra {

struct ContextPruneResult {
  std::vector<Message> m_messages;
  bool m_truncated{false};
  std::size_t m_tokensKept{0};
};

std::size_t estimate_tokens(const std::string& text);
ContextPruneResult prune_context_window(const std::vector<Message>& history, std::size_t tokenBudget);

}  // namespace sentra
