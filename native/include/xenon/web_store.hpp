#pragma once
#include "xenon/extension_store.hpp"
#include <atomic>
#include <string_view>

namespace xenon::web_store {
class Package;
// Native human UI only. No page script, MCP request or arbitrary URL reaches
// this transport. Downloaded bytes stay in memory until explicit confirmation.
std::string extension_id(std::string_view link);
class VerifiedCrx {
 public:
  const std::string& extension_id() const { return id; }
  const std::string& archive() const { return zip; }
 private:
  VerifiedCrx()=default;
  std::string id, public_key, zip;
  friend VerifiedCrx verify_crx3(const std::string&,const std::string&);
  friend Package unpack(VerifiedCrx,const std::atomic_bool&);
};
VerifiedCrx verify_crx3(const std::string& bytes, const std::string& expected_id);
VerifiedCrx download(const std::string& id, const std::atomic_bool& cancel);
struct ZipEntry { std::string name; uint32_t size{}, compressed{}, crc{}; bool directory{}; };
// Reject ambiguous Windows paths, links, encryption, ZIP64 and oversized trees
// before giving the archive to the CEF decompressor.
std::vector<ZipEntry> inspect_zip(std::string_view zip);
void validate_file_name(std::string_view name);
struct File { std::string name, bytes; };
class Package {
 public:
  std::string permission_review() const;
 private:
  Package()=default;
  std::string id, public_key, name, version;
  Json manifest;
  std::vector<File> files;
  friend Package unpack(VerifiedCrx,const std::atomic_bool&);
  friend class xenon::ExtensionStore;
};
// CEF implementation; runs entirely on its calling worker thread.
Package unpack(VerifiedCrx crx, const std::atomic_bool& cancel);
namespace detail {
bool allowed_download_url(std::string_view url);
bool verify_signature(std::string_view key, std::string_view signature,
                      const std::string& signed_bytes, bool rsa);
std::string permission_review(const Json& manifest,const std::string& name,
                              const std::string& version,const std::string& id);
}
}
