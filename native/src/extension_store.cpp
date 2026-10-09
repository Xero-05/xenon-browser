#include "xenon/extension_store.hpp"
#include "xenon/local_security.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <stdexcept>

namespace xenon {
namespace {
namespace fs = std::filesystem;
std::string field(const Json& value, const char* key) { auto found = value.find(key); return found != value.end() && found->is_string() ? found->get<std::string>() : std::string{}; }
// Reparse points include symbolic links and directory junctions, which the
// standard library does not always report as links on Windows.
bool linked(const fs::path& path) {
#ifdef _WIN32
  const auto attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) throw std::runtime_error("An extension file could not be inspected.");
  return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
  return fs::is_symlink(fs::symlink_status(path));
#endif
}
std::string read_bounded(const fs::path& path, std::uintmax_t limit) {
  if (linked(path) || !fs::is_regular_file(fs::symlink_status(path)) || fs::file_size(path) > limit) throw std::runtime_error("The extension manifest is missing or too large.");
  std::ifstream file(path, std::ios::binary); return {std::istreambuf_iterator<char>(file), {}};
}
std::string display_text(std::string value, size_t limit) {
  std::erase_if(value, [](unsigned char c) { return c < 0x20 || c == 0x7f; });
  if (value.size() > limit) { while (limit && (static_cast<unsigned char>(value[limit]) & 0xc0) == 0x80) --limit; value = value.substr(0, limit); }
  return value;
}
// Extension-relative page such as "options.html" or "popup/index.html".
std::string relative_page(std::string value) {
  while (value.starts_with("./")) value.erase(0, 2);
  if (value.empty() || value.size() > 512 || value.starts_with("/") || value.find("..") != std::string::npos ||
      value.find_first_of(":\\?#") != std::string::npos || std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 0x20; })) return {};
  return value;
}
std::string localized(const fs::path& folder, const Json& manifest, const std::string& value) {
  if (!value.starts_with("__MSG_") || !value.ends_with("__") || value.size() <= 8) return value;
  const auto locale = field(manifest, "default_locale");
  if (locale.empty() || locale.size() > 32 || !std::all_of(locale.begin(), locale.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-'; })) return value;
  try {
    const auto messages = Json::parse(read_bounded(folder / "_locales" / locale / "messages.json", 1024 * 1024), nullptr, false, true);
    if (!messages.is_object()) return value;
    auto key = value.substr(6, value.size() - 8); std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const auto& [name, entry] : messages.items()) {
      auto lower = name; std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (lower == key && entry.is_object()) { const auto message = field(entry, "message"); if (!message.empty()) return message; }
    }
  } catch (const std::exception&) {}
  return value;
}
struct CopyBudget { size_t files{}; std::uintmax_t bytes{}; };
void copy_tree(const fs::path& from, const fs::path& to, CopyBudget& budget, size_t depth) {
  if (depth > ExtensionStore::max_depth) throw std::runtime_error("The extension folder is nested too deeply.");
  fs::create_directory(to);
  for (const auto& entry : fs::directory_iterator(from)) {
    const auto& path = entry.path();
    if (linked(path)) throw std::runtime_error("Extension folders cannot contain links or junctions.");
    const auto status = fs::symlink_status(path);
    if (++budget.files > ExtensionStore::max_files) throw std::runtime_error("The extension folder contains too many files.");
    if (fs::is_directory(status)) { copy_tree(path, to / path.filename(), budget, depth + 1); continue; }
    if (!fs::is_regular_file(status)) throw std::runtime_error("Extension folders can contain only ordinary files and folders.");
    budget.bytes += fs::file_size(path);
    if (budget.bytes > ExtensionStore::max_bytes) throw std::runtime_error("The extension folder is larger than 128 MiB.");
    fs::copy_file(path, to / path.filename());
    // A read-only source attribute would make the managed copy undeletable.
    fs::permissions(to / path.filename(), fs::perms::owner_write, fs::perm_options::add);
  }
}
// Deletes only a tree Xenon created, refusing any link inside it.
bool remove_tree(const fs::path& path, size_t& budget, size_t depth = 0) {
  if (depth > ExtensionStore::max_depth + 1 || linked(path)) return false;
  for (const auto& entry : fs::directory_iterator(path)) {
    if (++budget > ExtensionStore::max_files * 2) return false;
    if (linked(entry.path())) return false;
    if (fs::is_directory(fs::symlink_status(entry.path()))) { if (!remove_tree(entry.path(), budget, depth + 1)) return false; }
    else { std::error_code ignored; fs::permissions(entry.path(), fs::perms::owner_write, fs::perm_options::add, ignored); fs::remove(entry.path()); }
  }
  return fs::remove(path);
}
bool within(const fs::path& child, const fs::path& parent) {
  if (parent.empty()) return false;
  const auto relative = child.lexically_relative(parent);
  return !relative.empty() && *relative.begin() != "..";
}
bool managed_folder(const std::string& folder) {
  return folder.size() == 16 && std::all_of(folder.begin(), folder.end(), [](unsigned char c) { return std::isdigit(c) || (c >= 'a' && c <= 'f'); });
}
std::optional<std::string> base64(const std::string& text) {
  std::string out; unsigned value = 0; int bits = -8;
  for (unsigned char c : text) {
    int digit = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1;
    if (c == '=' || std::isspace(c)) continue;
    if (digit < 0) return std::nullopt;
    value = (value << 6) | static_cast<unsigned>(digit); bits += 6;
    if (bits >= 0) { out.push_back(static_cast<char>((value >> bits) & 0xff)); bits -= 8; }
  }
  return out;
}
void write_atomic(const fs::path& target, const std::string& bytes) {
  auto temporary = target; temporary += ".tmp";
  { std::ofstream file(temporary, std::ios::binary | std::ios::trunc); file << bytes; file.flush(); if (!file) throw std::runtime_error("Extension settings could not be saved."); }
  local_security::restrict_path(temporary);
#ifdef _WIN32
  if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Extension settings could not be saved.");
#else
  fs::rename(temporary, target);
#endif
}
}

