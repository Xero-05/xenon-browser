#pragma once
#include "xenon/contracts.hpp"
#include <filesystem>
#include <memory>
#include <set>
#include <vector>
#include "include/cef_client.h"
#include "include/cef_request_context_handler.h"

namespace xenon {
class Vault;
class FilePolicy;
class CefEngine final : public BrowserEngine {
 public:
  explicit CefEngine(std::filesystem::path data_root);
  ~CefEngine() override;
  void initialize();
  void shutdown();
  void execute(const std::string&, const Json&, Reply) override;
  void execute_guarded(const std::string&, const Json&, std::function<bool()>, Reply) override;
  void set_event_sink(EventSink) override;
  void native_input(CefWindowHandle window, bool busy, bool credential_input = false, bool substantive = true,
                    bool primary_click = false);
  void set_native_key_callback(std::function<void(CefWindowHandle, UINT, WPARAM)> callback);
  void show_controls();
  void show_updates();
  void set_controls_callback(std::function<void()> callback);
  void set_updates_callback(std::function<void()> callback);
  void set_dialog_callback(std::function<void(const std::string&)> callback);
  void set_private_workspace_callback(std::function<void()> callback);
  // create(workspace, tab, human, openerTab) returns the native tab host.
  void set_host_callbacks(std::function<HWND(const std::string&,const std::string&,bool,const std::string&)> create,
                          std::function<void(const std::string&,HWND)> created,
                          std::function<void(const std::string&)> closed);
  void set_download_callback(std::function<bool(const std::string&)> allowed);
  void set_permission_callback(std::function<void(const std::string&,const std::string&,const std::string&,std::function<void(bool)>)> callback);
  void native_pointer(HWND page,POINT screen,bool substantive=false);
  void select_native_tab(const std::string& tab_id);
  void native_command(const std::string& tab_id,const std::string& command,const std::string& value={},Reply reply={});
  // A human chose a page context-menu link command (tab, url, new window).
  void set_open_link_callback(std::function<void(const std::string&,const std::string&,bool)> callback);
  // Human-installed, enabled extensions whose own pages native UI may open.
  void set_extension_ids(std::set<std::string> ids);
  // Count of vault accounts for the tab's exact HTTPS origin; metadata only.
  size_t saved_account_count(const std::string& tab_id);
  void set_save_prompt_callback(std::function<void(const std::string&, CefWindowHandle)> callback);
  // Native human UI only. These capabilities are never exposed by execute/MCP.
  void set_autofill_prompt_callback(std::function<void(const Json&, CefWindowHandle)> callback);
  void request_autofill(const std::string& tab_id, Reply);
  void fill_saved_account(const std::string& offer_id, const std::string& account_id, Reply);
  void dismiss_autofill(const std::string& offer_id);
  bool autofill_offer_valid(const std::string& offer_id);
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
  void fixture_autofill_focus(const std::string& tab_id, Reply);
#endif
  void cancel_login_prompts();
  void set_vault(Vault* vault);
  void set_file_policy(FilePolicy* files);
  // Startup only: tombstoned workspaces can never be reopened or restored.
  void exclude_workspaces(const std::vector<std::string>& workspaces);
  void restore_session(const std::vector<std::string>& allowed_workspaces, Reply reply);
  Json native_dialogs();
  Json native_tabs();
  void answer_native_dialog(const std::string& tab_id, bool accept, const std::string& text);
  CefRefPtr<CefClient> default_client();
  CefRefPtr<CefRequestContextHandler> default_context_handler();
  void protected_fill(const std::string& tab_id, const std::string& username,
                      const std::string& password, const Json& params, Reply);
  void release_protection(const std::string& tab_id);
  class Impl;
 private:
  std::shared_ptr<Impl> impl_;
};
}
