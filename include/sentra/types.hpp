#pragma once

#include <string>
#include <vector>

namespace sentra {

enum class Role {
  System,
  User,
  Assistant,
};

std::string role_to_string(Role role);
Role role_from_string(const std::string& value);

struct Message {
  Role m_role;
  std::string m_content;
};

struct GenerationRequest {
  std::vector<Message> m_messages;
  std::string m_modelId;
  std::string m_modelPath;
  std::size_t m_maxTokens{256};
};

struct GenerationResult {
  std::string m_text;
  bool m_contextTruncated{false};
  std::string m_warning;
  double m_firstTokenMs{0.0};
  double m_totalMs{0.0};
  std::size_t m_generatedTokens{0};
  double m_tokensPerSecond{0.0};
};

struct ModelSpec {
  std::string m_id;
  std::string m_name;
  std::string m_hfRepo;
  std::string m_hfFile;
  std::string m_localPath;
};

struct SessionMetadata {
  std::string m_sessionId;
  long long m_createdAtEpoch{0};
  std::string m_activeModelId;
  std::string m_runtimeName;
};

}  // namespace sentra
