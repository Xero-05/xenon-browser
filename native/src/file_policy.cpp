#include "xenon/file_policy.hpp"
#include "xenon/local_security.hpp"
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <vector>

namespace xenon {
namespace {
class Handle {
 public:
  HANDLE value{INVALID_HANDLE_VALUE};
  explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) : value(handle) {}
  ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
  Handle(Handle&& other) noexcept : value(other.value) { other.value = INVALID_HANDLE_VALUE; }
  Handle& operator=(Handle&& other) noexcept {
    if (this != &other) { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); value = other.value; other.value = INVALID_HANDLE_VALUE; }
    return *this;
  }
  Handle(const Handle&) = delete;
};
std::string utf8(const std::wstring& value) {
  const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (!length && !value.empty()) throw std::runtime_error("Unsupported filename");
  std::string result(length, '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
  return result;
}
std::wstring widen(const std::string& value) {
  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
  if (!length && !value.empty()) return L"download";
  std::wstring result(length, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), length);
  return result;
}
std::wstring folded(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  return value;
}
bool same_path(const std::filesystem::path& a, const std::filesystem::path& b) {
  const auto aa = a.native(), bb = b.native();
  return CompareStringOrdinal(aa.data(), static_cast<int>(aa.size()), bb.data(), static_cast<int>(bb.size()), TRUE) == CSTR_EQUAL;
}
bool within(const std::filesystem::path& target, const std::filesystem::path& root) {
  auto ti = target.begin();
  for (auto ri = root.begin(); ri != root.end(); ++ri, ++ti)
    if (ti == target.end() || !same_path(*ti, *ri)) return false;
  return true;
}
bool native_path_allowed(const std::filesystem::path& path) {
  const auto value = path.native();
  if (value.size() < 3 || value.size() > 30000 || !std::iswalpha(value[0]) || value[1] != L':' ||
      (value[2] != L'\\' && value[2] != L'/') || value.find(L'\0') != std::wstring::npos) return false;
  if (value.find(L':', 2) != std::wstring::npos) return false; // includes alternate data streams
  for (const auto& part : path.relative_path()) {
    const auto name = part.native();
    if (name.empty() || name == L"." || name == L".." || name.back() == L'.' || name.back() == L' ') return false;
    if (std::any_of(name.begin(), name.end(), [](wchar_t c) { return c < 32 || c == L'<' || c == L'>' || c == L'"' || c == L'|' || c == L'?' || c == L'*'; })) return false;
  }
  return true;
}
std::filesystem::path final_path(HANDLE handle) {
  const auto length = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (!length) throw std::runtime_error("File identity unavailable");
  std::wstring name(length, L'\0');
  const auto written = GetFinalPathNameByHandleW(handle, name.data(), length, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (!written || written >= length) throw std::runtime_error("File identity unavailable");
  name.resize(written);
  if (name.starts_with(L"\\\\?\\UNC\\")) throw std::runtime_error("Network files are unsupported");
  if (name.starts_with(L"\\\\?\\")) name.erase(0, 4);
  const std::filesystem::path result(name);
  if (!native_path_allowed(result)) throw std::runtime_error("Unsupported file location");
  return result;
}
struct FileIdentity {
  DWORD volume{}, high{}, low{};
  bool operator==(const FileIdentity&) const = default;
};
struct Opened {
  Handle file;
  std::vector<Handle> ancestors;
  std::filesystem::path canonical;
  FileIdentity identity;
  uint64_t size{};
};
// Pairing files may be saved outside the profile, including inside a granted
// folder. Inspect the locked contents on every use, not only at approval time.
// A SAX reader keeps credential strings out of a retained JSON document.
struct PairingConfigProbe : nlohmann::json_sax<Json> {
  int depth{};
  enum class Field { none, client, token } field{Field::none};
  bool client{}, token{};
  bool null() override { field = Field::none; return true; }
  bool boolean(bool) override { field = Field::none; return true; }
  bool number_integer(number_integer_t) override { field = Field::none; return true; }
  bool number_unsigned(number_unsigned_t) override { field = Field::none; return true; }
  bool number_float(number_float_t, const string_t&) override { field = Field::none; return true; }
  bool string(string_t& value) override {
    if (depth == 1 && field == Field::client && !value.empty()) client = true;
    if (depth == 1 && field == Field::token && value.size() == 64 &&
        std::all_of(value.begin(), value.end(), [](unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); })) token = true;
    if (!value.empty()) SecureZeroMemory(value.data(), value.size());
    value.clear(); field = Field::none; return true;
  }
  bool binary(binary_t& value) override { if (!value.empty()) SecureZeroMemory(value.data(), value.size()); value.clear(); field = Field::none; return true; }
  bool start_object(std::size_t) override { ++depth; field = Field::none; return true; }
  bool key(string_t& value) override {
    field = depth == 1 && value == "clientId" ? Field::client : depth == 1 && value == "token" ? Field::token : Field::none;
    return true;
  }
  bool end_object() override { --depth; field = Field::none; return true; }
  bool start_array(std::size_t) override { ++depth; field = Field::none; return true; }
  bool end_array() override { --depth; field = Field::none; return true; }
  bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override { return false; }
};
struct WipedBytes {
  std::vector<unsigned char> value;
  ~WipedBytes() { if (!value.empty()) SecureZeroMemory(value.data(), value.size()); }
};
bool pairing_config(const Opened& opened) {
  const auto name = folded(opened.canonical.filename().native());
  if ((name.ends_with(L".json") && (name.find(L"client-config") != std::wstring::npos || name.find(L"clientconfig") != std::wstring::npos)) ||
      name.ends_with(L".clientconfig")) return true;
  constexpr DWORD probe_limit = 1024 * 1024;
  WipedBytes bytes;
  bytes.value.resize(static_cast<size_t>(std::min<uint64_t>(opened.size, probe_limit)));
  DWORD count{};
  LARGE_INTEGER beginning{};
  if (!SetFilePointerEx(opened.file.value, beginning, nullptr, FILE_BEGIN) ||
      (!bytes.value.empty() && (!ReadFile(opened.file.value, bytes.value.data(), static_cast<DWORD>(bytes.value.size()), &count, nullptr) || count != bytes.value.size())))
    throw std::runtime_error("File contents are unavailable");
  auto start = bytes.value.begin();
  if (bytes.value.size() >= 3 && bytes.value[0] == 0xef && bytes.value[1] == 0xbb && bytes.value[2] == 0xbf) start += 3;
  while (start != bytes.value.end() && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')) ++start;
  // Large JSON objects cannot be inspected within the bounded transfer probe.
  // Deny them rather than allowing a config to hide its token beyond the cap.
  if (start == bytes.value.end()) return opened.size > probe_limit;
  if (*start != '{') return false;
  if (opened.size > probe_limit) return true;
  PairingConfigProbe probe;
  Json::sax_parse(start, bytes.value.end(), &probe);
  return probe.client && probe.token;
}
Opened open_path(const std::filesystem::path& path, bool directory = false) {
  if (!native_path_allowed(path)) throw std::runtime_error("Unsupported file location");
  Opened result;
  // Lock every ancestor against rename/reparse while CEF consumes this path.
  // OPEN_REPARSE_POINT also detects junctions before traversing the next level.
  std::filesystem::path parent = path.root_path();
  const auto relative_parent = path.parent_path().relative_path();
  auto open_parent = [&](const std::filesystem::path& current) {
    Handle handle(CreateFileW(current.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (handle.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle.value, &info) ||
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
      throw std::runtime_error("Upload directories must be ordinary local directories");
    result.ancestors.push_back(std::move(handle));
  };
  open_parent(parent);
  for (const auto& component : relative_parent) { parent /= component; open_parent(parent); }
  result.file = Handle(CreateFileW(path.c_str(), directory ? FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES : GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_FLAG_SEQUENTIAL_SCAN), nullptr));
  BY_HANDLE_FILE_INFORMATION info{};
  if (result.file.value == INVALID_HANDLE_VALUE || GetFileType(result.file.value) != FILE_TYPE_DISK ||
      !GetFileInformationByHandle(result.file.value, &info) ||
      (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
      (!!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory) || (!directory && info.nNumberOfLinks != 1))
    throw std::runtime_error("Transfer grants require ordinary local files and directories without links");
  result.canonical = final_path(result.file.value);
  if (!same_path(result.canonical, path.lexically_normal())) throw std::runtime_error("File path changed");
  result.identity = {info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow};
  result.size = static_cast<uint64_t>(info.nFileSizeHigh) << 32 | info.nFileSizeLow;
  return result;
}
Opened open_regular(const std::filesystem::path& path) {
  auto opened = open_path(path);
  if (pairing_config(opened)) throw std::runtime_error("Connection credential files cannot be transferred");
  return opened;
}
std::filesystem::path canonical_root(const std::filesystem::path& path) {
  if (!native_path_allowed(path)) throw std::runtime_error("Unsupported protected directory");
  Handle handle(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
  if (handle.value != INVALID_HANDLE_VALUE) return final_path(handle.value);
  // A not-yet-created protected directory is still forbidden by lexical path.
  return path.lexically_normal();
}
}

struct UploadFile::Impl { Opened opened; };
UploadFile::UploadFile() = default;
UploadFile::~UploadFile() = default;
UploadFile::UploadFile(UploadFile&&) noexcept = default;
UploadFile& UploadFile::operator=(UploadFile&&) noexcept = default;
const std::filesystem::path& UploadFile::path() const {
  if (!impl_) throw std::runtime_error("Upload capability is empty");
  return impl_->opened.canonical;
}

struct FilePolicy::Impl {
  struct Grant { std::string scope; std::filesystem::path path; FileIdentity identity; std::string folder; };
  std::filesystem::path downloads;
  std::filesystem::path policy_file;
  std::vector<std::filesystem::path> protected_roots;
  std::vector<std::filesystem::path> denied_sources;
  std::vector<FileIdentity> denied_source_ids;
  std::map<std::string, Grant> grants;
  std::map<std::string, Grant> folders;
  std::map<std::string, std::filesystem::path> download_scopes;
  mutable std::mutex mutex;
  bool denied(const std::filesystem::path& path, const FileIdentity& identity) const {
    for (const auto& root : protected_roots) if (within(path, root)) return true;
    for (const auto& source : denied_sources) if (same_path(path, source)) return true;
    for (const auto& source_id : denied_source_ids) if (identity == source_id) return true;
    return false;
  }
  void persist_state() const {
    Json state{{"version", 2}, {"paths", Json::array()}, {"identities", Json::array()}, {"files", Json::array()}, {"folders", Json::array()}};
    for (const auto& path : denied_sources) state["paths"].push_back(utf8(path.native()));
    for (const auto& id : denied_source_ids) state["identities"].push_back({{"volume", id.volume}, {"high", id.high}, {"low", id.low}});
    for (const auto& [id, grant] : grants) state["files"].push_back({{"id", id}, {"scope", grant.scope}, {"path", utf8(grant.path.native())},
      {"volume", grant.identity.volume}, {"high", grant.identity.high}, {"low", grant.identity.low}, {"folder", grant.folder}});
    for (const auto& [id, grant] : folders) state["folders"].push_back({{"id", id}, {"scope", grant.scope}, {"path", utf8(grant.path.native())},
      {"volume", grant.identity.volume}, {"high", grant.identity.high}, {"low", grant.identity.low}});
    const auto serialized = state.dump();
    if (serialized.size() > 4 * 1024 * 1024) throw std::runtime_error("File policy storage limit reached");
    const auto pending = policy_file.parent_path() / (local_security::random_hex(12) + ".pending");
    {
      local_security::SecurityDescriptor security;
      Handle file(CreateFileW(pending.c_str(), GENERIC_WRITE, 0, &security.attributes, CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr));
      DWORD written{};
      if (file.value == INVALID_HANDLE_VALUE || serialized.size() > MAXDWORD ||
          !WriteFile(file.value, serialized.data(), static_cast<DWORD>(serialized.size()), &written, nullptr) ||
          written != serialized.size() || !FlushFileBuffers(file.value))
        throw std::runtime_error("Sensitive file policy could not be persisted");
    }
    if (!MoveFileExW(pending.c_str(), policy_file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      DeleteFileW(pending.c_str());
      throw std::runtime_error("Sensitive file policy could not be persisted");
    }
  }
};
FilePolicy::FilePolicy(std::filesystem::path download_root, std::vector<std::filesystem::path> protected_roots)
    : impl_(std::make_unique<Impl>()) {
  if (!native_path_allowed(download_root)) throw std::runtime_error("A local download directory is required");
  std::filesystem::create_directories(download_root);
  local_security::restrict_path(download_root);
  impl_->downloads = canonical_root(download_root);
  const auto policy_root = impl_->downloads / L".policy";
  std::filesystem::create_directories(policy_root);
  local_security::restrict_path(policy_root);
  impl_->policy_file = policy_root / L"sensitive-inputs.json";
  impl_->protected_roots.push_back(policy_root);
  for (const auto& root : protected_roots) {
    impl_->protected_roots.push_back(root.lexically_normal());
    const auto canonical = canonical_root(root);
    if (!same_path(root, canonical)) impl_->protected_roots.push_back(canonical);
  }
  if (std::filesystem::exists(impl_->policy_file)) {
    if (std::filesystem::file_size(impl_->policy_file) > 4 * 1024 * 1024) throw std::runtime_error("Sensitive file policy is invalid");
    std::ifstream input(impl_->policy_file, std::ios::binary);
    const auto state = Json::parse(input);
    if (state.at("version") != 1 && state.at("version") != 2) throw std::runtime_error("Sensitive file policy version is unsupported");
    for (const auto& item : state.at("paths")) {
      const auto path = std::filesystem::path(widen(item.get<std::string>()));
      if (!native_path_allowed(path)) throw std::runtime_error("Sensitive file policy path is invalid");
      impl_->denied_sources.push_back(path);
    }
    for (const auto& id : state.at("identities"))
      impl_->denied_source_ids.push_back({id.at("volume").get<DWORD>(), id.at("high").get<DWORD>(), id.at("low").get<DWORD>()});
    for (const auto* kind : {"files", "folders"}) {
      auto& destination = std::string(kind) == "files" ? impl_->grants : impl_->folders;
      for (const auto& item : state.value(kind, Json::array())) {
        const auto path = std::filesystem::path(widen(item.at("path").get<std::string>()));
        if (!native_path_allowed(path)) throw std::runtime_error("File policy path is invalid");
        const auto id = item.at("id").get<std::string>(), scope = item.at("scope").get<std::string>();
        if (id.empty() || scope.empty() || destination.size() > 10000) throw std::runtime_error("File policy grant is invalid");
        destination.emplace(id, Impl::Grant{scope, path, {item.at("volume").get<DWORD>(), item.at("high").get<DWORD>(), item.at("low").get<DWORD>()}, item.value("folder", std::string{})});
      }
    }
  }
}
FilePolicy::~FilePolicy() = default;
Json FilePolicy::grant_upload(const std::string& scope, const std::filesystem::path& native_path) {
  if (scope.empty()) return failure("invalid_scope", "An upload must belong to a workspace.");
  try {
    auto opened = open_regular(native_path);
    std::lock_guard lock(impl_->mutex);
    if (impl_->denied(opened.canonical, opened.identity)) return failure("protected_file", "Browser state and credential import files cannot be uploaded.");
    for (const auto& [id, existing] : impl_->grants)
      if (existing.scope == scope && existing.folder.empty() && existing.identity == opened.identity && same_path(existing.path, opened.canonical))
        return success({{"fileId", id}, {"name", utf8(opened.canonical.filename().native())}, {"size", opened.size}});
    if (impl_->grants.size() >= 10000) return failure("grant_limit", "Revoke unused file grants before approving more.");
    const auto id = local_security::random_hex(24);
    impl_->grants.emplace(id, Impl::Grant{scope, opened.canonical, opened.identity, {}});
    try { impl_->persist_state(); } catch (...) { impl_->grants.erase(id); throw; }
    return success({{"fileId", id}, {"name", utf8(opened.canonical.filename().native())}, {"size", opened.size}});
  } catch (...) { return failure("upload_unavailable", "Choose an ordinary local file outside protected browser directories."); }
}
Json FilePolicy::grant_folder(const std::string& scope, const std::filesystem::path& native_path) {
  if (scope.empty()) return failure("invalid_scope", "A folder must belong to a workspace.");
  try {
    auto opened = open_path(native_path, true);
    std::lock_guard lock(impl_->mutex);
    if (impl_->denied(opened.canonical, opened.identity)) return failure("protected_folder", "Private browser directories cannot be granted.");
    for (const auto& [id, existing] : impl_->folders)
      if (existing.scope == scope && existing.identity == opened.identity && same_path(existing.path, opened.canonical))
        return success({{"folderId", id}, {"name", utf8(opened.canonical.filename().native())}});
    if (impl_->folders.size() >= 128) return failure("grant_limit", "Revoke an unused folder grant before approving another.");
    const auto id = local_security::random_hex(24);
    impl_->folders.emplace(id, Impl::Grant{scope, opened.canonical, opened.identity, {}});
    try { impl_->persist_state(); } catch (...) { impl_->folders.erase(id); throw; }
    return success({{"folderId", id}, {"name", utf8(opened.canonical.filename().native())}});
  } catch (...) { return failure("folder_unavailable", "Choose an ordinary local folder outside protected browser directories."); }
}
Json FilePolicy::list_folders(const std::string& scope) const {
  std::lock_guard lock(impl_->mutex);
  Json folders = Json::array();
  for (const auto& [id, grant] : impl_->folders) if (grant.scope == scope) {
    bool available = false;
    try { const auto opened = open_path(grant.path, true); available = opened.identity == grant.identity && !impl_->denied(opened.canonical, opened.identity); } catch (...) {}
    folders.push_back({{"folderId", id}, {"name", utf8(grant.path.filename().native())}, {"available", available}});
  }
  return success({{"folders", std::move(folders)}});
}
Json FilePolicy::list_files(const std::string& scope, const std::string& folder_id, size_t limit) {
  limit = std::clamp<size_t>(limit, 1, 1000);
  std::lock_guard lock(impl_->mutex);
  Json files = Json::array(); bool truncated = false;
  if (folder_id.empty()) {
    for (const auto& [id, grant] : impl_->grants) if (grant.scope == scope && grant.folder.empty()) {
      if (files.size() >= limit) { truncated = true; break; }
      try {
        const auto opened = open_regular(grant.path);
        if (!(opened.identity == grant.identity) || impl_->denied(opened.canonical, opened.identity)) continue;
        files.push_back({{"fileId", id}, {"name", utf8(grant.path.filename().native())}, {"size", opened.size}});
      } catch (...) {}
    }
    return success({{"files", std::move(files)}, {"truncated", truncated}});
  }
  const auto folder = impl_->folders.find(folder_id);
  if (folder == impl_->folders.end() || folder->second.scope != scope) return failure("folder_denied", "This workspace has no such folder grant.");
  std::vector<std::string> added;
  try {
    auto root = open_path(folder->second.path, true);
    if (!(root.identity == folder->second.identity) || impl_->denied(root.canonical, root.identity)) return failure("folder_changed", "The granted folder is unavailable or changed.");
    size_t inspected = 0;
    std::error_code ec;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    auto iter = std::filesystem::recursive_directory_iterator(root.canonical, options, ec);
    const auto end = std::filesystem::recursive_directory_iterator{};
    for (; iter != end; iter.increment(ec)) {
      if (ec) { ec.clear(); continue; }
      if (++inspected > 5000 || files.size() >= limit || impl_->grants.size() >= 10000) { truncated = true; break; }
      const auto path = iter->path();
      const auto attributes = GetFileAttributesW(path.c_str());
      if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) { iter.disable_recursion_pending(); continue; }
      if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
        if (iter.depth() >= 7) { iter.disable_recursion_pending(); truncated = true; continue; }
        bool denied = false;
        for (const auto& protected_root : impl_->protected_roots) if (within(path, protected_root)) { denied = true; break; }
        if (denied) iter.disable_recursion_pending();
        continue;
      }
      try {
        auto opened = open_regular(path);
        if (!within(opened.canonical, root.canonical) || impl_->denied(opened.canonical, opened.identity)) continue;
        std::string id;
        for (const auto& [existing_id, grant] : impl_->grants)
          if (grant.scope == scope && grant.folder == folder_id && grant.identity == opened.identity && same_path(grant.path, opened.canonical)) { id = existing_id; break; }
        if (id.empty()) {
          id = local_security::random_hex(24);
          impl_->grants.emplace(id, Impl::Grant{scope, opened.canonical, opened.identity, folder_id});
          added.push_back(id);
        }
        files.push_back({{"fileId", id}, {"name", utf8(path.filename().native())},
                        {"relativePath", utf8(path.lexically_relative(root.canonical).native())}, {"size", opened.size}});
      } catch (...) { /* Busy or changing files remain unavailable. */ }
    }
    if (!added.empty()) impl_->persist_state();
    return success({{"folderId", folder_id}, {"files", std::move(files)}, {"truncated", truncated},
                    {"limits", {{"maxFiles", limit}, {"maxEntries", 5000}, {"maxDepth", 8}}}});
  } catch (...) {
    for (const auto& id : added) impl_->grants.erase(id);
    return failure("folder_unavailable", "The approved folder could not be safely listed.");
  }
}
Json FilePolicy::revoke_grant(const std::string& scope, const std::string& grant_id) {
  std::lock_guard lock(impl_->mutex);
  bool removed = false;
  const auto folder = impl_->folders.find(grant_id);
  if (folder != impl_->folders.end() && folder->second.scope == scope) { impl_->folders.erase(folder); removed = true; }
  const auto count = std::erase_if(impl_->grants, [&](const auto& entry) {
    return entry.second.scope == scope && (entry.first == grant_id || entry.second.folder == grant_id);
  });
  removed = removed || count > 0;
  try { impl_->persist_state(); }
  catch (...) { return failure("policy_persistence_failed", "Access is revoked for this session, but the persisted policy could not be updated. Close Xenon before restarting."); }
  return success({{"revoked", removed}});
}
std::optional<UploadFile> FilePolicy::resolve_upload(const std::string& scope, const std::string& file_id) const {
  std::lock_guard lock(impl_->mutex);
  const auto found = impl_->grants.find(file_id);
  if (found == impl_->grants.end() || found->second.scope != scope) return std::nullopt;
  try {
    auto opened = open_regular(found->second.path);
    if (!(opened.identity == found->second.identity) || impl_->denied(opened.canonical, opened.identity)) return std::nullopt;
    if (!found->second.folder.empty()) {
      const auto folder = impl_->folders.find(found->second.folder);
      if (folder == impl_->folders.end() || folder->second.scope != scope || !within(opened.canonical, folder->second.path)) return std::nullopt;
      auto root = open_path(folder->second.path, true);
      if (!(root.identity == folder->second.identity)) return std::nullopt;
    }
    UploadFile result;
    result.impl_ = std::make_unique<UploadFile::Impl>();
    result.impl_->opened = std::move(opened);
    return result;
  } catch (...) { return std::nullopt; }
}
void FilePolicy::revoke_scope(const std::string& scope) {
  std::lock_guard lock(impl_->mutex);
  std::erase_if(impl_->grants, [&](const auto& grant) { return grant.second.scope == scope; });
  std::erase_if(impl_->folders, [&](const auto& grant) { return grant.second.scope == scope; });
  impl_->persist_state();
}
void FilePolicy::deny_source(const std::filesystem::path& native_path) {
  std::lock_guard lock(impl_->mutex);
  const auto canonical = canonical_root(native_path);
  impl_->denied_sources.push_back(native_path.lexically_normal());
  impl_->denied_sources.push_back(canonical);
  Handle source(CreateFileW(canonical.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  BY_HANDLE_FILE_INFORMATION info{};
  if (source.value != INVALID_HANDLE_VALUE && GetFileInformationByHandle(source.value, &info) &&
      !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
    impl_->denied_source_ids.push_back({info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow});
  std::erase_if(impl_->grants, [&](const auto& grant) { return impl_->denied(grant.second.path, grant.second.identity); });
  impl_->persist_state();
}
std::string FilePolicy::safe_download_name(const std::string& suggested_name) {
  std::wstring name = widen(suggested_name.substr(0, 4096));
  const auto slash = name.find_last_of(L"/\\");
  if (slash != std::wstring::npos) name.erase(0, slash + 1);
  for (auto& ch : name) if (ch < 32 || ch == 127 || ch == L'<' || ch == L'>' || ch == L':' || ch == L'"' || ch == L'|' || ch == L'?' || ch == L'*') ch = L'_';
  if (name.size() > 160) { name.resize(160); if (name.back() >= 0xD800 && name.back() <= 0xDBFF) name.pop_back(); }
  while (!name.empty() && (name.back() == L'.' || name.back() == L' ')) name.pop_back();
  if (name.empty() || name == L"." || name == L"..") name = L"download";
  auto stem = folded(name.substr(0, name.find(L'.')));
  while (!stem.empty() && stem.back() == L' ') stem.pop_back();
  static const std::set<std::wstring> reserved{L"con", L"prn", L"aux", L"nul", L"clock$", L"conin$", L"conout$"};
  const bool port = stem.size() == 4 && (stem.starts_with(L"com") || stem.starts_with(L"lpt")) &&
    ((stem[3] >= L'1' && stem[3] <= L'9') || stem[3] == L'\u00b9' || stem[3] == L'\u00b2' || stem[3] == L'\u00b3');
  if (reserved.contains(stem) || port) name = L"download-" + name;
  return utf8(name);
}
std::filesystem::path FilePolicy::allocate_download(const std::string& scope, const std::string& suggested_name) {
  if (scope.empty()) throw std::runtime_error("Downloads need a workspace scope");
  std::lock_guard lock(impl_->mutex);
  auto it = impl_->download_scopes.find(scope);
  if (it == impl_->download_scopes.end()) {
    const auto root = impl_->downloads / local_security::random_hex(16);
    std::filesystem::create_directory(root);
    local_security::restrict_path(root);
    it = impl_->download_scopes.emplace(scope, root).first;
  }
  const auto actual_root = canonical_root(it->second);
  if (!within(actual_root, impl_->downloads) || !same_path(actual_root, it->second)) throw std::runtime_error("Download directory changed");
  const auto name = local_security::random_hex(12) + "-" + safe_download_name(suggested_name);
  return actual_root / std::filesystem::path(widen(name));
}
}
