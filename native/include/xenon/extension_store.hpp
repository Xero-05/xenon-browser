#pragma once
#include "xenon/contracts.hpp"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace xenon {
// Native human UI only. Extensions are never installed, listed, enabled or
// opened through MCP or the broker. Chromium loads the enabled set once, when
// the browser process starts; changes apply after the next restart.
struct ExtensionRecord {
  std::string id, name, version, folder, options_page, popup_page;
  bool enabled{true};
  Json json() const;
};

class ExtensionStore {
 public:
  static constexpr size_t max_extensions = 64, max_files = 4096, max_depth = 32;
  static constexpr std::uintmax_t max_bytes = 128ull * 1024 * 1024;
  // data_root is Xenon's user-data directory. Managed copies live in its
  // extensions child; profile directories and the vault are never touched.
  explicit ExtensionStore(std::filesystem::path data_root);
  std::vector<ExtensionRecord> list() const;
  std::optional<ExtensionRecord> find(const std::string& id) const;
  // Validates a Manifest V3 folder chosen by the human and copies it into
  // managed storage. Links, reparse points and oversize trees are refused.
  // Throws std::runtime_error with a user-facing message; nothing is left
  // half-installed on failure.
  ExtensionRecord install_unpacked(const std::filesystem::path& source);
  bool set_enabled(const std::string& id, bool enabled);
  bool remove(const std::string& id);
  // Called once before Chromium starts. Returns the folders to load and
  // records them as this session's active set.
  std::vector<std::filesystem::path> startup_paths();
  // Loaded at startup and still installed and enabled.
  std::set<std::string> active_ids() const;
  bool active(const std::string& id) const;
  std::filesystem::path directory(const ExtensionRecord& record) const;
  // Chromium's extension ID: the first 128 bits of SHA-256, written a-p.
  static std::string generate_id(const std::string& bytes);
  // ID Chromium assigns an unpacked folder without a manifest key.
  static std::string unpacked_id(const std::filesystem::path& absolute_folder);
  static std::optional<std::string> key_id(const std::string& base64_key);
 private:
  void save() const;
  void purge_pending();
  std::filesystem::path root_;
  std::vector<ExtensionRecord> records_;
  std::vector<std::string> pending_deletion_;
  std::set<std::string> loaded_;
};
}
