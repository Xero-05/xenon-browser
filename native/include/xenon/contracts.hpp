#pragma once

#include <functional>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

namespace xenon {
using Json = nlohmann::json;
// Engine and broker responses always use one envelope. No secrets in errors.
using Reply = std::function<void(Json)>;
using EventSink = std::function<void(const Json&)>;

inline Json success(Json value = Json::object()) {
  return {{"ok", true}, {"result", std::move(value)}};
}
inline Json failure(std::string code, std::string message) {
  return {{"ok", false}, {"error", {{"code", std::move(code)}, {"message", std::move(message)}}}};
}

// execute may be called from any thread. CEF implementations marshal to the UI
// thread and complete asynchronously; no navigation or network wait blocks it.
// The broker is the only caller for agent commands. Handoff is NOT a command.
class BrowserEngine {
 public:
  virtual ~BrowserEngine() = default;
  virtual void execute(const std::string& command, const Json& params, Reply reply) = 0;
  // A queued engine implementation must invoke permit on its actual dispatch
  // thread immediately before beginning the operation. Multi-step asynchronous
  // actions retain and recheck it before further page or file-input effects.
  // Successful rechecks are metadata-idempotent. Denial stops new effects but
  // must not prevent balancing held keys/buttons or internal cleanup. The
  // callback checks trusted broker metadata; ownership commits never call Chromium.
  // Explicit handoff freezes undispatched work but lets an already dispatched
  // finite action drain under its unchanged owner/generation. Physical activity,
  // client revocation or loss of authorization still invalidates continuation.
  virtual void execute_guarded(const std::string& command, const Json& params,
                               std::function<bool()> permit, Reply reply) {
    if (!permit()) { reply(failure("DISPATCH_CANCELLED", "Control changed before engine dispatch")); return; }
    execute(command, params, std::move(reply));
  }
  virtual void set_event_sink(EventSink sink) = 0;
};

// Engine command names (native-private protocol):
// workspace.ensure {workspaceId}; tabs.create {workspaceId, url};
// tabs.close / tabs.list; page.navigate/back/forward/reload/observe/screenshot;
// page.click/fill/select/check/key/scroll/drag/hover/wait/dialog;
// page.batch is broker-only: bounded sequential fill/select/check/click steps,
// dispatched as existing guarded engine commands under one journal reservation.
// files.upload/downloads; auth.accounts/login.
// Page commands include agentSessionId, workspaceId, tabId; mutating commands
// also include operationId and ownershipGeneration. Observe returns observationId.
// Engine owns opaque element references and validates freshness before input.
// Engine events: tab.created {workspaceId,tabId,openerTabId?}, tab.closed,
// tab.navigated {tabId,documentId}, human.input {tabId,busy,pauseUntil},
// human.idle {tabId},
// auth.protected {tabId,protected}, system.locked {locked}.
// Physical page activity preserves owner/generation and pauses only that tab.
// pauseUntil is an advisory Unix-millisecond deadline, null while keys/buttons
// or IME composition are held. Only the engine's idle event ends the pause;
// the broker never dispatches page input or polls the browser to resume it.
// Every accepted command captures the activity epoch, so an interrupted
// continuation remains invalid after idle. Prior observations are discarded.
// Native explicit takeover/release and agent handoff remain ownership changes.
// Broker-only control.activity needs authentication but no worker/workspace
// arguments. It returns {tabs:[...]} only for this client's connected workers'
// owned, granted tabs, with retained activity status and no page content.
}
