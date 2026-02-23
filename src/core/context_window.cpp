#include "sentra/context_window.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace sentra {

std::size_t estimate_tokens(const std::string& text) {
  std::size_t words = 0;
  bool in_word = false;
  for (char c : text) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      in_word = false;
      continue;
    }
    if (!in_word) {
      ++words;
      in_word = true;
    }
  }
  return words == 0 && !text.empty() ? 1 : words;
}

ContextPruneResult prune_context_window(const std::vector<Message>& history, std::size_t token_budget) {
  ContextPruneResult result;
  if (history.empty()) {
    return result;
  }

  std::vector<std::size_t> pinned_system_indices;
  pinned_system_indices.reserve(history.size());

  for (std::size_t i = 0; i < history.size(); ++i) {
    if (history[i].role == Role::System) {
      pinned_system_indices.push_back(i);
      result.tokens_kept += estimate_tokens(history[i].content);
    }
  }

  std::vector<std::size_t> kept_non_system_indices;
  kept_non_system_indices.reserve(history.size());

  for (std::size_t i = history.size(); i-- > 0;) {
    if (history[i].role == Role::System) {
      continue;
    }
    const std::size_t message_tokens = estimate_tokens(history[i].content);
    if (result.tokens_kept + message_tokens > token_budget) {
      result.truncated = true;
      continue;
    }
    result.tokens_kept += message_tokens;
    kept_non_system_indices.push_back(i);
  }

  std::unordered_set<std::size_t> keep;
  for (std::size_t idx : pinned_system_indices) {
    keep.insert(idx);
  }
  for (std::size_t idx : kept_non_system_indices) {
    keep.insert(idx);
  }

  result.messages.reserve(keep.size());
  for (std::size_t i = 0; i < history.size(); ++i) {
    if (keep.find(i) != keep.end()) {
      result.messages.push_back(history[i]);
    } else {
      result.truncated = true;
    }
  }

  return result;
}

}  // namespace sentra
