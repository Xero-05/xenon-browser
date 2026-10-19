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
  // True when the human currently controls the tab; other tabs gate passkeys.
  void set_passkey_control_callback(std::function<bool(const std::string&)> human_controls);
  void set_permission_callback(std::function<void(const std::string&,const std::string&,const std::string&,std::function<void(bool)>)> callback);
  // Native chrome only; none of these are reachable through MCP.
  // A page entered or left fullscreen: (tab, fullscreen).
  void set_fullscreen_callback(std::function<void(const std::string&,bool)> callback);
  // A website link for another app: (tab, requesting origin, url, scheme,
  // permitted, done). `permitted` is false unless the tab is the person's or
  // follows their own recent input in it; only then may native UI offer to
  // open it. Call done(opened) once the prompt is answered.
  void set_external_link_callback(std::function<void(const std::string&,const std::string&,const std::string&,const std::string&,bool,std::function<void(bool)>)> callback);
  // A page asks to share every display: (tab, origin, with system audio, answer).
  void set_screen_share_callback(std::function<void(const std::string&,const std::string&,bool,std::function<void(bool)>)> callback);
  struct SiteIcon {int width{},height{};std::vector<unsigned char> pixels;};
  // The tab's site icon as premultiplied top-down BGRA. UI thread only.
  bool site_icon(const std::string& tab_id,SiteIcon& icon) const;
  // Native Clear browsing data for one workspace. Cookies created since
  // `since_unix_ms` (0: all), and for all time the stored data of `origins`
  // and the HTTP cache. Agent input in the workspace pauses; evidence is stale.
  void clear_browsing_data(const std::string& workspace,int64_t since_unix_ms,bool cookies,bool storage,bool cache,std::vector<std::string> origins,Reply reply);
  void native_pointer(HWND page,POINT screen,bool substantive=false);
  void select_native_tab(const std::string& tab_id,bool awaiting_browser=false);
  // Native shell only: whether the tab is hidden behind its window's selected
  // tab. Covered tabs paint at a reduced rate unless an agent works in them.
  void set_tab_covered(const std::string& tab_id,bool covered);
  void native_command(const std::string& tab_id,const std::string& command,const std::string& value={},Reply reply={});
  // Native human site information for the tab's current website: connection
  // and certificate summary, cookie metadata without values, storage use and
  // site settings. Never exposed through MCP.
  void site_info(const std::string& tab_id,Reply reply);
  // Native human site actions. Each is refused unless the tab still shows
  // `origin` outside protected authentication. Deleting cookies or site data
  // counts as human page activity: agent input pauses and evidence is stale.
  void set_site_setting(const std::string& tab_id,const std::string& origin,const std::string& setting,const std::string& value,Reply reply);
  void reset_site_settings(const std::string& tab_id,const std::string& origin,Reply reply);
  // Empty `keys` deletes every cookie of the site; otherwise only those keys.
  void delete_site_cookies(const std::string& tab_id,const std::string& origin,const std::vector<std::string>& keys,Reply reply);
  void clear_site_data(const std::string& tab_id,const std::string& origin,Reply reply);
  // Native diagnostic read using the agent's rendered-evidence filter. Does
  // not publish/replace agent observation tokens or actionable references.
  void developer_elements(const std::string& tab_id, Reply);
  void developer_highlight(const std::string& tab_id,const std::string& diagnostic_id,bool discard=false,Reply reply={});
  void developer_pick(const std::string& tab_id,bool enabled,Reply);
  void developer_pick_point(const std::string& tab_id,double x,double y);
  void developer_hover_point(const std::string& tab_id,double x,double y,Reply);
  // Session-only native opt-in; never exposed to MCP.
  void set_uncertain_click_collection(bool enabled);
  void clear_uncertain_click_log();
  Json uncertain_click_log_status() const;
  // A human chose a page context-menu link command (tab, url, new window).
  void set_open_link_callback(std::function<void(const std::string&,const std::string&,bool)> callback);
  // Human-installed, enabled extensions whose own pages native UI may open.
  void set_extension_ids(std::set<std::string> ids);
  // Chromium's native extension manager and Web Store. Human UI only; these
  // browsers are never broker tabs and cannot be observed/controlled by MCP.
  void open_extensions_manager(const std::string& workspace="native-default",
                               const std::string& url="chrome://extensions/",Reply reply={});
  void set_workspace_label_callback(std::function<std::string(const std::string&)> callback);
  void extension_quick_list(const std::string& tab_id,const std::string& workspace,Reply reply);
  void invoke_extension_action(const std::string& tab_id,const std::string& workspace,
                        const std::string& extension,const std::string& command,uint64_t epoch,RECT anchor,Reply reply);
  void dismiss_extension_action(const std::string& tab_id);
  bool extension_side_visible(const std::string& tab_id);
  void layout_extension_surfaces(const std::string& tab_id,RECT dock,RECT toolbar_anchor);
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
  void fixture_extension_manager(const std::string& workspace,const std::string& action,Reply reply);
#endif
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
