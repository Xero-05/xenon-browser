#pragma once
#include "xenon/contracts.hpp"
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace xenon {
// Native-only. Never serialize this object or forward it through MCP/IPC.
class Secret {
 public:
  Secret(std::string username, std::string password);
  ~Secret();
  Secret(Secret&& other);
  Secret& operator=(Secret&& other);
  Secret(const Secret&) = delete;
  Secret& operator=(const Secret&) = delete;
  const std::string& username() const noexcept { return username_; }
  const std::string& password() const noexcept { return password_; }
 private:
  void clear() noexcept;
  std::string username_, password_;
};

// Native UI metadata only. The username display never contains credential bytes.
// A proposal is not proof of successful authentication; the caller decides when
// to show its prompt. Do not add these objects or methods to MCP/IPC responses.
struct PendingCredential {
  std::string candidate_id;
  std::string origin;
  std::string username_label;
  bool update{};
};

class Vault {
 public:
  using PendingClock = std::function<std::chrono::steady_clock::time_point()>;
  static constexpr size_t kMaxPendingLogins = 64;
  static constexpr auto kPendingLoginLifetime = std::chrono::minutes(10);
  explicit Vault(const std::filesystem::path& database, PendingClock pending_clock = {});
  ~Vault();
  Vault(const Vault&) = delete;
  Vault& operator=(const Vault&) = delete;
  // Accounts are metadata only; labels must be human-selected descriptions.
  Json list_accounts(std::optional<std::string> https_origin = std::nullopt) const;
  Json save(const std::string& origin_or_url, const std::string& username,
            const std::string& password, const std::string& label = {},
            const std::string& replace_account_id = {});
  std::optional<Secret> get_secret(const std::string& account_id,
                                 const std::string& expected_https_origin) const;
  Json remove(const std::string& account_id);
  // Pending secrets are DPAPI-encrypted in memory, never persisted, and cleared
  // on lock. Invalid, locked or unchanged credentials produce no proposal.
  std::optional<PendingCredential> propose_login(const std::string& origin_or_url,
                                                const std::string& username,
                                                const std::string& password);
  std::vector<PendingCredential> pending_logins() const;
  // Confirmation consumes the token. A saved record changed since proposal
  // requires a fresh proposal; an old prompt can never overwrite that change.
  Json accept_login(const std::string& candidate_id, const std::string& label = {});
  void dismiss_login(const std::string& candidate_id);
  // The path comes from a native file picker. Never expose this method to agents.
  // Conflicts are skipped unless the native user explicitly chose replacement.
  Json preview_csv(const std::filesystem::path& path) const;
  Json import_csv(const std::filesystem::path& path, bool replace_conflicts = false,
                  const std::string& expected_file_digest = {});
  void set_locked(bool locked);
  bool locked() const;
  static std::optional<std::string> normalize_https_origin(const std::string& url);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
