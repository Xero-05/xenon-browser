#pragma once
#include "xenon/contracts.hpp"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace xenon::updates {
struct Release {
  std::string version;
  std::string url;
  std::string sha256;
  uint64_t bytes{};
};
using Progress = std::function<void(uint64_t downloaded, uint64_t total)>;

// Native-only blocking operations: call from a lifetime-safe worker, never the
// CEF/Win32 message loop. Exceptions contain fixed diagnostics, not response data.
std::optional<Release> select_release(const Json& releases, const std::string& current);
std::optional<Release> check_for_update(const std::string& current);
std::filesystem::path download_installer(const Release& release, Progress progress,
                                       const std::atomic_bool& cancel);
class InstallerLaunch;
namespace detail {
std::shared_ptr<InstallerLaunch> prepare_installer_in(const std::filesystem::path&,
    const Release&, const std::filesystem::path& private_root);
}
// A verified image created suspended. Discarding it cancels only that unstarted
// process. Start exactly once, after normal CEF shutdown releases the running
// marker; this never terminates a running browser or waits for installation.
class InstallerLaunch {
 public:
  ~InstallerLaunch();
  void start();
 private:
  struct Impl;
  explicit InstallerLaunch(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
  friend std::shared_ptr<InstallerLaunch> detail::prepare_installer_in(
      const std::filesystem::path&, const Release&, const std::filesystem::path&);
};
std::shared_ptr<InstallerLaunch> prepare_installer(const std::filesystem::path&, const Release&);

namespace detail {
// Shared boundary checks, exposed for synthetic native tests. These two checks
// perform no network, profile reads or process launch.
bool allowed_redirect_url(const std::string& url, bool metadata);
void verify_installer_file(const std::filesystem::path& path, const Release& release);
std::optional<Release> select_release_json(const std::string& metadata, const std::string& current);
using ByteSink = std::function<void(const unsigned char*, size_t)>;
using ByteSource = std::function<void(const ByteSink&)>;
// Shared storage boundary. The native test supplies its own byte source and
// disposable absolute root; production supplies the fixed HTTPS transport.
std::filesystem::path stage_installer(const Release& release,
    const std::filesystem::path& private_root, Progress progress,
    const std::atomic_bool& cancel, const ByteSource& source);
// Release smoke seam: real fixed-origin HTTPS download, but an explicitly chosen
// disposable storage root. Neither helper launches code or changes browser state.
std::filesystem::path download_installer_in(const Release& release,
    const std::filesystem::path& private_root, Progress progress,
    const std::atomic_bool& cancel);
}
}
