#pragma once

#include "xenon/contracts.hpp"
#include "xenon/permissions.hpp"
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
  bool configure_client(const std::string& client_id, const ClientPolicy& policy);
  bool configure_workspace_client(const std::string& workspace_id, const std::string& client_id,
                                  std::optional<WorkspaceAccess> access);
  bool rename_workspace(const std::string& workspace_id, const std::string& display_name);
  bool grant_client_account(const std::string& client_id, const std::string& account_id, const std::string& origin);
  bool revoke_account(const std::string& client_id, const std::string& workspace_id, const std::string& account_id);
  void resource_changed(const std::string& client_id);
  bool allow_download(const std::string& tab_id) const;
  void set_ui_state_callback(std::function<void()> callback);
  bool grant_account(const std::string& client_id, const std::string& workspace_id,
                     const std::string& account_id, const std::string& origin);
  RevocationStatus revoke_client(const std::string& client_id);
  // Native human action only. Saves a permanent tombstone before revoking
  // access, then closes the workspace after accepted finite input drains.
  void remove_workspace(const std::string& workspace_id, Reply reply);
  std::vector<std::string> removed_workspaces() const;
  void human_acquire(const std::string& tab_id);
  void human_release(const std::string& tab_id, const std::string& to_session);
  // Native human actions only; never MCP methods. Closing cancels each tab's
  // queued agent mutations and refuses new ones. A control group closed in
  // full is first claimed for the human. The engine closes only after
  // accepted finite input drains. Workspaces, profiles and grants remain.
  void close_tabs(const std::vector<std::string>& tab_ids, Reply reply);
  void close_workspace_tabs(const std::string& workspace_id, Reply reply);
  // Empty when a native human may move this tab to another workspace: no
  // agent owner, pending handoff or accepted input in its control group, not
  // protected, and both workspaces persistent and available. Otherwise a code.
  std::string tab_move_blocker(const std::string& tab_id, const std::string& workspace_id) const;
  // Profiles cannot share a live page: this opens url as a human tab in the
  // target workspace, then closes the original only if it is still movable.
  void move_tab_to_workspace(const std::string& tab_id, const std::string& workspace_id,
                             const std::string& url, Reply reply);
  void open_human_workspace(const std::string& url, Reply reply, bool private_mode = false, const std::string& name = {});
  void open_initial_human_workspace(const std::string& url, Reply reply);
  void open_human_tab(const std::string& workspace_id, const std::string& url, Reply reply);
  void stop_all();

 private:
  void open_human_workspace_impl(const std::string& url, Reply reply, bool private_mode, bool initial, const std::string& name = {});
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
}
