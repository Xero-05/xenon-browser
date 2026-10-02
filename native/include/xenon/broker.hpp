#pragma once

#include "xenon/contracts.hpp"
#include <filesystem>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace xenon {
// All authorization and control state is outside BrowserEngine. One broker is
// shared by all pipe connections; handles are names, not authentication tokens.
class Broker {
 public:
  enum class RevocationStatus { missing, pending, durable };
  struct Limits { size_t max_connected_workers{16}; };
  Broker(BrowserEngine& engine, std::filesystem::path data_root);
  Broker(BrowserEngine& engine, std::filesystem::path data_root, Limits limits);
  ~Broker();
  Broker(const Broker&) = delete;
  Broker& operator=(const Broker&) = delete;

  void dispatch(const std::string& connection_id, const Json& request, Reply reply);
  void disconnect(const std::string& connection_id);
  Json state() const;
  bool approve_pairing(const std::string& request_id);
  bool deny_pairing(const std::string& request_id);
  bool share_workspace(const std::string& workspace_id, const std::string& client_id);
  bool grant_account(const std::string& client_id, const std::string& workspace_id,
                     const std::string& account_id, const std::string& origin);
  RevocationStatus revoke_client(const std::string& client_id);
  // Native human action only. Saves a permanent tombstone before revoking
  // access, then closes the workspace after accepted finite input drains.
  void remove_workspace(const std::string& workspace_id, Reply reply);
  std::vector<std::string> removed_workspaces() const;
  void human_acquire(const std::string& tab_id);
  void human_release(const std::string& tab_id, const std::string& to_session);
  void open_human_workspace(const std::string& url, Reply reply, bool private_mode = false);
  void open_initial_human_workspace(const std::string& url, Reply reply);
  void stop_all();

 private:
  void open_human_workspace_impl(const std::string& url, Reply reply, bool private_mode, bool initial);
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
}
