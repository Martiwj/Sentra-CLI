#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "sentra/types.hpp"

namespace sentra {

class ModelRegistry {
 public:
  static ModelRegistry load_from_tsv(const std::string& path, const std::string& preferredModelId);

  const std::vector<ModelSpec>& models() const;
  std::optional<std::reference_wrapper<const ModelSpec>> active_model() const;
  std::optional<std::reference_wrapper<const ModelSpec>> find_model(const std::string& modelId) const;
  bool set_active_model(const std::string& modelId, std::string& error);
  bool add_model(ModelSpec model, std::string& error);

 private:
  std::vector<ModelSpec> m_models;
  std::size_t m_activeIndex{0};
};

}  // namespace sentra
