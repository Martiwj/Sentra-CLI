#include "sentra/context_window.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace sentra {

std::size_t estimate_tokens(const std::string& text) {
  std::size_t words = 0;
  bool inWord = false;
  for (char c : text) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      inWord = false;
      continue;
    }
    if (!inWord) {
      ++words;
      inWord = true;
    }
  }
  return words == 0 && !text.empty() ? 1 : words;
}

ContextPruneResult prune_context_window(const std::vector<Message>& history, std::size_t tokenBudget) {
  ContextPruneResult result;
  if (history.empty()) {
    return result;
  }

  std::vector<std::size_t> pinnedSystemIndices;
  pinnedSystemIndices.reserve(history.size());

  for (std::size_t i = 0; i < history.size(); ++i) {
    if (history[i].m_role == Role::System) {
      pinnedSystemIndices.push_back(i);
      result.m_tokensKept += estimate_tokens(history[i].m_content);
    }
  }

  std::vector<std::size_t> keptNonSystemIndices;
  keptNonSystemIndices.reserve(history.size());

  for (std::size_t i = history.size(); i-- > 0;) {
    if (history[i].m_role == Role::System) {
      continue;
    }
    const std::size_t messageTokens = estimate_tokens(history[i].m_content);
    if (result.m_tokensKept + messageTokens > tokenBudget) {
      result.m_truncated = true;
      continue;
    }
    result.m_tokensKept += messageTokens;
    keptNonSystemIndices.push_back(i);
  }

  std::unordered_set<std::size_t> keep;
  for (std::size_t idx : pinnedSystemIndices) {
    keep.insert(idx);
  }
  for (std::size_t idx : keptNonSystemIndices) {
    keep.insert(idx);
  }

  result.m_messages.reserve(keep.size());
  for (std::size_t i = 0; i < history.size(); ++i) {
    if (keep.find(i) != keep.end()) {
      result.m_messages.push_back(history[i]);
    } else {
      result.m_truncated = true;
    }
  }

  return result;
}

}  // namespace sentra
