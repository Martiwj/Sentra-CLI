#include "sentra/model_registry.hpp"

#include <functional>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace sentra
{
  namespace
  {

    std::string trim(const std::string &value)
    {
      const auto start = value.find_first_not_of(" \t\r\n");
      if (start == std::string::npos)
      {
        return "";
      }
      const auto end = value.find_last_not_of(" \t\r\n");
      return value.substr(start, end - start + 1);
    }

    std::vector<std::string> split_tsv(const std::string &line)
    {
      std::vector<std::string> cols;
      std::string col;
      std::istringstream in(line);
      while (std::getline(in, col, '\t'))
      {
        cols.push_back(trim(col));
      }
      return cols;
    }

  } // namespace

  ModelRegistry ModelRegistry::load_from_tsv(const std::string &path, const std::string &preferredModelId)
  {
    ModelRegistry registry;

    std::ifstream in(path);
    if (!in.is_open())
    {
      throw std::runtime_error("failed to open models registry: " + path);
    }

    std::string line;
    while (std::getline(in, line))
    {
      line = trim(line);
      if (line.empty() || line[0] == '#')
      {
        continue;
      }

      const std::vector<std::string> cols = split_tsv(line);
      if (cols.size() < 5)
      {
        continue;
      }

      ModelSpec model;
      model.m_id = cols[0];
      model.m_name = cols[1];
      model.m_hfRepo = cols[2];
      model.m_hfFile = cols[3];
      model.m_localPath = cols[4];

      if (!model.m_id.empty())
      {
        registry.m_models.push_back(std::move(model));
      }
    }

    if (registry.m_models.empty())
    {
      throw std::runtime_error("models registry is empty: " + path);
    }

    registry.m_activeIndex = 0;
    if (!preferredModelId.empty())
    {
      for (std::size_t i = 0; i < registry.m_models.size(); ++i)
      {
        if (registry.m_models[i].m_id == preferredModelId)
        {
          registry.m_activeIndex = i;
          break;
        }
      }
    }

    return registry;
  }

  const std::vector<ModelSpec> &ModelRegistry::models() const { return m_models; }

  std::optional<std::reference_wrapper<const ModelSpec>> ModelRegistry::active_model() const
  {
    if (m_models.empty() || m_activeIndex >= m_models.size())
    {
      return std::nullopt;
    }
    return std::cref(m_models[m_activeIndex]);
  }

  std::optional<std::reference_wrapper<const ModelSpec>> ModelRegistry::find_model(
      const std::string &modelId) const
  {
    for (const auto &model : m_models)
    {
      if (model.m_id == modelId)
      {
        return std::cref(model);
      }
    }
    return std::nullopt;
  }

  bool ModelRegistry::set_active_model(const std::string &modelId, std::string &error)
  {
    for (std::size_t i = 0; i < m_models.size(); ++i)
    {
      if (m_models[i].m_id == modelId)
      {
        m_activeIndex = i;
        error.clear();
        return true;
      }
    }

    error = "unknown model id: " + modelId;
    return false;
  }

  bool ModelRegistry::add_model(ModelSpec model, std::string &error)
  {
    if (model.m_id.empty() || model.m_hfRepo.empty() || model.m_hfFile.empty() || model.m_localPath.empty())
    {
      error = "model requires non-empty id, hf_repo, hf_file, and local_path";
      return false;
    }
    if (find_model(model.m_id).has_value())
    {
      error = "model id already exists: " + model.m_id;
      return false;
    }
    if (model.m_name.empty())
    {
      model.m_name = model.m_id;
    }
    m_models.push_back(std::move(model));
    error.clear();
    return true;
  }

} // namespace sentra