Json ExtensionRecord::json() const {
  return {{"id", id}, {"name", name}, {"version", version}, {"folder", folder}, {"optionsPage", options_page}, {"popupPage", popup_page}, {"enabled", enabled}};
}

ExtensionStore::ExtensionStore(fs::path data_root) : root_(std::move(data_root) / "extensions") {
  try {
    const auto registry = root_ / "extensions.json";
    if (!fs::exists(registry)) return;
    const auto value = Json::parse(read_bounded(registry, 1024 * 1024), nullptr, false);
    if (!value.is_object() || value.value("version", 0) != 1) return;
    for (const auto& entry : value.value("extensions", Json::array())) {
      ExtensionRecord record{field(entry, "id"), field(entry, "name"), field(entry, "version"), field(entry, "folder"),
        relative_page(field(entry, "optionsPage")), relative_page(field(entry, "popupPage")), entry.value("enabled", false)};
      if (record.id.size() == 32 && managed_folder(record.folder) && records_.size() < max_extensions) records_.push_back(std::move(record));
    }
    for (const auto& folder : value.value("pendingDeletion", Json::array())) if (folder.is_string() && managed_folder(folder.get<std::string>())) pending_deletion_.push_back(folder.get<std::string>());
    purge_pending();
  } catch (const std::exception&) { records_.clear(); pending_deletion_.clear(); }
}

void ExtensionStore::purge_pending() {
  std::vector<std::string> remaining;
  for (const auto& folder : pending_deletion_) {
    const auto path = root_ / folder; size_t budget{};
    try { if (fs::exists(path) && !remove_tree(path, budget)) remaining.push_back(folder); } catch (const std::exception&) { remaining.push_back(folder); }
  }
  if (remaining.size() != pending_deletion_.size()) { pending_deletion_ = std::move(remaining); try { save(); } catch (const std::exception&) {} }
}

void ExtensionStore::save() const {
  Json extensions = Json::array(); for (const auto& record : records_) extensions.push_back(record.json());
  fs::create_directories(root_); local_security::restrict_path(root_);
  write_atomic(root_ / "extensions.json", Json{{"version", 1}, {"extensions", extensions}, {"pendingDeletion", pending_deletion_}}.dump(2));
}

std::vector<ExtensionRecord> ExtensionStore::list() const { return records_; }
std::optional<ExtensionRecord> ExtensionStore::find(const std::string& id) const {
  for (const auto& record : records_) if (record.id == id) return record;
  return std::nullopt;
}
fs::path ExtensionStore::directory(const ExtensionRecord& record) const { return fs::absolute(root_ / record.folder).lexically_normal(); }

std::string ExtensionStore::generate_id(const std::string& bytes) {
  const auto digest = local_security::sha256(bytes);
  std::string id; for (size_t n = 0; n < 32; ++n) { const char c = digest[n]; id.push_back(static_cast<char>('a' + (c <= '9' ? c - '0' : c - 'a' + 10))); }
  return id;
}
std::string ExtensionStore::unpacked_id(const fs::path& folder) {
#ifdef _WIN32
  // Chromium hashes the UTF-16 path and normalizes a lowercase drive letter.
  auto text = folder.wstring();
  if (text.size() >= 2 && text[1] == L':') text[0] = static_cast<wchar_t>(std::towupper(text[0]));
  return generate_id(std::string(reinterpret_cast<const char*>(text.data()), text.size() * sizeof(wchar_t)));
#else
  return generate_id(folder.string());
#endif
}
std::optional<std::string> ExtensionStore::key_id(const std::string& key) {
  const auto bytes = base64(key);
  if (!bytes || bytes->size() < 64 || bytes->size() > 4096) return std::nullopt;
  return generate_id(*bytes);
}

