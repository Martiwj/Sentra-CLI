#pragma once

#include <functional>
#include <string>

#include "sentra/types.hpp"

namespace sentra {

using StreamCallback = std::function<void(const std::string&)>;

class IModelRuntime {
 public:
  virtual ~IModelRuntime() = default;
  virtual std::string name() const = 0;
  virtual bool is_available() const = 0;
  virtual GenerationResult generate(const GenerationRequest& request, StreamCallback on_token) = 0;
};

}  // namespace sentra
