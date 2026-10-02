#include "xenon/broker.hpp"
#include "xenon/local_security.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace xenon {
namespace {
std::string identifier(const char* prefix) { return std::string(prefix) + local_security::random_hex(16); }
std::string field(const Json& value, const char* key) {
  auto it = value.find(key); return it != value.end() && it->is_string() ? it->get<std::string>() : std::string{};
}
using Delivery = std::pair<Reply, Json>;
void deliver(std::vector<Delivery> items) { for (auto& [reply, value] : items) if (reply) { try { reply(std::move(value)); } catch (...) {} } }
const std::set<std::string> read_commands{"page.observe", "page.screenshot", "files.downloads", "auth.accounts"};
const std::set<std::string> write_commands{"tabs.close", "page.navigate", "page.back", "page.forward", "page.reload", "page.click", "page.fill", "page.select", "page.check", "page.key", "page.scroll", "page.drag", "page.hover", "page.dialog", "files.upload", "auth.login"};
const std::set<std::string> target_commands{"page.click", "page.fill", "page.select", "page.check", "page.key", "page.scroll", "page.drag", "page.hover", "files.upload", "auth.login"};
bool safe_web_url(const std::string& url) {
  if (url.size() > 16384 || url.find_first_of("\r\n\t\\") != std::string::npos) return false;
  if (url == "about:blank") return true;
  auto offset = url.rfind("https://", 0) == 0 ? 8U : (url.rfind("http://", 0) == 0 ? 7U : 0U);
  if (!offset) return false;
  auto authority = url.substr(offset, url.find_first_of("/?#", offset) - offset);
  return !authority.empty() && authority.find('@') == std::string::npos;
}
bool exact_https_origin(const std::string& origin) {
  if (origin.rfind("https://", 0) != 0 || !safe_web_url(origin)) return false;
  return origin.find_first_of("/?#", 8) == std::string::npos;
}
Json removed_workspace_result(const Json& value) {
  Json filtered;
  if (value.value("ok", false)) filtered = success({{"status", "completed"}, {"resultWithheld", true}});
  else {
    const auto error = value.find("error");
    auto code = error != value.end() && error->is_object() ? field(*error, "code") : std::string{};
    filtered = failure(code.empty() ? "OUTCOME_UNKNOWN" : code, "Operation ended in a removed workspace; page details are withheld. Inspect its recorded dispatch and outcome status before deciding what to do.");
  }
  for (const auto* key : {"operationId", "dispatchStatus"}) if (value.contains(key)) filtered[key] = value[key];
  return filtered;
}
}

struct Broker::Impl : std::enable_shared_from_this<Broker::Impl> {
  struct Client { std::string name, token_hash; };
  struct AccountGrant { std::string client, workspace, account, origin; };
  struct Pairing { std::string name, connection; Reply reply; };
  struct Workspace {
    std::set<std::string> clients; bool ready{}, private_mode{};
    bool removing{}, removal_active{}, removal_retry{};
    size_t pending_creates{};
    std::string removal_error;
    std::vector<Reply> removal_waiters;
  };
  struct Worker {
    std::string client, connection, workspace, name;
    bool connected{true}, retiring{};
    uint64_t attachment{1}, inactive_order{};
    size_t callbacks{};
  };
  struct Observation { std::string id; uint64_t generation{}, human_epoch{}; };
  struct Command { std::string operation, session, method; uint64_t generation{}, attachment{}, human_epoch{}; Json params; };
  struct Tab {
    std::string workspace, owner, document, group;
    uint64_t generation{1}, state_epoch{}, human_epoch{}; bool frozen{}, human_busy{}, protected_auth{};
    std::optional<int64_t> human_pause_until;
    std::optional<std::string> pending_owner;
    std::string active, dialog_active;
    std::deque<Command> queue;
    std::map<std::string, Observation> observations;
  };
  struct Operation {
    std::string client, tab, workspace, method, signature, state{"queued"};
    bool dispatched{};
    Json result;
    std::vector<Reply> waiters;
  };
  BrowserEngine& engine;
  std::filesystem::path root;
  mutable std::mutex mutex;
  bool stopping{}, locked{};
  std::map<std::string, Client> clients;
  // Failed durable revocations remain selectable in native UI for retry, while
  // the client and all grants are already absent from live authorization state.
  std::map<std::string, std::string> pending_revocations;
  std::map<std::string, std::string> connections;
  std::map<std::string, Pairing> pairings;
  std::map<std::string, Workspace> workspaces;
  std::set<std::string> removed_workspaces;
  bool removal_check_queued{};
  std::map<std::string, Worker> workers;
  std::map<std::string, Tab> tabs;
  std::map<std::string, Operation> operations;
  std::vector<AccountGrant> account_grants;
  std::deque<std::string> operation_order;
  std::mutex io_mutex;
  std::condition_variable io_condition;
  std::deque<std::function<void()>> io_jobs;
  bool io_stopped{};
  std::thread io_thread;
  std::mutex persistence_mutex;
  uint64_t snapshot_revision{}, written_revision{};
  uint64_t inactive_order{};
  const Limits limits;
  static constexpr size_t disconnected_cache = 256, max_retained_workers = 1024, max_tabs = 64, max_queue = 16, max_worker_pending = 32, max_journal = 4096;