ExtensionRecord ExtensionStore::install_unpacked(const fs::path& chosen) {
  if (records_.size() >= max_extensions) throw std::runtime_error("Remove an extension before adding another.");
  std::error_code error; const auto source = fs::canonical(chosen, error);
  if (error || !fs::is_directory(source) || linked(source)) throw std::runtime_error("Choose the extension's folder, the one that contains manifest.json.");
  if (within(source, fs::weakly_canonical(root_.parent_path(), error)))
    throw std::runtime_error("Choose an extension folder outside Xenon's data directory.");
  const auto manifest = Json::parse(read_bounded(source / "manifest.json", 1024 * 1024), nullptr, false, true);
  if (!manifest.is_object()) throw std::runtime_error("manifest.json is not valid JSON.");
  const auto version = manifest.value("manifest_version", 0);
  if (version == 2) throw std::runtime_error("Manifest V2 extensions are no longer supported by Chromium. Use the extension's Manifest V3 version.");
  if (version != 3) throw std::runtime_error("Only Manifest V3 extensions are supported.");
  ExtensionRecord record;
  record.name = display_text(localized(source, manifest, field(manifest, "name")), 120);
  record.version = display_text(field(manifest, "version"), 32);
  if (record.name.empty() || record.version.empty()) throw std::runtime_error("The extension manifest needs a name and version.");
  record.options_page = relative_page(field(manifest.value("options_ui", Json::object()), "page"));
  if (record.options_page.empty()) record.options_page = relative_page(field(manifest, "options_page"));
  record.popup_page = relative_page(field(manifest.value("action", Json::object()), "default_popup"));
  if (manifest.contains("key") && !manifest["key"].is_string()) throw std::runtime_error("The extension manifest key is invalid.");
  std::optional<std::string> keyed;
  if (manifest.contains("key")) { keyed = key_id(manifest["key"].get<std::string>()); if (!keyed) throw std::runtime_error("The extension manifest key is invalid."); }
  fs::create_directories(root_); local_security::restrict_path(root_);
  std::string folder; do folder = local_security::random_hex(8); while (fs::exists(root_ / folder) || fs::exists(root_ / (folder + ".partial")));
  const auto partial = root_ / (folder + ".partial"), target = root_ / folder;
  try {
    CopyBudget budget; copy_tree(source, partial, budget, 0);
    // The copied manifest is what Chromium loads; recheck it is unchanged.
    if (Json::parse(read_bounded(partial / "manifest.json", 1024 * 1024), nullptr, false, true) != manifest) throw std::runtime_error("The extension changed while it was being copied. Try again.");
    fs::rename(partial, target);
  } catch (...) { size_t budget{}; try { if (fs::exists(partial)) remove_tree(partial, budget); } catch (...) {} throw; }
  record.folder = folder;
  record.id = keyed ? *keyed : unpacked_id(directory(record));
  if (find(record.id)) { size_t budget{}; try { remove_tree(target, budget); } catch (...) {} throw std::runtime_error("This extension is already installed. Remove it before adding it again."); }
  records_.push_back(record);
  try { save(); } catch (...) { records_.pop_back(); size_t budget{}; try { remove_tree(target, budget); } catch (...) {} throw; }
  return record;
}

bool ExtensionStore::set_enabled(const std::string& id, bool enabled) {
  for (auto& record : records_) if (record.id == id) {
    const auto previous = record.enabled; record.enabled = enabled;
    try { save(); return true; } catch (const std::exception&) { record.enabled = previous; return false; }
  }
  return false;
}

bool ExtensionStore::remove(const std::string& id) {
  const auto found = std::find_if(records_.begin(), records_.end(), [&](const auto& record) { return record.id == id; });
  if (found == records_.end()) return false;
  const auto record = *found; records_.erase(found);
  // A loaded extension's files can be open until restart; delete them then.
  pending_deletion_.push_back(record.folder);
  try { save(); } catch (const std::exception&) { records_.push_back(record); pending_deletion_.pop_back(); return false; }
  if (!loaded_.contains(record.id)) purge_pending();
  return true;
}

std::vector<fs::path> ExtensionStore::startup_paths() {
  std::vector<fs::path> paths;
  for (const auto& record : records_) {
    if (!record.enabled) continue;
    const auto path = directory(record);
    // Chromium separates --load-extension folders with commas.
    std::error_code error;
    if (path.native().find(fs::path(",").native()) != fs::path::string_type::npos || !fs::is_regular_file(path / "manifest.json", error)) continue;
    paths.push_back(path); loaded_.insert(record.id);
  }
  return paths;
}
std::set<std::string> ExtensionStore::active_ids() const {
  std::set<std::string> ids; for (const auto& record : records_) if (record.enabled && loaded_.contains(record.id)) ids.insert(record.id);
  return ids;
}
bool ExtensionStore::active(const std::string& id) const { return active_ids().contains(id); }
}
