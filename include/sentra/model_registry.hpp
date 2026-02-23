#pragma once

#include <string>
#include <vector>

#include "sentra/types.hpp"

namespace sentra {

class ModelRegistry {
 public:
  static ModelRegistry load_from_tsv(const std::string& path, const std::string& preferred_model_id);

  const std::vector<ModelSpec>& models() const;
  const ModelSpec* active_model() const;
  const ModelSpec* find_model(const std::string& model_id) const;
  bool set_active_model(const std::string& model_id, std::string& error);

 private:
  std::vector<ModelSpec> models_;
  std::size_t active_index_{0};
};

}  // namespace sentra
