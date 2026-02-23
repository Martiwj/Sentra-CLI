#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdlib>

#include "sentra/context_window.hpp"
#include "sentra/model_registry.hpp"
#include "sentra/session_store.hpp"
#include "sentra/types.hpp"

namespace fs = std::filesystem;

namespace {

void assert_true(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string make_temp_dir(const std::string& prefix) {
  const std::string dir = (fs::temp_directory_path() / (prefix + std::to_string(std::rand()))).string();
  fs::create_directories(dir);
  return dir;
}

void test_model_registry_parsing_and_switching() {
  const std::string dir = make_temp_dir("sentra-registry-");
  const std::string path = dir + "/models.tsv";
  std::ofstream out(path);
  out << "a\tModel A\trepo/a\tfile-a.gguf\t./models/a.gguf\n";
  out << "b\tModel B\trepo/b\tfile-b.gguf\t./models/b.gguf\n";
  out.close();

  sentra::ModelRegistry registry = sentra::ModelRegistry::load_from_tsv(path, "b");
  assert_true(registry.active_model() != nullptr, "active model should exist");
  assert_true(registry.active_model()->id == "b", "preferred model id should be selected");

  std::string error;
  assert_true(registry.set_active_model("a", error), "switch to model a should succeed");
  assert_true(registry.active_model()->id == "a", "active model should be a after switch");
  assert_true(registry.add_model({"c", "Model C", "repo/c", "file-c.gguf", "./models/c.gguf"}, error),
              "adding model c should succeed");
  assert_true(registry.find_model("c") != nullptr, "added model c should be findable");
  assert_true(!registry.add_model({"c", "Duplicate C", "repo/c2", "file-c2.gguf", "./models/c2.gguf"}, error),
              "duplicate id should fail");
  assert_true(!registry.set_active_model("missing", error), "unknown model should fail");
  assert_true(error.find("unknown model id") != std::string::npos, "error should mention unknown model");

  fs::remove_all(dir);
}

void test_session_store_encoding_and_metadata() {
  const std::string dir = make_temp_dir("sentra-session-");
  sentra::SessionStore store(dir);
  const std::string session_id = "session-test";

  store.ensure_session(session_id, "model-x", "mock");
  store.append(session_id, {sentra::Role::System, "sys\tline\nnext"});
  store.append(session_id, {sentra::Role::User, "hello"});
  store.update_metadata(session_id, "model-y", "local-binary");

  const std::vector<sentra::Message> loaded = store.load(session_id);
  assert_true(loaded.size() == 2, "two messages should load");
  assert_true(loaded[0].role == sentra::Role::System, "first role should be system");
  assert_true(loaded[0].content == "sys\tline\nnext", "escaped content should round-trip");
  assert_true(loaded[1].content == "hello", "user content should round-trip");

  const auto metadata = store.load_metadata(session_id);
  assert_true(metadata.has_value(), "metadata should exist");
  assert_true(metadata->active_model_id == "model-y", "metadata should keep latest model id");
  assert_true(metadata->runtime_name == "local-binary", "metadata should keep runtime");

  const auto listed = store.list_sessions();
  assert_true(!listed.empty(), "session list should not be empty");

  fs::remove_all(dir);
}

void test_context_pruning() {
  std::vector<sentra::Message> history = {
      {sentra::Role::System, "You are system prompt and should stay."},
      {sentra::Role::User, "old context old context old context"},
      {sentra::Role::Assistant, "older answer older answer older answer"},
      {sentra::Role::User, "recent question"},
      {sentra::Role::Assistant, "recent answer"},
      {sentra::Role::User, "latest user query"},
  };

  const sentra::ContextPruneResult pruned = sentra::prune_context_window(history, 12);
  assert_true(!pruned.messages.empty(), "pruned history should not be empty");
  assert_true(pruned.messages.front().role == sentra::Role::System, "system message should remain pinned");
  assert_true(pruned.messages.back().content == "latest user query", "latest context should be preserved");
  assert_true(pruned.truncated, "history should be marked truncated when budget is tight");
}

}  // namespace

int main() {
  try {
    test_model_registry_parsing_and_switching();
    test_session_store_encoding_and_metadata();
    test_context_pruning();
    std::cout << "sentra_tests: all tests passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "sentra_tests: failure: " << ex.what() << "\n";
    return 1;
  }
}