  Impl(BrowserEngine& target, std::filesystem::path directory, Limits configured) : engine(target), root(std::move(directory)), limits(configured) {
    if(limits.max_connected_workers<1 || limits.max_connected_workers>256)throw std::invalid_argument("Concurrent worker limit must be from 1 through 256");
    std::filesystem::create_directories(root);
    local_security::restrict_path(root);
    const auto path = root / "broker-state.json";
    if (std::filesystem::exists(path)) {
      local_security::restrict_path(path);
      std::ifstream file(path); Json saved; file >> saved;
      if (saved.value("version", 0) != 1) throw std::runtime_error("Unsupported broker state version");
      for (auto& entry : saved.value("clients", Json::array())) {
        auto id = field(entry, "id"), hash = field(entry, "tokenHash");
        if (!id.empty() && hash.size() == 64) clients.emplace(id, Client{field(entry, "name"), hash});
      }
      for (auto& entry : saved.value("removedWorkspaces", Json::array()))
        if (entry.is_string() && !entry.get<std::string>().empty() && entry != "native-default") removed_workspaces.insert(entry.get<std::string>());
      for (auto& entry : saved.value("workspaces", Json::array())) {
        auto id = field(entry, "id"); if (id.empty() || removed_workspaces.contains(id)) continue;
        Workspace workspace;
        for (auto& client : entry.value("clients", Json::array())) if (client.is_string() && clients.contains(client.get<std::string>())) workspace.clients.insert(client.get<std::string>());
        workspaces.emplace(id, std::move(workspace));
      }
      for (auto& entry : saved.value("accountGrants", Json::array())) {
        AccountGrant grant{field(entry, "clientId"), field(entry, "workspaceId"), field(entry, "accountId"), field(entry, "origin")};
        if (clients.contains(grant.client) && granted(grant.client, grant.workspace) && !grant.account.empty() && exact_https_origin(grant.origin)) account_grants.push_back(std::move(grant));
      }
      for (auto& entry : saved.value("operations", Json::array())) {
        auto id = field(entry, "operationId"), client = field(entry, "clientId");
        if (id.empty() || !clients.contains(client) || operations.size() >= max_journal) continue;
        Operation operation; operation.client = client; operation.tab = field(entry, "tabId"); operation.workspace = field(entry, "workspaceId");
        operation.state = field(entry, "state"); operation.dispatched = entry.value("dispatched", true);
        const bool terminal = operation.state == "completed" || operation.state == "failed" || operation.state == "cancelled";
        if (!terminal) { operation.state = "outcome_unknown"; operation.dispatched = true; }
        operation.result = failure(terminal ? "RESULT_NOT_RETAINED" : "OUTCOME_UNKNOWN", terminal ? "Operation metadata survived restart; page results are not retained and the operation will not be replayed" : "Browser restarted after accepting this operation; inspect the page before deciding what to do");
        operation.result["operationId"] = id; operation.result["dispatchStatus"] = operation.dispatched ? "dispatched" : "not_dispatched";
        operations.emplace(id, std::move(operation)); operation_order.push_back(id);
      }
    }
    io_thread = std::thread([this] {
      while (true) {
        std::function<void()> job;
        { std::unique_lock lock(io_mutex); io_condition.wait(lock, [&] { return io_stopped || !io_jobs.empty(); }); if (io_jobs.empty() && io_stopped) return; job = std::move(io_jobs.front()); io_jobs.pop_front(); }
        try { job(); } catch (...) { /* Individual jobs report conservative failure. */ }
      }
    });
  }
  ~Impl() { shutdown_io(); }
  void shutdown_io() {
    { std::lock_guard lock(io_mutex); io_stopped = true; } io_condition.notify_all();
    if (io_thread.joinable()) { if (io_thread.get_id() == std::this_thread::get_id()) io_thread.detach(); else io_thread.join(); }
  }
  bool post_io(std::function<void()> job) {
    { std::lock_guard lock(io_mutex); if (io_stopped) return false; io_jobs.push_back(std::move(job)); } io_condition.notify_one(); return true;
  }
  void persist_async() {
    std::weak_ptr<Impl> weak = shared_from_this();
    post_io([weak] { if (auto self = weak.lock()) {
      Json snapshot; { std::lock_guard lock(self->mutex); snapshot = self->snapshot_locked(); }
      try { self->persist_value(snapshot); } catch (...) {}
    } });
  }
  Json snapshot_locked() {
    Json saved{{"version", 1}, {"clients", Json::array()}, {"workspaces", Json::array()}, {"accountGrants", Json::array()}, {"operations", Json::array()}};
    saved["revision"] = ++snapshot_revision;
    saved["removedWorkspaces"] = removed_workspaces;
    for (auto& [id, value] : clients) saved["clients"].push_back({{"id", id}, {"name", value.name}, {"tokenHash", value.token_hash}});
    for (auto& [id, value] : workspaces) if (!value.private_mode && !removed_workspaces.contains(id)) saved["workspaces"].push_back({{"id", id}, {"clients", value.clients}});
    for (auto& grant : account_grants) if (workspaces.contains(grant.workspace) && !removed_workspaces.contains(grant.workspace) && !workspaces.at(grant.workspace).private_mode) saved["accountGrants"].push_back({{"clientId", grant.client}, {"workspaceId", grant.workspace}, {"accountId", grant.account}, {"origin", grant.origin}});
    // Deliberately persist no arguments, signatures, observations or page output.
    // A durable pending_dispatch marker means a crash cannot trigger a replay.
    for (auto& [id, value] : operations) saved["operations"].push_back({{"operationId", id}, {"clientId", value.client}, {"tabId", value.tab}, {"workspaceId", value.workspace}, {"state", value.state}, {"dispatched", value.dispatched}});
    return saved;
  }
  void persist_value(const Json& saved) {
    std::lock_guard io_lock(persistence_mutex);
    auto revision = saved.at("revision").get<uint64_t>(); if (revision < written_revision) return;
    auto temporary = root / "broker-state.tmp", destination = root / "broker-state.json";
    { std::ofstream file(temporary, std::ios::binary | std::ios::trunc); file << saved.dump(2); file.flush(); if (!file) throw std::runtime_error("Cannot persist client grants"); }
    local_security::restrict_path(temporary);
#ifdef _WIN32
    HANDLE durable_file = CreateFileW(temporary.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (durable_file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot flush recovery record");
    const bool flushed = FlushFileBuffers(durable_file) != FALSE; CloseHandle(durable_file);
    if (!flushed) throw std::runtime_error("Cannot flush recovery record");
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Cannot persist client grants");
#else
    std::filesystem::rename(temporary, destination);
#endif
    written_revision = revision;
  }
  void persist_locked() { persist_value(snapshot_locked()); }
  bool granted(const std::string& client, const std::string& workspace) const {
    auto it = workspaces.find(workspace); return it != workspaces.end() && !removed_workspaces.contains(workspace) && it->second.clients.contains(client);
  }
  size_t connected_workers_locked() const {
    return static_cast<size_t>(std::count_if(workers.begin(),workers.end(),[](const auto& entry){return entry.second.connected && !entry.second.retiring;}));
  }
  Json worker_limits_locked() const { return {{"connectedWorkers",limits.max_connected_workers},{"disconnectedCache",disconnected_cache},{"totalRetainedWorkers",max_retained_workers}}; }
  Json worker_json(const std::string& id,const Worker& worker) const {
    return {{"agentSessionId",id},{"clientId",worker.client},{"workspaceId",worker.workspace},{"name",worker.name},{"connected",worker.connected},
      {"state",worker.retiring?"retiring":worker.connected?"connected":"disconnected"}};
  }
  bool worker_active_locked(const std::string& id,const std::string& client,const std::string& connection,uint64_t attachment) const {
    const auto found=workers.find(id);
    const auto transport=connections.find(connection);
    return !stopping && found!=workers.end() && !found->second.retiring && found->second.connected && found->second.client==client &&
      found->second.connection==connection && found->second.attachment==attachment && clients.contains(client) && transport!=connections.end() && transport->second==client;
  }
  Json stale_worker_locked(const std::string& id) const {
    auto found=workers.find(id);return failure(found!=workers.end() && found->second.retiring?"SESSION_RETIRED":"SESSION_CHANGED","Worker attachment changed while the request was pending; its result is withheld");
  }
  bool worker_referenced_locked(const std::string& id,const Worker& worker) const {
    if(worker.callbacks)return true;
    for(const auto& [tab_id,tab]:tabs) {
      if(tab.owner==id || (tab.pending_owner && *tab.pending_owner==id))return true;
      for(const auto& command:tab.queue)if(command.session==id)return true;
    }
    return false;
  }
  bool evict_oldest_disconnected_locked() {
    auto oldest=workers.end();
    for(auto it=workers.begin();it!=workers.end();++it)if(!it->second.connected && !it->second.retiring && !worker_referenced_locked(it->first,it->second) &&
      (oldest==workers.end() || it->second.inactive_order<oldest->second.inactive_order))oldest=it;
    if(oldest==workers.end())return false;
    for(auto& [tab_id,tab]:tabs)tab.observations.erase(oldest->first);
    workers.erase(oldest);return true;
  }
  void cleanup_workers_locked() {
    for(auto it=workers.begin();it!=workers.end();) {
      if(it->second.retiring && !worker_referenced_locked(it->first,it->second))it=workers.erase(it);else ++it;
    }
    size_t count=static_cast<size_t>(std::count_if(workers.begin(),workers.end(),[](const auto& entry){return !entry.second.connected && !entry.second.retiring;}));
    while(count>disconnected_cache && evict_oldest_disconnected_locked())--count;
  }
  void finish_callback_locked(const std::string& id) {
    auto found=workers.find(id);if(found!=workers.end() && found->second.callbacks)--found->second.callbacks;
    cleanup_workers_locked();
    schedule_removals_locked();
  }
  void finish_create_locked(const std::string& workspace) {
    auto found = workspaces.find(workspace);
    if (found != workspaces.end() && found->second.pending_creates) --found->second.pending_creates;
    schedule_removals_locked();
  }
  void schedule_removals_locked() {
    if (stopping || removal_check_queued || std::none_of(workspaces.begin(), workspaces.end(), [](const auto& item) { return item.second.removing && !item.second.removal_active && !item.second.removal_retry; })) return;
    removal_check_queued = true;
    std::weak_ptr<Impl> weak = shared_from_this();
    if (!post_io([weak] { if (auto self = weak.lock()) self->progress_removals(); })) removal_check_queued = false;
  }
  void progress_removals() {
    std::vector<std::string> ready;
    {
      std::lock_guard lock(mutex); removal_check_queued = false;
      if (stopping) return;
      for (auto& [id, workspace] : workspaces) {
        if (!workspace.removing || workspace.removal_active || workspace.removal_retry || workspace.pending_creates) continue;
        const bool busy = std::any_of(tabs.begin(), tabs.end(), [&](const auto& item) {
          const auto& tab = item.second;
          return tab.workspace == id && (!tab.active.empty() || !tab.dialog_active.empty() || tab.human_busy);
        });
        if (busy) continue;
        workspace.removal_active = true; ready.push_back(id);
      }
    }
    for (const auto& id : ready) {
      auto self = shared_from_this();
      execute_safe("workspace.remove", {{"workspaceId", id}}, [self, id](Json result) {
        std::vector<Delivery> output;
        {
          std::lock_guard lock(self->mutex); auto found = self->workspaces.find(id);
          if (found == self->workspaces.end()) return;
          auto& workspace = found->second;
          if (result.value("ok", false)) {
            // Engine completion guarantees all tracked and pending native tabs
            // have closed. The durable tombstone remains for startup cleanup.
            std::erase_if(self->tabs, [&](const auto& item) { return item.second.workspace == id; });
            auto value = success({{"workspaceId", id}, {"status", "removed"}, {"profileCleanup", "next_start"}});
            const auto metadata = result.find("result");
            if (metadata != result.end() && metadata->is_object()) {
              if (field(*metadata, "profileCleanup") == "memory_only") value["result"]["profileCleanup"] = "memory_only";
              const auto pending = metadata->find("filePermissionsCleanupPending");
              if (pending != metadata->end() && pending->is_boolean()) value["result"]["filePermissionsCleanupPending"] = *pending;
            }
            for (auto& waiter : workspace.removal_waiters) output.emplace_back(std::move(waiter), value);
            self->workspaces.erase(found); self->cleanup_workers_locked();
          } else {
            workspace.removal_active = false; workspace.removal_retry = true;
            workspace.removal_error = "WORKSPACE_CLOSE_FAILED";
            auto value = failure("WORKSPACE_CLOSE_FAILED", "Workspace access is revoked and removal is saved, but browser closure did not finish. Retry removal or restart the browser to complete cleanup.");
            for (auto& waiter : workspace.removal_waiters) output.emplace_back(std::move(waiter), value);
            workspace.removal_waiters.clear();
          }
        }
        deliver(std::move(output));
      });
    }
  }
  void execute_safe(const std::string& command,const Json& params,Reply reply) {
    auto completed=std::make_shared<std::atomic<bool>>(false);
    auto once=[completed,reply=std::move(reply)](Json value)mutable {
      if(completed->exchange(true))return;
      if(!value.is_object() || (value.contains("ok") && !value["ok"].is_boolean()))value=failure("ENGINE_PROTOCOL","Engine returned an invalid response");
      reply(std::move(value));
    };
    try{engine.execute(command,params,once);}catch(...){once(failure("OUTCOME_UNKNOWN","Engine request ended unexpectedly; inspect existing state before retrying"));}
  }
  void deactivate_worker_locked(const std::string& id,bool retire,std::vector<Delivery>& output) {
    auto found=workers.find(id);if(found==workers.end())return;
    found->second.connected=false;found->second.retiring=found->second.retiring||retire;++found->second.attachment;found->second.inactive_order=++inactive_order;
    std::set<std::string> groups;
    for(auto& [tab_id,tab]:tabs) {tab.observations.erase(id);if(tab.owner==id || (tab.pending_owner && *tab.pending_owner==id))groups.insert(tab.group);}
    for(const auto& group:groups)for(auto& [tab_id,tab]:tabs)if(tab.group==group) {
      std::string recipient;
      if(retire && tab.owner==id && tab.pending_owner && *tab.pending_owner!=id) {
        const auto next=workers.find(*tab.pending_owner);
        if(*tab.pending_owner=="human" || (next!=workers.end() && next->second.connected && !next->second.retiring && granted(next->second.client,tab.workspace)))recipient=*tab.pending_owner;
      }
      begin_transfer_locked(tab,recipient,output,retire?"SESSION_RETIRED":"CLIENT_DISCONNECTED");break;
    }
  }
  bool fresh_after_human_locked(const Tab& tab, const std::string& session) const {
    const auto observed = tab.observations.find(session);
    return !tab.human_busy && observed != tab.observations.end() && observed->second.generation == tab.generation && observed->second.human_epoch == tab.human_epoch;
  }
  Json tab_json(const std::string& id, const Tab& tab, const std::string& session = {}) const {
    const auto& observing_session = session.empty() ? tab.owner : session;
    Json result{{"tabId", id}, {"workspaceId", tab.workspace}, {"controlGroupId", tab.group}, {"ownerSessionId", tab.owner}, {"ownershipGeneration", tab.generation}, {"handoffPending", tab.pending_owner.has_value()}, {"inputBusy", !tab.active.empty() || !tab.dialog_active.empty() || tab.human_busy}, {"protected", tab.protected_auth},
      {"humanActivityEpoch", tab.human_epoch}, {"humanPaused", tab.human_busy}, {"humanPauseUntil", tab.human_pause_until ? Json(*tab.human_pause_until) : Json(nullptr)},
      {"requiresFreshObservation", tab.human_epoch != 0 && !fresh_after_human_locked(tab, observing_session)},
      {"activityMessage", tab.human_busy ? "Human page activity paused agent input; ownership is unchanged. Wait for the pause to end, then observe again." : tab.human_epoch ? "Human page activity occurred; use a fresh observation before continuing." : "No human page activity has been recorded for this tab."}};
    if (tab.pending_owner) result["pendingOwnerSessionId"] = *tab.pending_owner;
    if (!tab.document.empty()) result["documentId"] = tab.document;
    return result;
  }
  void finish_locked(const std::string& id, Json value, std::vector<Delivery>& output, const std::string& final_state = {}) {
    auto it = operations.find(id); if (it == operations.end() || !it->second.result.is_null()) return;
    auto& operation = it->second;
    if (!value.is_object()) value = failure("ENGINE_PROTOCOL", "Engine returned an invalid response");
    operation.state = final_state.empty() ? (value.value("ok", false) ? "completed" : "failed") : final_state;
    if (final_state.empty() && operation.dispatched && !value.value("ok", false)) {
      const auto error = value.find("error");
      const auto code = error != value.end() && error->is_object() ? field(*error, "code") : std::string{};
      if (code == "OUTCOME_UNKNOWN" || code == "input_uncertain" || code == "input_interrupted") operation.state = "outcome_unknown";
    }
    value["operationId"] = id;
    value["dispatchStatus"] = operation.dispatched ? "dispatched" : "not_dispatched";
    operation.result = value;
    for (auto& waiter : operation.waiters) output.emplace_back(std::move(waiter), value);
    operation.waiters.clear();
    persist_async();
  }
  void cancel_queued_locked(Tab& tab, std::vector<Delivery>& output, const std::string& code) {
    while (!tab.queue.empty()) {
      finish_locked(tab.queue.front().operation, failure(code, "Command was cancelled before dispatch"), output, "cancelled");
      tab.queue.pop_front();
    }
  }
  void transfer_if_ready_locked(Tab& tab) {
    if (!tab.pending_owner) return;
    const auto group = tab.group, recipient = *tab.pending_owner;
    for (auto& [id, member] : tabs) if (member.group == group && (!member.active.empty() || !member.dialog_active.empty() || member.human_busy)) return;
    for (auto& [id, member] : tabs) if (member.group == group) {
      member.owner = recipient; member.pending_owner.reset(); ++member.generation;
      member.frozen = removed_workspaces.contains(member.workspace); member.observations.clear();
    }
    // Deliberately no BrowserEngine call: ownership does not touch the page.
  }
  void begin_transfer_locked(Tab& tab, const std::string& recipient, std::vector<Delivery>& output, const std::string& code) {
    const auto group = tab.group;
    for (auto& [id, member] : tabs) if (member.group == group) {
      member.frozen = true; member.pending_owner = recipient; cancel_queued_locked(member, output, code);
    }
    transfer_if_ready_locked(tab);
  }
  void observe_result_locked(Tab& tab, const std::string& session, const Json& value, uint64_t expected_generation) {
    if (tab.human_busy || tab.generation != expected_generation || !value.value("ok", false)) return;
    auto result = value.find("result"); if (result == value.end() || !result->is_object()) return;
    auto id = field(*result, "observationId");
    if (id.empty() && result->contains("observation") && (*result)["observation"].is_object()) id = field((*result)["observation"], "observationId");
    if (!id.empty()) tab.observations[session] = {id, expected_generation, tab.human_epoch};
  }
  void complete_read(const std::string& client, const std::string& workspace, const std::string& tab_id,
                     const std::string& session, const std::string& connection, uint64_t attachment, uint64_t generation, uint64_t state_epoch, const std::string& method, Reply reply, Json value) {
    {
      std::lock_guard lock(mutex); auto it = tabs.find(tab_id);
      if (!clients.contains(client) || !granted(client, workspace)) value = failure("ACCESS_REVOKED", "Workspace access was revoked while the observation was pending");
      else if(!worker_active_locked(session,client,connection,attachment))value=stale_worker_locked(session);
      else if (it == tabs.end() || it->second.workspace != workspace) value = failure("TAB_CLOSED", "Tab closed while observing");
      else if (it->second.protected_auth && (method == "page.observe" || method == "page.screenshot" || method == "page.wait")) value = failure("SENSITIVE_AUTH_IN_PROGRESS", "Detailed observations are paused during protected authentication");
      else if (it->second.human_busy) value = failure("HUMAN_INPUT_PAUSED", "Human page input is still active; wait for the pause to end and observe again");
      else if (it->second.generation != generation || it->second.frozen) value = failure("OWNERSHIP_CHANGED", "Control changed while observing; capture a fresh observation");
      else if (it->second.state_epoch != state_epoch || !it->second.active.empty() || !it->second.dialog_active.empty()) value = failure("OBSERVATION_CHANGED", "The tab changed while observing; capture a fresh observation");
      else observe_result_locked(it->second, session, value, generation);
      finish_callback_locked(session);
    }
    reply(std::move(value));
  }
  void pump(const std::string& tab_id) {
    Command command;
    std::vector<Delivery> output;
    bool execute{};
    {
      std::lock_guard lock(mutex);
      auto it = tabs.find(tab_id); if (it == tabs.end()) return;
      auto& tab = it->second;
      if (stopping || tab.frozen || !tab.active.empty() || !tab.dialog_active.empty() || tab.human_busy) return;
      while (!tab.queue.empty()) {
        command = std::move(tab.queue.front()); tab.queue.pop_front();
        auto worker = workers.find(command.session);
        if (tab.human_epoch != command.human_epoch) {
          finish_locked(command.operation, failure("HUMAN_ACTIVITY", "Human page activity cancelled this command before dispatch; observe again before continuing"), output, "cancelled"); continue;
        }
        if (tab.owner != command.session || tab.generation != command.generation || worker == workers.end() || !worker->second.connected || worker->second.attachment!=command.attachment) {
          finish_locked(command.operation, failure("OWNERSHIP_CHANGED", "Acquire control and observe again"), output, "cancelled"); continue;
        }
        tab.active = command.operation;
        ++worker->second.callbacks;
        auto& operation = operations.at(command.operation); operation.state = "pending_dispatch";
        execute = true; break;
      }
    }
    deliver(std::move(output));
    if (!execute) return;
    dispatch_command(tab_id, std::move(command));
  }
  void dispatch_command(const std::string& tab_id, Command command) {
    auto self = shared_from_this();
    auto completed=std::make_shared<std::atomic<bool>>(false);
    auto done = [self, tab_id, command, completed](Json value) {
      if(!value.is_object() || !value.contains("ok") || !value["ok"].is_boolean())value=failure("OUTCOME_UNKNOWN","Engine returned an invalid completion; inspect operation status and the page before retrying");
      if(completed->exchange(true))return;
      std::vector<Delivery> replies;
      {
        std::lock_guard lock(self->mutex);
        auto it = self->tabs.find(tab_id);
        if (it != self->tabs.end()) {
          auto worker=self->workers.find(command.session);
          if(worker!=self->workers.end() && worker->second.connected && !worker->second.retiring && worker->second.attachment==command.attachment && it->second.human_epoch==command.human_epoch && self->granted(worker->second.client,it->second.workspace))self->observe_result_locked(it->second, command.session, value, command.generation);
          if (it->second.active == command.operation) it->second.active.clear();
          if (it->second.dialog_active == command.operation) it->second.dialog_active.clear();
          self->transfer_if_ready_locked(it->second);
        }
        if (self->removed_workspaces.contains(field(command.params, "workspaceId"))) value = removed_workspace_result(value);
        self->finish_locked(command.operation, std::move(value), replies);
        self->finish_callback_locked(command.session);
      }
      deliver(std::move(replies)); self->pump(tab_id);
    };
    // Write-ahead metadata is flushed on the dedicated I/O worker before the
    // first engine call. No CEF callback performs filesystem I/O.
    const bool posted=post_io([self, tab_id, command, done] {
      std::vector<Delivery> cancelled; bool dispatch{}, persisted{}; Json snapshot;
      {
        std::lock_guard lock(self->mutex);
        auto it = self->tabs.find(tab_id); auto operation = self->operations.find(command.operation);
        if (it == self->tabs.end() || operation == self->operations.end() || !operation->second.result.is_null()) {self->finish_callback_locked(command.session);return;}
        auto& tab = it->second;
        auto worker = self->workers.find(command.session);
        const bool human_changed = tab.human_busy || tab.human_epoch != command.human_epoch;
        if (self->stopping || tab.frozen || human_changed || tab.generation != command.generation || tab.owner != command.session || worker == self->workers.end() || !worker->second.connected || worker->second.attachment!=command.attachment) {
          if (tab.active == command.operation) tab.active.clear();
          if (tab.dialog_active == command.operation) tab.dialog_active.clear();
          self->finish_locked(command.operation, failure(human_changed ? "HUMAN_ACTIVITY" : "OWNERSHIP_CHANGED", "Command was cancelled before dispatch; observe again before continuing"), cancelled, "cancelled"); self->transfer_if_ready_locked(tab);
        } else {
          snapshot = self->snapshot_locked();
        }
      }
      if (!snapshot.is_null()) {
        try { self->persist_value(snapshot); persisted = true; } catch (...) {}
        std::lock_guard lock(self->mutex);
        auto tab = self->tabs.find(tab_id); auto operation = self->operations.find(command.operation);
        auto worker = self->workers.find(command.session);
        if (tab != self->tabs.end() && operation != self->operations.end() && operation->second.result.is_null()) {
          const bool human_changed = tab->second.human_busy || tab->second.human_epoch != command.human_epoch;
          if (!persisted || self->stopping || tab->second.frozen || human_changed || tab->second.generation != command.generation || tab->second.owner != command.session || worker == self->workers.end() || !worker->second.connected || worker->second.attachment!=command.attachment) {
            if (tab->second.active == command.operation) tab->second.active.clear();
            if (tab->second.dialog_active == command.operation) tab->second.dialog_active.clear();
            self->finish_locked(command.operation, failure(!persisted ? "JOURNAL_UNAVAILABLE" : human_changed ? "HUMAN_ACTIVITY" : "OWNERSHIP_CHANGED", persisted ? "Command was cancelled before dispatch; observe again before continuing" : "Command was not dispatched because its recovery record could not be saved"), cancelled, persisted ? "cancelled" : "failed");
            self->transfer_if_ready_locked(tab->second);
          } else { dispatch = true; }
        }
      }
      deliver(std::move(cancelled));
      if (!dispatch) { {std::lock_guard lock(self->mutex);self->finish_callback_locked(command.session);} self->pump(tab_id); return; }
      auto permit = [self, tab_id, command] {
        std::lock_guard lock(self->mutex);
        auto tab = self->tabs.find(tab_id); auto operation = self->operations.find(command.operation); auto worker = self->workers.find(command.session);
        if (tab == self->tabs.end() || operation == self->operations.end() || worker == self->workers.end()) return false;
        if (self->stopping || !operation->second.result.is_null() || !self->clients.contains(worker->second.client) || !self->granted(worker->second.client, tab->second.workspace) || !worker->second.connected || worker->second.attachment!=command.attachment || (tab->second.frozen&&!operation->second.dispatched) || tab->second.human_busy || tab->second.human_epoch!=command.human_epoch || tab->second.owner != command.session || tab->second.generation != command.generation) return false;
        if (tab->second.active != command.operation && tab->second.dialog_active != command.operation) return false;
        // Multi-step engine actions recheck authority before each side effect.
        // Only the first successful check begins a new observation epoch.
        if (!operation->second.dispatched) {
          operation->second.dispatched = true; operation->second.state = "dispatched"; ++tab->second.state_epoch;
        }
        return true;
      };
      try { self->engine.execute_guarded(command.method, command.params, std::move(permit), done); }
      catch (...) { done(failure("OUTCOME_UNKNOWN", "Engine dispatch ended unexpectedly; inspect operation status before retrying")); }
    });
    if(!posted)done(failure("DISPATCH_CANCELLED","Browser dispatcher stopped before this request could run"));
  }
  void on_event(const Json& event) {
    if (!event.is_object()) return;
    std::vector<Delivery> output;
    std::string wake;
    {
      std::lock_guard lock(mutex);
      auto type = field(event, "type"); if (type.empty()) type = field(event, "event");
      const Json& data = event.contains("params") && event["params"].is_object() ? event["params"] : event;
      auto tab_id = field(data, "tabId"); auto tab = tabs.find(tab_id);
      if (type == "system.locked") { locked = data.value("locked", true); return; }
      if (type == "tab.created" && tab == tabs.end()) {
        auto workspace = field(data, "workspaceId"), opener = field(data, "openerTabId");
        if (!workspaces.contains(workspace) || tab_id.empty()) return;
        Tab created; created.workspace = workspace; created.owner = "human"; created.group = tab_id;
        workspaces.at(workspace).ready = !removed_workspaces.contains(workspace);
        auto parent = tabs.find(opener); if (parent != tabs.end() && parent->second.workspace == workspace) {
          created.owner = parent->second.owner; created.group = parent->second.group; created.generation = parent->second.generation;
          created.frozen = parent->second.frozen; created.pending_owner = parent->second.pending_owner;
        }
        if (removed_workspaces.contains(workspace)) { created.frozen = true; created.owner.clear(); created.pending_owner.reset(); }
        tabs.emplace(tab_id, std::move(created)); schedule_removals_locked(); return;
      }
      if (tab == tabs.end()) return;
      if (type == "tab.closed") {
        const auto group = tab->second.group;
        cancel_queued_locked(tab->second, output, "TAB_CLOSED");
        if (!tab->second.active.empty()) finish_locked(tab->second.active, failure("OUTCOME_UNKNOWN", "Tab closed after dispatch"), output, "outcome_unknown");
        if (!tab->second.dialog_active.empty()) finish_locked(tab->second.dialog_active, failure("OUTCOME_UNKNOWN", "Tab closed while handling a dialog"), output, "outcome_unknown");
        tabs.erase(tab);
        for (auto& [id, member] : tabs) if (member.group == group) { transfer_if_ready_locked(member); break; }
      } else if (type == "tab.navigated") {
        tab->second.document = field(data, "documentId"); ++tab->second.state_epoch; tab->second.observations.clear();
      } else if (type == "auth.protected") {
        tab->second.protected_auth = data.value("protected", true); tab->second.observations.clear();
      } else if (type == "human.input") {
        ++tab->second.state_epoch; ++tab->second.human_epoch; tab->second.human_busy = true;
        tab->second.human_pause_until.reset();
        const auto until=data.find("pauseUntil");
        if (!data.value("busy",false) && until!=data.end() && until->is_number_integer() && until->get<int64_t>()>0) tab->second.human_pause_until=until->get<int64_t>();
        tab->second.observations.clear();
        cancel_queued_locked(tab->second, output, "HUMAN_ACTIVITY");
      } else if (type == "human.idle") {
        tab->second.human_busy = false; tab->second.human_pause_until.reset(); transfer_if_ready_locked(tab->second); wake = tab_id;
      }
      cleanup_workers_locked();
      schedule_removals_locked();
    }
    deliver(std::move(output)); if (!wake.empty()) pump(wake);
  }
};

Broker::Broker(BrowserEngine& engine, std::filesystem::path root) : Broker(engine,std::move(root),Limits{}) {}
Broker::Broker(BrowserEngine& engine, std::filesystem::path root, Limits limits) : impl_(std::make_shared<Impl>(engine, std::move(root),limits)) {
  std::weak_ptr<Impl> weak = impl_; engine.set_event_sink([weak](const Json& event) { if (auto instance = weak.lock()) instance->on_event(event); });
}
Broker::~Broker() { stop_all(); impl_->engine.set_event_sink({}); impl_->shutdown_io(); }

void Broker::dispatch(const std::string& connection, const Json& request, Reply reply) {
  auto self = impl_;
  if (!request.is_object() || !request.contains("method") || !request["method"].is_string() || (request.contains("params") && !request["params"].is_object())) { reply(failure("INVALID_REQUEST", "Expected method and object params")); return; }
  const auto method = field(request, "method"); auto params = request.value("params", Json::object());
  std::unique_lock lock(self->mutex);
  auto respond = [&](Json value) { lock.unlock(); reply(std::move(value)); };
  if (self->stopping) { respond(failure("STOPPED", "Browser control is stopped")); return; }
  if (method == "hello") {
    auto client = field(params, "clientId"), token = field(params, "token");
    auto it = self->clients.find(client);
    if (token.size() != 64 || it == self->clients.end() || !local_security::equal_secret(it->second.token_hash, local_security::sha256(token))) { respond(failure("UNAUTHORIZED", "Pair this client in the browser")); return; }
    if (self->connections.contains(connection) && self->connections.at(connection) != client) { respond(failure("CONNECTION_BOUND", "Open a separate connection for a different paired client")); return; }
    self->connections[connection] = client;
    auto limits=self->worker_limits_locked();limits["workers"]=self->limits.max_connected_workers;limits["tabs"]=Impl::max_tabs;limits["queuedPerTab"]=Impl::max_queue;
    respond(success({{"clientId", client}, {"protocolVersion", 1}, {"limits", std::move(limits)}})); return;
  }
  if (method == "pair.request") {
    if (self->connections.contains(connection)) { respond(failure("CONNECTION_BOUND", "This connection is already authenticated")); return; }
    auto name = field(params, "name"); if (name.empty() || name.size() > 120) { respond(failure("INVALID_NAME", "Client name must contain 1 to 120 characters")); return; }
    if (self->pairings.size() >= 16) { respond(failure("CAPACITY_EXCEEDED", "Too many pending pairing requests")); return; }
    for (auto& [id, pending] : self->pairings) if (pending.connection == connection) { respond(failure("PAIRING_PENDING", "This connection already has a pending pairing")); return; }
    self->pairings.emplace(identifier("pair_"), Impl::Pairing{name, connection, std::move(reply)}); return;
  }
  auto authenticated = self->connections.find(connection);
  if (authenticated == self->connections.end()) { respond(failure("UNAUTHORIZED", "Authenticate the local adapter first")); return; }
  const auto client = authenticated->second;
  if (method == "control.activity") {
    Json result = Json::array();
    for (const auto& [id, tab] : self->tabs) {
      const auto owner=self->workers.find(tab.owner);
      if (owner!=self->workers.end() && owner->second.client==client && owner->second.connected && !owner->second.retiring && self->granted(client,tab.workspace))
        result.push_back(self->tab_json(id,tab,tab.owner));
    }
    respond(success({{"tabs",std::move(result)}})); return;
  }
  if (method == "workers.list") {
    Json result = Json::array(); for (auto& [id, worker] : self->workers) if (worker.client == client) {auto row=self->worker_json(id,worker);row.erase("clientId");result.push_back(std::move(row));}
    respond(success({{"workers", result},{"limits",self->worker_limits_locked()},{"connectedWorkerCount",self->connected_workers_locked()}})); return;
  }
  if (method == "workspaces.list") {
    Json result = Json::array(); for (auto& [id, workspace] : self->workspaces) if (self->granted(client, id)) result.push_back({{"workspaceId", id}, {"ready", workspace.ready}});
    respond(success({{"workspaces", result}})); return;
  }
  if (method == "workspaces.share" || method == "workspaces.remove" || method == "workspace.remove") { respond(failure("HUMAN_REQUIRED", "Manage workspaces in the native browser")); return; }
  if (method == "workers.resume") {
    auto id = field(params, "agentSessionId"); auto worker = self->workers.find(id);
    if (worker == self->workers.end() || worker->second.client != client) { respond(failure("SESSION_DENIED", "Unknown or unauthorized worker")); return; }
    if(worker->second.retiring){respond(failure("SESSION_RETIRED","This worker is retiring and cannot resume"));return;}
    if (worker->second.connected && worker->second.connection != connection) { respond(failure("SESSION_CONNECTED", "Worker is attached to another live connection")); return; }
    if(!worker->second.connected && self->connected_workers_locked()>=self->limits.max_connected_workers){respond(failure("CAPACITY_EXCEEDED","Concurrent connected-worker limit reached; retire or disconnect a worker before resuming another"));return;}
    if(!worker->second.connected)++worker->second.attachment;
    worker->second.connection = connection; worker->second.connected = true;
    respond(success({{"agentSessionId", id}, {"workspaceId", worker->second.workspace}, {"state","connected"},{"requiresAcquire", true}})); return;
  }
  if(method=="workers.retire") {
    const auto id=field(params,"agentSessionId");auto worker=self->workers.find(id);
    if(worker==self->workers.end() || worker->second.client!=client){respond(failure("SESSION_DENIED","Unknown or unauthorized worker"));return;}
    if(worker->second.connected && worker->second.connection!=connection){respond(failure("SESSION_CONNECTED","Worker is attached to another live connection"));return;}
    std::vector<Delivery> output;self->deactivate_worker_locked(id,true,output);self->cleanup_workers_locked();
    auto result=success({{"agentSessionId",id},{"status",self->workers.contains(id)?"retiring":"retired"}});
    lock.unlock();deliver(std::move(output));reply(std::move(result));return;
  }
  if (method == "workers.create") {
    self->cleanup_workers_locked();
    if(self->connected_workers_locked()>=self->limits.max_connected_workers){respond(failure("CAPACITY_EXCEEDED","Concurrent connected-worker limit reached; retire or disconnect a worker before creating another"));return;}
    while(self->workers.size()>=Impl::max_retained_workers && self->evict_oldest_disconnected_locked()){}
    if(self->workers.size()>=Impl::max_retained_workers){respond(failure("WORKER_DRAIN_PRESSURE","Worker metadata is temporarily full of connected or draining sessions; wait for accepted operations to finish"));return;}
    auto name = field(params, "name"); if (name.size() > 120) { respond(failure("INVALID_NAME", "Worker name is too long")); return; }
    auto workspace = field(params, "workspaceId"); bool created = workspace.empty();
    if (created) { workspace = identifier("ws_"); self->workspaces.emplace(workspace, Impl::Workspace{{client}, false}); }
    else if (!self->granted(client, workspace)) { respond(failure("WORKSPACE_DENIED", "Workspace access has not been granted")); return; }
    auto session = identifier("agent_"); self->workers.emplace(session, Impl::Worker{client, connection, workspace, name});
    try { if(created)self->persist_locked(); } catch (...) { self->workers.erase(session); if (created) self->workspaces.erase(workspace); respond(failure("PERSIST_FAILED", "Cannot persist workspace grants")); return; }
    auto& created_worker=self->workers.at(session);++created_worker.callbacks;const auto attachment=created_worker.attachment;
    ++self->workspaces.at(workspace).pending_creates;
    lock.unlock();
    self->execute_safe("workspace.ensure", {{"workspaceId", workspace}}, [self, workspace, session, client, connection, attachment, reply](Json value) {
      { std::lock_guard guard(self->mutex);
        if(value.value("ok",false) && self->workspaces.contains(workspace) && !self->removed_workspaces.contains(workspace))self->workspaces.at(workspace).ready=true;
        if(!self->worker_active_locked(session,client,connection,attachment))value=self->stale_worker_locked(session);
        else if(value.value("ok",false))value=success({{"agentSessionId",session},{"workspaceId",workspace},{"state","connected"}});
        else {auto& worker=self->workers.at(session);worker.connected=false;worker.retiring=true;}
        self->finish_callback_locked(session);
        self->finish_create_locked(workspace);
      }
      reply(std::move(value));
    }); return;
  }
  if (method == "operations.get") {
    auto id = field(params, "operationId"); auto operation = self->operations.find(id);
    if (operation == self->operations.end() || operation->second.client != client) { respond(failure("OPERATION_UNKNOWN", "Operation is unavailable; do not assume it was not dispatched")); return; }
    respond(success({{"operationId", id}, {"state", operation->second.state}, {"dispatchStatus", operation->second.dispatched ? "dispatched" : "not_dispatched"}, {"response", operation->second.result}})); return;
  }
  auto session_id = field(params, "agentSessionId"); auto worker = self->workers.find(session_id);
  if(worker!=self->workers.end() && worker->second.client==client && worker->second.retiring){respond(failure("SESSION_RETIRED","This worker is retiring and cannot accept requests"));return;}
  if (worker == self->workers.end() || worker->second.client != client || !worker->second.connected || worker->second.connection != connection) { respond(failure("SESSION_DENIED", "Use a connected worker belonging to this client")); return; }
  auto workspace_id = field(params, "workspaceId");
  if (workspace_id.empty() || !self->granted(client, workspace_id)) { respond(failure("WORKSPACE_DENIED", "Workspace access has not been granted")); return; }
  if (!self->workspaces.at(workspace_id).ready) { respond(failure("WORKSPACE_NOT_READY", "Workspace is not initialized")); return; }
  const auto attachment=worker->second.attachment;
  if (method == "auth.accounts") {
    const auto account_tab = field(params, "tabId"); auto account_tab_it = self->tabs.find(account_tab);
    if (account_tab_it == self->tabs.end() || account_tab_it->second.workspace != workspace_id) { respond(failure("TAB_DENIED", "Unknown tab in this workspace")); return; }
    params["clientId"] = client;++worker->second.callbacks; lock.unlock();
    self->execute_safe(method, params, [self, client, workspace_id, account_tab, session_id, connection, attachment, reply](Json value) {
      {
        std::lock_guard guard(self->mutex); auto tab = self->tabs.find(account_tab);
        if (!self->clients.contains(client) || !self->granted(client, workspace_id)) value = failure("ACCESS_REVOKED", "Workspace access was revoked while the account list was pending");
        else if(!self->worker_active_locked(session_id,client,connection,attachment))value=self->stale_worker_locked(session_id);
        else if (tab == self->tabs.end() || tab->second.workspace != workspace_id) value = failure("TAB_CLOSED", "Tab closed while listing accounts");
        else if (value.value("ok", false)) {
        Json filtered = Json::array();
        if (value.contains("result") && value["result"].is_object()) for (auto& account : value["result"].value("accounts", Json::array())) {
          if (!account.is_object()) continue;
          const auto id = field(account, "accountId"), origin = field(account, "origin");
          for (auto& grant : self->account_grants) if (grant.client == client && grant.workspace == workspace_id && grant.account == id && grant.origin == origin) {
            // Only this documented metadata crosses the credential boundary.
            filtered.push_back({{"accountId", id}, {"origin", origin}, {"label", field(account, "label")}}); break;
          }
        }
        value = success({{"accounts", std::move(filtered)}});
        }
        self->finish_callback_locked(session_id);
      }
      reply(std::move(value));
    }); return;
  }
  if (method == "files.downloads" || method == "files.folders" || method == "files.list") {
    params["clientId"] = client;++worker->second.callbacks; lock.unlock();
    self->execute_safe(method, params, [self, client, workspace_id, session_id, connection, attachment, reply](Json value) {
      { std::lock_guard guard(self->mutex); if (!self->clients.contains(client) || !self->granted(client, workspace_id)) value = failure("ACCESS_REVOKED", "Workspace access was revoked while file metadata was pending");
        else if(!self->worker_active_locked(session_id,client,connection,attachment))value=self->stale_worker_locked(session_id);
        self->finish_callback_locked(session_id);
      }
      reply(std::move(value));
    }); return;
  }
  if (method == "tabs.list") {
    Json result = Json::array(); for (auto& [id, tab] : self->tabs) if (tab.workspace == workspace_id) result.push_back(self->tab_json(id, tab, session_id));
    respond(success({{"tabs", result}})); return;
  }
  if (method == "tabs.create") {
    if (self->tabs.size() >= Impl::max_tabs) { respond(failure("CAPACITY_EXCEEDED", "Tab limit reached")); return; }
    auto url = params.value("url", std::string("about:blank")); if (!safe_web_url(url)) { respond(failure("URL_DENIED", "Only ordinary HTTP and HTTPS pages are allowed")); return; }
    auto id = identifier("tab_"); Impl::Tab tab; tab.workspace = workspace_id; tab.owner = session_id; tab.group = id; self->tabs.emplace(id, std::move(tab));
    ++worker->second.callbacks; ++self->workspaces.at(workspace_id).pending_creates;
    lock.unlock(); self->execute_safe("tabs.create", {{"workspaceId", workspace_id}, {"tabId", id}, {"url", url}}, [self, id, client, workspace_id, connection, attachment, session_id, reply](Json value) {
      { std::lock_guard guard(self->mutex); auto found = self->tabs.find(id);
        if(!value.value("ok",false))self->tabs.erase(id);
        else if(!self->granted(client,workspace_id))value=failure("ACCESS_REVOKED","Workspace access was revoked while the tab was being created");
        else if(!self->worker_active_locked(session_id,client,connection,attachment))value=self->stale_worker_locked(session_id);
        else value=found==self->tabs.end()?failure("TAB_CLOSED","Tab closed while being created"):success(self->tab_json(id,found->second,session_id));
        self->finish_callback_locked(session_id);
        self->finish_create_locked(workspace_id);
      }
      reply(std::move(value));
    }); return;
  }
  auto tab_id = field(params, "tabId"); auto found = self->tabs.find(tab_id);
  if (found == self->tabs.end() || found->second.workspace != workspace_id) { respond(failure("TAB_DENIED", "Unknown tab in this workspace")); return; }
  auto& tab = found->second;
  if (method == "control.status") { respond(success(self->tab_json(tab_id, tab, session_id))); return; }
  if (method == "control.acquire" || method == "control.release" || method == "control.handoff") {
    auto expected = params.find("expectedGeneration");
    if (expected == params.end() || !expected->is_number_integer() || expected->get<int64_t>() < 0 || expected->get<uint64_t>() != tab.generation) { respond(failure("OWNERSHIP_CHANGED", "Read control status before changing ownership")); return; }
    std::string recipient;
    if (method == "control.acquire") {
      if (!tab.owner.empty() && tab.owner != session_id) { respond(failure("TAB_OWNED", "Current owner must release or hand off this tab")); return; }
      recipient = session_id;
    } else {
      if (tab.owner != session_id) { respond(failure("NOT_OWNER", "Only the current owner can hand off control")); return; }
      if (method == "control.handoff") {
        recipient = field(params, "toSessionId"); auto target = self->workers.find(recipient);
        if (target == self->workers.end() || !target->second.connected || !self->granted(target->second.client, workspace_id)) { respond(failure("HANDOFF_DENIED", "Recipient needs a connected session and workspace grant")); return; }
      }
    }
    if (tab.pending_owner) { respond(failure("HANDOFF_PENDING", "A handoff is already pending")); return; }
    std::vector<Delivery> output; self->begin_transfer_locked(tab, recipient, output, "OWNERSHIP_CHANGED");
    auto result = success(self->tab_json(tab_id, tab, session_id)); lock.unlock(); deliver(std::move(output)); reply(std::move(result)); return;
  }
  if (tab.protected_auth && (method == "page.observe" || method == "page.screenshot" || method == "page.wait")) { respond(failure("SENSITIVE_AUTH_IN_PROGRESS", "Detailed observations are paused during protected authentication")); return; }
  if (method == "page.wait") {
    if (tab.human_busy) { respond(failure("HUMAN_INPUT_PAUSED", "Human page input is active; wait for the pause to end before observing")); return; }
    if (!tab.active.empty() || !tab.dialog_active.empty() || tab.frozen) { respond(failure("TAB_BUSY", "Tab is executing input or changing owner")); return; }
    params["clientId"] = client; auto generation = tab.generation, epoch = tab.state_epoch;++worker->second.callbacks; lock.unlock();
    self->execute_safe(method, params, [self, client, workspace_id, tab_id, session_id, connection, attachment, generation, epoch, method, reply](Json value) { self->complete_read(client, workspace_id, tab_id, session_id, connection, attachment, generation, epoch, method, reply, std::move(value)); }); return;
  }
  if (read_commands.contains(method)) {
    if (tab.human_busy) { respond(failure("HUMAN_INPUT_PAUSED", "Human page input is active; wait for the pause to end before observing")); return; }
    if (!tab.active.empty() || !tab.dialog_active.empty() || tab.frozen) { respond(failure("TAB_BUSY", "Tab is executing input or changing owner")); return; }
    params["clientId"] = client; auto generation = tab.generation, epoch = tab.state_epoch;++worker->second.callbacks; lock.unlock();
    self->execute_safe(method, params, [self, client, workspace_id, tab_id, session_id, connection, attachment, generation, epoch, method, reply](Json value) { self->complete_read(client, workspace_id, tab_id, session_id, connection, attachment, generation, epoch, method, reply, std::move(value)); }); return;
  }
  if (!write_commands.contains(method)) { respond(failure("METHOD_UNSUPPORTED", "This browser capability is not implemented")); return; }
  auto operation_id = field(params, "operationId"); if (operation_id.empty()) operation_id = identifier("op_");
  if (operation_id.size() > 160) { respond(failure("INVALID_OPERATION", "Operation ID is too long")); return; }
  params["operationId"] = operation_id; params["clientId"] = client;
  auto signature = method + "\n" + params.dump();
  auto existing = self->operations.find(operation_id);
  if (existing != self->operations.end()) {
    if (existing->second.client != client || (!existing->second.signature.empty() && existing->second.signature != signature)) { respond(failure("OPERATION_CONFLICT", "Operation ID was already used for a different command")); return; }
    if (!existing->second.result.is_null()) { respond(existing->second.result); return; }
    existing->second.waiters.push_back(std::move(reply)); return;
  }
  if (tab.owner != session_id || tab.frozen) { respond(failure("NOT_OWNER", "Acquire this tab before acting")); return; }
  if (tab.human_busy) { respond(failure("HUMAN_INPUT_PAUSED", "Human page input temporarily paused this tab; ownership is unchanged. Wait for idle, then observe again")); return; }
  if (!params.contains("ownershipGeneration") || !params["ownershipGeneration"].is_number_unsigned() || params["ownershipGeneration"].get<uint64_t>() != tab.generation) { respond(failure("OWNERSHIP_CHANGED", "Read control status and observe again")); return; }
  if (method == "auth.login" && self->locked) { respond(failure("VAULT_LOCKED", "Unlock the vault in the native browser")); return; }
  if (method == "auth.login") {
    const auto account = field(params, "accountId"); std::string origin;
    for (auto& grant : self->account_grants) if (grant.client == client && grant.workspace == workspace_id && grant.account == account) { origin = grant.origin; break; }
    if (origin.empty()) { respond(failure("ACCOUNT_DENIED", "Grant this account to the client and workspace in the native browser")); return; }
    params["grantedOrigin"] = origin;
  }
  if (method == "page.navigate" && !safe_web_url(field(params, "url"))) { respond(failure("URL_DENIED", "Only ordinary HTTP and HTTPS pages are allowed")); return; }
  if (tab.human_epoch && method!="tabs.close" && method!="page.dialog" && !self->fresh_after_human_locked(tab,session_id)) {
    respond(failure("OBSERVATION_REQUIRED", "Human page activity invalidated prior evidence. Capture a fresh observation before continuing")); return;
  }
  if (target_commands.contains(method)) {
    auto observed = tab.observations.find(session_id);
    if (observed == tab.observations.end() || observed->second.generation != tab.generation || observed->second.human_epoch != tab.human_epoch || observed->second.id != field(params, "observationId")) { respond(failure("OBSERVATION_REQUIRED", "Capture a fresh observation after acquiring this tab or human page activity")); return; }
  }
  if (method == "page.dialog" ? !tab.dialog_active.empty() : tab.queue.size() >= Impl::max_queue) { respond(failure("CAPACITY_EXCEEDED", "This tab's command queue or dialog reservation is full")); return; }
  size_t worker_pending{};
  for (auto& [id, candidate] : self->tabs) { if (candidate.owner == session_id) { if (!candidate.active.empty()) ++worker_pending; if (!candidate.dialog_active.empty()) ++worker_pending; } for (auto& item : candidate.queue) if (item.session == session_id) ++worker_pending; }
  if (worker_pending >= Impl::max_worker_pending) { respond(failure("CAPACITY_EXCEEDED", "This worker's outstanding command budget is full; allow other workers to make progress")); return; }
  while (self->operations.size() >= Impl::max_journal && !self->operation_order.empty()) {
    auto candidate = self->operation_order.front(); auto op = self->operations.find(candidate);
    if (op != self->operations.end() && op->second.result.is_null()) break;
    self->operation_order.pop_front(); self->operations.erase(candidate);
  }
  if (self->operations.size() >= Impl::max_journal) { respond(failure("CAPACITY_EXCEEDED", "Operation journal is full")); return; }
  Impl::Operation operation; operation.client = client; operation.tab = tab_id; operation.workspace = workspace_id; operation.method = method; operation.signature = signature; operation.waiters.push_back(std::move(reply));
  self->operations.emplace(operation_id, std::move(operation)); self->operation_order.push_back(operation_id);
  if (method == "page.dialog") {
    // A JavaScript dialog can hold the reply to the input which opened it.
    // Only the current unfrozen owner may use this single bounded side lane;
    // it is still journaled and included in the control-group input barrier.
    tab.dialog_active = operation_id; self->operations.at(operation_id).state = "pending_dispatch";
    ++worker->second.callbacks;
    Impl::Command command{operation_id, session_id, method, tab.generation, attachment, tab.human_epoch, params};
    lock.unlock(); self->dispatch_command(tab_id, std::move(command)); return;
  }
  tab.queue.push_back({operation_id, session_id, method, tab.generation, attachment, tab.human_epoch, params});
  lock.unlock(); self->pump(tab_id);
}

Json Broker::state() const {
  auto self = impl_; std::lock_guard lock(self->mutex);
  Json result{{"clients", Json::array()}, {"pendingRevocations", Json::array()}, {"pairings", Json::array()}, {"workers", Json::array()}, {"workspaces", Json::array()}, {"tabs", Json::array()}, {"accountGrants", Json::array()}, {"stopped", self->stopping}};
  for (auto& [id, client] : self->clients) result["clients"].push_back({{"clientId", id}, {"name", client.name}});
  for (auto& [id, name] : self->pending_revocations) result["pendingRevocations"].push_back({{"clientId", id}, {"name", name}});
  for (auto& [id, pair] : self->pairings) result["pairings"].push_back({{"requestId", id}, {"name", pair.name}});
  for (auto& [id, worker] : self->workers) result["workers"].push_back(self->worker_json(id,worker));
  result["limits"]=self->worker_limits_locked();result["connectedWorkerCount"]=self->connected_workers_locked();
  for (auto& [id, workspace] : self->workspaces) {
    Json row{{"workspaceId", id}, {"clientIds", workspace.clients}, {"ready", workspace.ready}, {"private", workspace.private_mode}, {"removing", workspace.removing}};
    if (workspace.removing) row["removalStatus"] = workspace.removal_retry ? "retry" : workspace.removal_active ? "closing" : "draining";
    if (!workspace.removal_error.empty()) row["removalError"] = workspace.removal_error;
    result["workspaces"].push_back(std::move(row));
  }
  for (auto& [id, tab] : self->tabs) result["tabs"].push_back(self->tab_json(id, tab));
  for (auto& grant : self->account_grants) result["accountGrants"].push_back({{"clientId", grant.client}, {"workspaceId", grant.workspace}, {"accountId", grant.account}, {"origin", grant.origin}});
  return result;
}
bool Broker::approve_pairing(const std::string& id) {
  auto self = impl_; Reply reply; Json result;
  { std::lock_guard lock(self->mutex); auto pair = self->pairings.find(id); if (pair == self->pairings.end()) return false;
    auto client = identifier("client_"), token = local_security::random_hex(32);
    self->clients.emplace(client, Impl::Client{pair->second.name, local_security::sha256(token)});
    try { self->persist_locked(); } catch (...) { self->clients.erase(client); return false; }
    self->connections[pair->second.connection] = client; reply = std::move(pair->second.reply); self->pairings.erase(pair);
    result = success({{"clientId", client}, {"token", token}});
  } if (reply) reply(std::move(result)); return true;
}
bool Broker::deny_pairing(const std::string& id) {
  Reply reply; { std::lock_guard lock(impl_->mutex); auto found = impl_->pairings.find(id); if (found == impl_->pairings.end()) return false; reply = std::move(found->second.reply); impl_->pairings.erase(found); }
  if (reply) reply(failure("PAIRING_DENIED", "Pairing was declined in the browser")); return true;
}
bool Broker::share_workspace(const std::string& workspace, const std::string& client) {
  std::lock_guard lock(impl_->mutex); auto found = impl_->workspaces.find(workspace); if (found == impl_->workspaces.end() || impl_->removed_workspaces.contains(workspace) || found->second.private_mode || !impl_->clients.contains(client)) return false;
  bool inserted = found->second.clients.insert(client).second;
  try { impl_->persist_locked(); } catch (...) { if (inserted) found->second.clients.erase(client); return false; } return true;
}
bool Broker::grant_account(const std::string& client, const std::string& workspace, const std::string& account, const std::string& origin) {
  if (account.empty() || account.size() > 160 || !exact_https_origin(origin)) return false;
  std::lock_guard lock(impl_->mutex); if (!impl_->granted(client, workspace)) return false;
  for (auto& grant : impl_->account_grants) if (grant.client == client && grant.workspace == workspace && grant.account == account && grant.origin == origin) return true;
  impl_->account_grants.push_back({client, workspace, account, origin});
  try { impl_->persist_locked(); } catch (...) { impl_->account_grants.pop_back(); return false; } return true;
}
Broker::RevocationStatus Broker::revoke_client(const std::string& client) {
  std::vector<std::string> connections;
  auto status = RevocationStatus::pending;
  { std::lock_guard lock(impl_->mutex);
    auto found = impl_->clients.find(client);
    if (found == impl_->clients.end() && !impl_->pending_revocations.contains(client)) return RevocationStatus::missing;
    if (found != impl_->clients.end()) {
      impl_->pending_revocations[client] = found->second.name;
      impl_->clients.erase(found);
    }
    for (auto& [id, workspace] : impl_->workspaces) workspace.clients.erase(client);
    std::erase_if(impl_->account_grants, [&](auto& grant) { return grant.client == client; });
    for (auto& [id, owner] : impl_->connections) if (owner == client) connections.push_back(id);
    try {
      impl_->persist_locked(); impl_->pending_revocations.erase(client); status = RevocationStatus::durable;
    } catch (...) { /* Keep denial active and expose the unsaved revocation for retry. */ }
  }
  for (auto& connection : connections) disconnect(connection);
  return status;
}
std::vector<std::string> Broker::removed_workspaces() const {
  std::lock_guard lock(impl_->mutex);
  return {impl_->removed_workspaces.begin(), impl_->removed_workspaces.end()};
}
void Broker::remove_workspace(const std::string& id, Reply reply) {
  auto self = impl_; std::vector<Delivery> output;
  {
    std::lock_guard lock(self->mutex);
    if (id == "native-default") {
      output.emplace_back(std::move(reply), failure("WORKSPACE_PROTECTED", "Personal is the browser's default workspace and cannot be removed. Remove another workspace instead."));
    } else if (self->stopping) {
      output.emplace_back(std::move(reply), failure("STOPPED", "Browser control is stopped; saved removals will be cleaned up at the next start"));
    } else if (auto found = self->workspaces.find(id); found != self->workspaces.end()) {
      auto& workspace = found->second;
      bool durable = workspace.removing;
      if (!durable) {
        self->removed_workspaces.insert(id);
        // A single atomic recovery record removes grants and adds the marker.
        // No live authorization or engine state changes until it is durable.
        try { self->persist_locked(); durable = true; }
        catch (...) {
          self->removed_workspaces.erase(id);
          output.emplace_back(std::move(reply), failure("PERSISTENCE_FAILED", "Workspace was not removed because its recovery record could not be saved. Retry after storage is available."));
        }
      }
      if (durable) {
        workspace.removing = true; workspace.ready = false;
        workspace.removal_retry = false; workspace.removal_error.clear();
        workspace.removal_waiters.push_back(std::move(reply)); workspace.clients.clear();
        std::erase_if(self->account_grants, [&](const auto& grant) { return grant.workspace == id; });
        for (auto& [operation_id, operation] : self->operations) {
          if (operation.workspace == id && !operation.result.is_null()) operation.result = removed_workspace_result(operation.result);
        }
        std::vector<std::string> retired;
        for (const auto& [session, worker] : self->workers) if (worker.workspace == id && !worker.retiring) retired.push_back(session);
        for (const auto& session : retired) self->deactivate_worker_locked(session, true, output);
        std::set<std::string> groups;
        for (auto& [tab_id, tab] : self->tabs) if (tab.workspace == id && groups.insert(tab.group).second)
          self->begin_transfer_locked(tab, "", output, "WORKSPACE_REMOVED");
        self->cleanup_workers_locked(); self->schedule_removals_locked();
      }
    } else {
      output.emplace_back(std::move(reply), self->removed_workspaces.contains(id)
        ? success({{"workspaceId", id}, {"status", "removed"}, {"profileCleanup", "next_start"}})
        : failure("WORKSPACE_UNKNOWN", "Workspace is no longer available"));
    }
  }
  deliver(std::move(output));
}
void Broker::human_acquire(const std::string& id) {
  std::vector<Delivery> output; { std::lock_guard lock(impl_->mutex); auto found = impl_->tabs.find(id); if (found == impl_->tabs.end()) return;
    if (impl_->removed_workspaces.contains(found->second.workspace)) return;
    impl_->begin_transfer_locked(found->second, "human", output, "HUMAN_CONTROL");
  } deliver(std::move(output));
}
void Broker::human_release(const std::string& id, const std::string& session) {
  std::vector<Delivery> output; { std::lock_guard lock(impl_->mutex); auto found = impl_->tabs.find(id); auto target = impl_->workers.find(session);
  if (found == impl_->tabs.end() || found->second.owner != "human" || target == impl_->workers.end() || !target->second.connected || !impl_->granted(target->second.client, found->second.workspace)) return;
  impl_->begin_transfer_locked(found->second, session, output, "OWNERSHIP_CHANGED"); } deliver(std::move(output));
}
void Broker::disconnect(const std::string& connection) {
  std::vector<Delivery> output; { std::lock_guard lock(impl_->mutex); impl_->connections.erase(connection);
    for (auto it = impl_->pairings.begin(); it != impl_->pairings.end();) { if (it->second.connection == connection) it = impl_->pairings.erase(it); else ++it; }
    std::vector<std::string> sessions;for(const auto& [id,worker]:impl_->workers)if(worker.connection==connection && worker.connected)sessions.push_back(id);
    for(const auto& id:sessions)impl_->deactivate_worker_locked(id,false,output);
    impl_->cleanup_workers_locked();
  } deliver(std::move(output));
}
void Broker::stop_all() {
  std::vector<Delivery> output; { std::lock_guard lock(impl_->mutex); impl_->stopping = true;
    std::set<std::string> groups;
    for (auto& [id, tab] : impl_->tabs) if (groups.insert(tab.group).second) impl_->begin_transfer_locked(tab, "human", output, "STOPPED");
    for (auto& [id, pair] : impl_->pairings) output.emplace_back(std::move(pair.reply), failure("STOPPED", "Browser control stopped")); impl_->pairings.clear();
    for (auto& [id, workspace] : impl_->workspaces) {
      for (auto& waiter : workspace.removal_waiters) output.emplace_back(std::move(waiter), failure("REMOVAL_PENDING_RESTART", "Workspace removal is saved and will finish during the next browser start"));
      workspace.removal_waiters.clear();
    }
  } deliver(std::move(output));
}
void Broker::open_human_workspace(const std::string& url, Reply reply, bool private_mode) { open_human_workspace_impl(url, std::move(reply), private_mode, false); }
void Broker::open_initial_human_workspace(const std::string& url, Reply reply) { open_human_workspace_impl(url, std::move(reply), false, true); }
void Broker::open_human_workspace_impl(const std::string& url, Reply reply, bool private_mode, bool initial) {
  if (!safe_web_url(url)) { reply(failure("URL_DENIED", "Only ordinary web pages are allowed")); return; }
  auto self = impl_; auto workspace = initial ? std::string("native-default") : identifier("ws_"), id = identifier("tab_");
  Json rejected;
  {
    std::lock_guard lock(self->mutex);
    if (self->stopping || self->removed_workspaces.contains(workspace)) rejected = failure("WORKSPACE_UNAVAILABLE", "Workspace cannot be opened");
    else {
      const auto [entry, inserted] = self->workspaces.try_emplace(workspace, Impl::Workspace{{}, false, private_mode});
      Impl::Tab tab; tab.workspace = workspace; tab.owner = "human"; tab.group = id; self->tabs.emplace(id, std::move(tab));
      try { self->persist_locked(); ++entry->second.pending_creates; }
      catch (...) { self->tabs.erase(id); if (inserted) self->workspaces.erase(entry); rejected = failure("PERSISTENCE_FAILED", "Workspace could not be saved"); }
    }
  }
  if (!rejected.is_null()) { reply(std::move(rejected)); return; }
  self->execute_safe("workspace.ensure", {{"workspaceId", workspace}, {"private", private_mode}}, [self, workspace, id, url, private_mode, reply](Json result) {
    {
      std::lock_guard lock(self->mutex);
      if (self->stopping || self->removed_workspaces.contains(workspace) || !self->workspaces.contains(workspace))
        result = failure("WORKSPACE_REMOVED", "Workspace was removed or closed while initialization was pending");
      if (!result.value("ok", false)) { self->tabs.erase(id); self->finish_create_locked(workspace); }
      else self->workspaces.at(workspace).ready = true;
    }
    if (!result.value("ok", false)) { reply(std::move(result)); return; }
    self->execute_safe("tabs.create", {{"workspaceId", workspace}, {"tabId", id}, {"url", url}, {"private", private_mode}}, [self, workspace, id, reply](Json value) {
      {
        std::lock_guard lock(self->mutex);
        if (!value.value("ok", false)) self->tabs.erase(id);
        if (self->stopping || self->removed_workspaces.contains(workspace)) value = failure("WORKSPACE_REMOVED", "Workspace was removed or closed while the tab was being created");
        self->finish_create_locked(workspace);
      }
      reply(std::move(value));
    });
  });
}
}
