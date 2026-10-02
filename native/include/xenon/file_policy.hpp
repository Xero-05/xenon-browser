#pragma once
#include "xenon/contracts.hpp"
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace xenon {
// Native grants are never filenames supplied directly by an agent. Resolved
// upload handles retain a read handle denying write/delete until completion.
class UploadFile {
 public:
  UploadFile();
  ~UploadFile();
  UploadFile(UploadFile&&) noexcept;
  UploadFile& operator=(UploadFile&&) noexcept;
  UploadFile(const UploadFile&) = delete;
  const std::filesystem::path& path() const;
 private:
  friend class FilePolicy;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class FilePolicy {
 public:
  FilePolicy(std::filesystem::path download_root,
             std::vector<std::filesystem::path> protected_roots);
  ~FilePolicy();
  // Human-selected regular file only. Returns {fileId,name,size}, never a path.
  Json grant_upload(const std::string& scope, const std::filesystem::path& native_path);
  // Folder grants are native-only, persistent and recursively list regular
  // files without following links. Agent callers supply only folderId tokens.
  Json grant_folder(const std::string& scope, const std::filesystem::path& native_path);
  Json list_folders(const std::string& scope) const;
  Json list_files(const std::string& scope, const std::string& folder_id = {}, size_t limit = 100);
  Json revoke_grant(const std::string& scope, const std::string& grant_id);
  std::optional<UploadFile> resolve_upload(const std::string& scope, const std::string& file_id) const;
  void revoke_scope(const std::string& scope);
  void deny_source(const std::filesystem::path& native_path);
  // This native-only destination is passed to CEF, never to an agent.
  std::filesystem::path allocate_download(const std::string& scope, const std::string& suggested_name);
  static std::string safe_download_name(const std::string& suggested_name);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
