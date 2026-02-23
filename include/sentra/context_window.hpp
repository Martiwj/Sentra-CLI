#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "sentra/types.hpp"

namespace sentra {

struct ContextPruneResult {
  std::vector<Message> messages;
  bool truncated{false};
  std::size_t tokens_kept{0};
};

std::size_t estimate_tokens(const std::string& text);
ContextPruneResult prune_context_window(const std::vector<Message>& history, std::size_t token_budget);

}  // namespace sentra
