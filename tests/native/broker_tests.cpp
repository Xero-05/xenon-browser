#include "xenon/broker.hpp"
#include "xenon/local_security.hpp"
#include "xenon/pipe_server.hpp"
#include "xenon/dialog_notices.hpp"
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace xenon;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string code(const Json& value) { return value.contains("error") ? value["error"].value("code", std::string{}) : std::string{}; }
void human_dialog_notices() {
  const auto tab=[](const char* id,const char* owner,bool protected_auth=false){return Json{{"tabId",id},{"ownerSessionId",owner},{"protected",protected_auth}};};
  const auto dialog=[](const char* id){return Json{{"tabId",id},{"type","confirm"},{"message","Synthetic logout confirmation"},{"origin","https://fixture.example.invalid"}};};
  Json state{{"tabs",Json::array({tab("human","human",true),tab("agent","worker-one"),tab("idle","")})}};
  Json dialogs=Json::array({dialog("human"),dialog("agent"),dialog("idle"),dialog("missing")});
  DialogNotices notices;
  for(const char* id:{"agent","idle","missing","human","human"})notices.notify(id);
  require(!notices.take(state,dialogs,true),"Held physical input postpones dialog surfacing");
  auto notice=notices.take(state,dialogs,false);
  require(notice&&notice->tab_id=="human"&&notice->protected_auth,"Only the current human-owned dialog surfaces, preserving protection context");
  require(notice->type=="confirm"&&notice->message=="Synthetic logout confirmation"&&notice->origin=="https://fixture.example.invalid","Native notice preserves exact dialog context");
  require(!notices.take(state,dialogs,false),"Repeated polling does not reopen an already surfaced dialog");

  DialogNotices ownership_race;ownership_race.notify("human");state["tabs"][0]["ownerSessionId"]="worker-two";
  require(!ownership_race.take(state,dialogs,false),"A human-to-agent handoff before delivery prevents any native notice");
  state["tabs"][0]["ownerSessionId"]="human";
  require(!ownership_race.take(state,dialogs,false),"A discarded ownership-race notice is not replayed after a later takeover");
  DialogNotices close_race;close_race.notify("human");
  require(!close_race.take(state,Json::array({dialog("agent")}),false),"A dialog answered before delivery does not open Controls");
  close_race.notify("missing");require(!close_race.take(state,dialogs,false),"A closed or unknown tab cannot open Controls");

  DialogNotices sequence;state["tabs"].push_back(tab("second","human"));dialogs.push_back(dialog("second"));
  sequence.notify("human");sequence.notify("second");
  require(sequence.take(state,dialogs,false)->tab_id=="human","The first pending human dialog is selected");
  require(!sequence.take(state,dialogs,false),"A second tab does not replace a dialog while the user is reading it");
  auto next=sequence.take(state,Json::array({dialog("second")}),false);
  require(next&&next->tab_id=="second","The next dialog is surfaced after the previous one is answered");
  sequence.notify("second");
  require(sequence.take(state,Json::array({dialog("second")}),false).has_value(),"A new chained dialog in the same tab can surface again");

  DialogNotices paused_worker;
  Json worker_state{{"tabs",Json::array({{{"tabId","paused-worker"},{"ownerSessionId","worker-one"},{"humanPaused",true},{"protected",false}},tab("unpaused-worker","worker-two")})}};
  const auto worker_dialogs=Json::array({dialog("paused-worker"),dialog("unpaused-worker")});
  paused_worker.notify("unpaused-worker");paused_worker.notify("paused-worker");
  const auto paused_notice=paused_worker.take(worker_state,worker_dialogs,false);
  require(paused_notice&&paused_notice->tab_id=="paused-worker","A human-triggered dialog surfaces on a physically paused agent-owned tab");
  require(!paused_worker.take(worker_state,worker_dialogs,false),"Unpaused agent-owned dialogs do not surface as human activity");
}
struct FakeEngine final : BrowserEngine {
  struct Pending { std::string command; Json params; Reply reply; };
  EventSink sink;
  std::vector<std::string> calls;
  std::map<std::string, Pending> pending;
  std::mutex mutex;
  unsigned observations{};
  bool delay_observe{};
  bool delay_metadata{};
  bool delay_guard{};
  std::string delay_create,throw_command;
  std::vector<Pending> held_creates;
  struct Guarded { std::string command; Json params; std::function<bool()> permit; Reply reply; };
  std::map<std::string, Guarded> guards;
  std::map<std::string, Reply> held_metadata;
  std::map<std::string, Reply> held_removals;
  Reply held_observation;
  Json held_observation_value;
  void set_event_sink(EventSink target) override { sink = std::move(target); }
  void execute_guarded(const std::string& command, const Json& params, std::function<bool()> permit, Reply reply) override {
    if (!delay_guard) { BrowserEngine::execute_guarded(command, params, std::move(permit), std::move(reply)); return; }
    std::lock_guard lock(mutex); guards.emplace(params.at("operationId").get<std::string>(), Guarded{command, params, std::move(permit), std::move(reply)});
  }
  void wait_guard(const std::string& id) {
    for (unsigned i = 0; i < 1000; ++i) { { std::lock_guard lock(mutex); if (guards.contains(id)) return; } std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    throw std::runtime_error("Timed out waiting for engine UI dispatch queue");
  }
  void dispatch_guard(const std::string& id) {
    Guarded value; { std::lock_guard lock(mutex); auto it = guards.find(id); require(it != guards.end(), "Expected queued UI dispatch"); value = std::move(it->second); guards.erase(it); }
    if (!value.permit()) { value.reply(failure("DISPATCH_CANCELLED", "Control changed before dispatch")); return; }
    execute(value.command, value.params, std::move(value.reply));
  }
  std::function<bool()> guard_permit(const std::string& id) {
    std::lock_guard lock(mutex); return guards.at(id).permit;
  }
  void execute(const std::string& command, const Json& params, Reply reply) override {
    { std::lock_guard lock(mutex); calls.push_back(command); }
    if(command==throw_command)throw std::runtime_error("Synthetic engine failure");
    if(command=="workspace.remove") { std::lock_guard lock(mutex); held_removals.emplace(params.at("workspaceId").get<std::string>(),std::move(reply)); return; }
    if(command==delay_create){held_creates.push_back({command,params,std::move(reply)});return;}
    if (command == "workspace.ensure" || command == "tabs.create") { reply(success(params)); return; }
    if (command == "page.observe") {
      auto value = success({{"observationId", "snapshot_" + std::to_string(++observations)}, {"nodes", Json::array()}});
      if (delay_observe) { held_observation = std::move(reply); held_observation_value = std::move(value); } else reply(std::move(value)); return;
    }
    if (command == "page.screenshot") { reply(success({{"mimeType", "image/png"}, {"data", "test"}})); return; }
    if ((command == "auth.accounts" || command == "files.downloads") && delay_metadata) { held_metadata.emplace(command, std::move(reply)); return; }
    if (command == "auth.accounts") { reply(success({{"accounts", Json::array({{{"accountId", "allowed-account"}, {"origin", "https://example.test"}, {"label", "Allowed"}, {"username", "must-not-cross"}}, {{"accountId", "other-account"}, {"origin", "https://other.test"}, {"label", "Other"}}})}})); return; }
    if (!params.contains("operationId")) { reply(failure("UNSUPPORTED", "Fake engine deliberately rejects unsupported commands")); return; }
    std::lock_guard lock(mutex); pending.emplace(params.at("operationId").get<std::string>(), Pending{command, params, std::move(reply)});
  }
  size_t call_count() { std::lock_guard lock(mutex); return calls.size(); }
  size_t removal_count() { std::lock_guard lock(mutex); return held_removals.size(); }
  size_t command_count(const std::string& command) { std::lock_guard lock(mutex); return static_cast<size_t>(std::count(calls.begin(),calls.end(),command)); }
  void wait_removal() { for(unsigned i=0;i<1000;++i){if(removal_count())return;std::this_thread::sleep_for(std::chrono::milliseconds(5));}throw std::runtime_error("Timed out waiting for workspace removal"); }
  void complete_removal(const std::string& workspace,Json result=success()) {
    Reply reply; {std::lock_guard lock(mutex);auto found=held_removals.find(workspace);require(found!=held_removals.end(),"Expected a workspace closure request");reply=std::move(found->second);held_removals.erase(found);}reply(std::move(result));
  }
  size_t pending_count() { std::lock_guard lock(mutex); return pending.size(); }
  void wait_pending(size_t count) { for (unsigned i = 0; i < 1000; ++i) { if (pending_count() == count) return; std::this_thread::sleep_for(std::chrono::milliseconds(5)); } throw std::runtime_error("Timed out waiting for concurrent engine dispatch"); }
  void complete(const std::string& id, Json result = success()) {
    Reply reply;
    { std::lock_guard lock(mutex); auto it = pending.find(id); require(it != pending.end(), "Expected a dispatched engine operation"); reply = std::move(it->second.reply); pending.erase(it); }
    reply(std::move(result));
  }
  void event(const std::string& type, Json data) { data["type"] = type; if (sink) sink(data); }
  void complete_observation() { auto reply = std::move(held_observation); require(static_cast<bool>(reply), "Expected an observation in flight"); reply(held_observation_value); }
  Json pending_params(const std::string& id) { std::lock_guard lock(mutex); return pending.at(id).params; }
  void complete_create(){require(!held_creates.empty(),"Expected delayed creation");auto held=std::move(held_creates.front());held_creates.erase(held_creates.begin());held.reply(success(held.params));}
};
Json call(Broker& broker, const std::string& connection, const std::string& method, Json params = Json::object()) {
  Json output;
  broker.dispatch(connection, {{"method", method}, {"params", params}}, [&](Json value) { output = std::move(value); });
  require(!output.is_null(), "Expected a synchronous control result"); return output;
}
Json pair(Broker& broker, const std::string& connection, const std::string& name) {
  Json output;
  broker.dispatch(connection, {{"method", "pair.request"}, {"params", {{"name", name}}}}, [&](Json value) { output = std::move(value); });
  require(output.is_null(), "Pairing must await native approval");
  auto pending = broker.state()["pairings"];
  require(pending.size() == 1, "Pairing appears in native state");
  require(broker.approve_pairing(pending[0]["requestId"]), "Native pairing approval succeeds");
  require(output.value("ok", false), "Pairing returns credentials only after approval");
  return output["result"];
}
struct Worker {
  std::string connection, session, workspace;
  Json tab;
  Json base() const { return {{"agentSessionId", session}, {"workspaceId", workspace}, {"tabId", tab.value("tabId", std::string{})}}; }
};
Worker worker(Broker& broker, const std::string& connection, const std::string& name, const std::string& workspace = {}) {
  Json params{{"name", name}}; if (!workspace.empty()) params["workspaceId"] = workspace;
  auto result = call(broker, connection, "workers.create", params); require(result.value("ok", false), "Worker creation succeeds");
  Worker value{connection, result["result"]["agentSessionId"], result["result"]["workspaceId"], Json::object()};
  auto tab = call(broker, connection, "tabs.create", {{"agentSessionId", value.session}, {"workspaceId", value.workspace}, {"url", "https://example.test/"}});
  require(tab.value("ok", false), "Tab creation succeeds"); value.tab = tab["result"]; return value;
}
Json action_params(Broker& broker, const Worker& worker, const std::string& operation) {
  auto base = worker.base(); auto status = call(broker, worker.connection, "control.status", base); require(status.value("ok", false), "Control status succeeds");
  auto snapshot = call(broker, worker.connection, "page.observe", base); require(snapshot.value("ok", false), "Observation succeeds");
  base["operationId"] = operation; base["ownershipGeneration"] = status["result"]["ownershipGeneration"];
  base["observationId"] = snapshot["result"]["observationId"]; base["elementRef"] = "node_1"; return base;
}
Json create_only(Broker& broker,const std::string& connection,const std::string& workspace={}) {
  Json params{{"name","lifecycle"}};if(!workspace.empty())params["workspaceId"]=workspace;
  return call(broker,connection,"workers.create",params);
}
Json worker_row(Broker& broker,const std::string& id) {
  const auto state=broker.state();for(const auto& row:state["workers"])if(row["agentSessionId"]==id)return row;return Json{};
}
void worker_capacity_and_churn(const std::filesystem::path& directory) {
  for(size_t invalid:{size_t{0},size_t{257}}){FakeEngine e;bool rejected=false;try{Broker b(e,directory/"invalid",Broker::Limits{invalid});}catch(const std::invalid_argument&){rejected=true;}require(rejected,"Worker configuration accepts only 1..256");}
  {FakeEngine e;Broker b(e,directory/"maximum",Broker::Limits{256});require(b.state()["limits"]["connectedWorkers"]==256,"Upper configured worker limit is supported");}
  FakeEngine engine;Broker broker(engine,directory/"churn",Broker::Limits{1});auto principal=pair(broker,"a","Owner"),other=pair(broker,"b","Other");
  require(call(broker,"same-client","hello",principal).value("ok",false),"Same client may authenticate a separate connection");
  std::string workspace;Json retained_tab;
  for(int i=0;i<1025;++i){
    auto created=create_only(broker,"a",workspace);require(created.value("ok",false),"Retirement/recreation churn must not hit a lifetime quota");
    const auto id=created["result"]["agentSessionId"].get<std::string>();workspace=created["result"]["workspaceId"];
    if(i==0){
      require(code(create_only(broker,"b"))=="CAPACITY_EXCEEDED","Concurrent cap is global across clients");
      require(code(call(broker,"b","workers.retire",{{"agentSessionId",id}}))=="SESSION_DENIED","Another client cannot retire a worker");
      require(code(call(broker,"same-client","workers.retire",{{"agentSessionId",id}}))=="SESSION_CONNECTED","Another live attachment cannot retire the worker");
      require(call(broker,"a","workers.resume",{{"agentSessionId",id}}).value("ok",false),"Idempotent same-attachment resume works at full capacity");
      auto tab=call(broker,"a","tabs.create",{{"agentSessionId",id},{"workspaceId",workspace}});retained_tab=tab["result"];
      engine.event("tab.navigated",{{"tabId",retained_tab["tabId"]},{"documentId","unchanged-live-document"}});
      const auto info=call(broker,"a","workers.list")["result"];
      require(info["limits"]["connectedWorkers"]==1 && info["limits"]["disconnectedCache"]==256 && info["connectedWorkerCount"]==1,"Worker list exposes configured admission limits");
    }
    const auto calls=engine.call_count();const auto retired=call(broker,"a","workers.retire",{{"agentSessionId",id}});
    require(retired["result"]["status"]=="retired" && broker.state()["workers"].empty(),"Quiescent retirement immediately erases only worker metadata");
    require(engine.call_count()==calls,"Retirement makes zero browser engine calls");
    require(code(call(broker,"a","workers.resume",{{"agentSessionId",id}}))=="SESSION_DENIED","Retired worker cannot resume");
    const auto tabs=broker.state()["tabs"];require(tabs.size()==1 && tabs[0]["tabId"]==retained_tab["tabId"] && tabs[0]["documentId"]=="unchanged-live-document" && tabs[0]["ownerSessionId"]=="","Churn preserves live tab identity/document/workspace");
  }
  std::string oldest,recent;
  for(int i=0;i<260;++i){
    auto created=create_only(broker,"a",workspace);require(created.value("ok",false),"Disconnected churn retains reusable concurrent admission");
    recent=created["result"]["agentSessionId"];if(!i)oldest=recent;
    broker.disconnect("a");require(call(broker,"a","hello",principal).value("ok",false),"Fixture reconnect succeeds");
    require(broker.state()["workers"].size()<=256,"Disconnected quiescent cache is bounded");
  }
  require(code(call(broker,"a","workers.resume",{{"agentSessionId",oldest}}))=="SESSION_DENIED","Oldest quiescent disconnected handle is evicted");
  require(call(broker,"a","workers.resume",{{"agentSessionId",recent}}).value("ok",false),"Newest disconnected handle remains resumable");
  broker.disconnect("a");call(broker,"a","hello",principal);auto fresh=create_only(broker,"a",workspace);require(fresh.value("ok",false),"Disconnect frees concurrent capacity");
  require(code(call(broker,"a","workers.resume",{{"agentSessionId",recent}}))=="CAPACITY_EXCEEDED","Resuming a disconnected worker consumes concurrent capacity");
  call(broker,"a","workers.retire",{{"agentSessionId",fresh["result"]["agentSessionId"]}});
  require(call(broker,"a","workers.resume",{{"agentSessionId",recent}}).value("ok",false),"Retirement makes admission available for resume");
}
void worker_retirement_boundaries(const std::filesystem::path& directory) {
  FakeEngine engine;Broker broker(engine,directory,Broker::Limits{2});pair(broker,"host","Owner");auto a=worker(broker,"host","A");
  auto second=create_only(broker,"host",a.workspace);const std::string b=second["result"]["agentSessionId"];
  auto params=action_params(broker,a,"retire-active");Json active,queued;
  broker.dispatch("host",{{"method","page.click"},{"params",params}},[&](Json value){active=std::move(value);});engine.wait_pending(1);
  auto next=params;next["operationId"]="retire-queued";broker.dispatch("host",{{"method","page.click"},{"params",next}},[&](Json value){queued=std::move(value);});
  auto before=engine.call_count();auto retired=call(broker,"host","workers.retire",{{"agentSessionId",a.session}});
  require(retired["result"]["status"]=="retiring" && worker_row(broker,a.session)["state"]=="retiring","In-flight retirement stays visible until input balances");
  require(code(queued)=="SESSION_RETIRED" && queued["dispatchStatus"]=="not_dispatched","Retirement cancels queued input honestly");
  require(code(call(broker,"host","workers.resume",{{"agentSessionId",a.session}}))=="SESSION_RETIRED","Draining worker cannot resume");
  engine.event("tab.created",{{"tabId","late-retirement-popup"},{"workspaceId",a.workspace},{"openerTabId",a.tab["tabId"]}});
  auto third=create_only(broker,"host",a.workspace);require(third.value("ok",false),"Retiring worker no longer consumes connected admission");
  require(engine.call_count()==before+1,"Retirement and late popup handling issue no engine calls");
  engine.complete("retire-active");require(active.value("ok",false),"Already dispatched gesture retains actual completion");
  require(worker_row(broker,a.session).is_null(),"Retiring worker is erased after clean input completion");
  const auto drained=broker.state();for(const auto& tab:drained["tabs"])require(tab["ownerSessionId"]=="" && !tab["handoffPending"].get<bool>(),"Late popup drains with its original control group");
  require(call(broker,"host","operations.get",{{"operationId","retire-active"}})["result"]["state"]=="completed","Retirement preserves the operation journal outcome");
  call(broker,"host","workers.retire",{{"agentSessionId",third["result"]["agentSessionId"]}});
  auto d=worker(broker,"host","D",a.workspace);auto dp=action_params(broker,d,"outgoing-transfer");
  broker.dispatch("host",{{"method","page.click"},{"params",dp}},[](Json){});engine.wait_pending(1);
  auto handoff=d.base();handoff["expectedGeneration"]=d.tab["ownershipGeneration"];handoff["toSessionId"]=b;
  require(call(broker,"host","control.handoff",handoff)["result"]["handoffPending"],"Fixture transfer waits for active input");
  before=engine.call_count();call(broker,"host","workers.retire",{{"agentSessionId",d.session}});
  require(engine.call_count()==before,"Retiring outgoing owner does not operate the browser");engine.complete("outgoing-transfer");
  Worker target{"host",b,a.workspace,d.tab};require(call(broker,"host","control.status",target.base())["result"]["ownerSessionId"]==b,"Retiring outgoing owner preserves a valid accepted recipient");
  auto bp=action_params(broker,target,"incoming-transfer");broker.dispatch("host",{{"method","page.click"},{"params",bp}},[](Json){});engine.wait_pending(1);
  const auto incoming=create_only(broker,"host",a.workspace);const std::string incoming_id=incoming["result"]["agentSessionId"];
  handoff=target.base();handoff["expectedGeneration"]=bp["ownershipGeneration"];handoff["toSessionId"]=incoming_id;call(broker,"host","control.handoff",handoff);
  before=engine.call_count();call(broker,"host","workers.retire",{{"agentSessionId",incoming_id}});require(engine.call_count()==before,"Retiring incoming recipient is metadata-only");
  engine.event("tab.created",{{"tabId","late-incoming-popup"},{"workspaceId",a.workspace},{"openerTabId",d.tab["tabId"]}});engine.complete("incoming-transfer");
  require(call(broker,"host","control.status",target.base())["result"]["ownerSessionId"]=="","Retired incoming recipient cannot receive ownership");
  require(worker_row(broker,incoming_id).is_null(),"No dangling retired recipient remains");
  auto acquisition=target.base();auto status=call(broker,"host","control.status",target.base())["result"];acquisition["expectedGeneration"]=status["ownershipGeneration"];call(broker,"host","control.acquire",acquisition);
  auto guarded=action_params(broker,target,"retire-before-engine");engine.delay_guard=true;Json guard_result;
  broker.dispatch("host",{{"method","page.click"},{"params",guarded}},[&](Json value){guard_result=std::move(value);});engine.wait_guard("retire-before-engine");
  before=engine.call_count();call(broker,"host","workers.retire",{{"agentSessionId",b}});engine.dispatch_guard("retire-before-engine");
  require(code(guard_result)=="DISPATCH_CANCELLED" && guard_result["dispatchStatus"]=="not_dispatched" && engine.call_count()==before,"Retirement cancels already-queued UI dispatch without input");
  require(worker_row(broker,b).is_null(),"Cancelled engine reservation does not pin retired metadata");
}
void worker_late_callbacks(const std::filesystem::path& directory) {
  FakeEngine engine;Broker broker(engine,directory,Broker::Limits{2});auto principal=pair(broker,"old","Callback owner");auto a=worker(broker,"old","A");
  engine.delay_observe=true;Json read;
  broker.dispatch("old",{{"method","page.observe"},{"params",a.base()}},[&](Json value){read=std::move(value);});
  broker.disconnect("old");call(broker,"new","hello",principal);require(call(broker,"new","workers.resume",{{"agentSessionId",a.session}}).value("ok",false),"Disconnected observer can resume on a new attachment");
  engine.complete_observation();require(code(read)=="SESSION_CHANGED","Old observation cannot cross disconnect/resume attachment generations");engine.delay_observe=false;a.connection="new";
  auto before=engine.call_count();broker.disconnect("new");call(broker,"new","hello",principal);
  require(call(broker,"new","workers.retire",{{"agentSessionId",a.session}})["result"]["status"]=="retired","Same client may retire a disconnected session");require(engine.call_count()==before,"Disconnected retirement is browser-invisible");
  auto b=worker(broker,"new","B",a.workspace);engine.delay_metadata=true;Json metadata;
  broker.dispatch("new",{{"method","files.downloads"},{"params",b.base()}},[&](Json value){metadata=std::move(value);});
  require(call(broker,"new","workers.retire",{{"agentSessionId",b.session}})["result"]["status"]=="retiring","Metadata callback pins retiring identity until it is rejected");
  engine.held_metadata.at("files.downloads")(success({{"downloads",Json::array({{{"name","must-be-withheld"}}})}}));
  require(code(metadata)=="SESSION_RETIRED" && metadata.dump().find("must-be-withheld")==std::string::npos && worker_row(broker,b.session).is_null(),"Retirement withholds late metadata and then reclaims worker");engine.delay_metadata=false;
  engine.delay_create="workspace.ensure";Json created;
  broker.dispatch("new",{{"method","workers.create"},{"params",{{"name","pending"},{"workspaceId",a.workspace}}}},[&](Json value){created=std::move(value);});
  auto rows=call(broker,"new","workers.list")["result"]["workers"];require(rows.size()==1,"Pending worker is tracked");const std::string pending=rows[0]["agentSessionId"];
  call(broker,"new","workers.retire",{{"agentSessionId",pending}});engine.complete_create();
  require(code(created)=="SESSION_RETIRED" && worker_row(broker,pending).is_null(),"Late workspace initialization cannot resurrect retired worker");engine.delay_create.clear();
  auto c=worker(broker,"new","C",a.workspace);engine.delay_create="tabs.create";Json late_tab;
  broker.dispatch("new",{{"method","tabs.create"},{"params",{{"agentSessionId",c.session},{"workspaceId",c.workspace}}}},[&](Json value){late_tab=std::move(value);});
  const std::string late_id=engine.held_creates.front().params["tabId"];call(broker,"new","workers.retire",{{"agentSessionId",c.session}});
  engine.event("tab.created",{{"tabId",late_id},{"workspaceId",c.workspace}});engine.complete_create();engine.delay_create.clear();
  require(code(late_tab)=="SESSION_RETIRED" && worker_row(broker,c.session).is_null(),"Late tab reply is not delivered to a retired worker");
  const auto created_state=broker.state();bool retained=false;for(const auto& tab:created_state["tabs"])if(tab["tabId"]==late_id){retained=true;require(tab["ownerSessionId"]=="","Late-created page cannot resurrect retired ownership");}require(retained,"Retirement preserves an already accepted new tab");
  auto d=worker(broker,"new","D",a.workspace);engine.throw_command="page.observe";
  require(code(call(broker,"new","page.observe",d.base()))=="OUTCOME_UNKNOWN","Thrown read engine failure is reported");engine.throw_command.clear();
  require(call(broker,"new","workers.retire",{{"agentSessionId",d.session}})["result"]["status"]=="retired","Thrown read cannot strand callback references");
  auto e=worker(broker,"new","E",a.workspace);auto params=action_params(broker,e,"malformed-lifecycle-completion");Json mutation;
  broker.dispatch("new",{{"method","page.click"},{"params",params}},[&](Json value){mutation=std::move(value);});engine.wait_pending(1);
  require(call(broker,"new","workers.retire",{{"agentSessionId",e.session}})["result"]["status"]=="retiring","Malformed completion fixture first waits for accepted input");
  engine.complete("malformed-lifecycle-completion",{{"ok","bad"}});
  require(code(mutation)=="OUTCOME_UNKNOWN" && mutation["dispatchStatus"]=="dispatched","Malformed post-dispatch completion does not imply that no input happened");
  require(worker_row(broker,e.session).is_null(),"Malformed engine completion does not strand retiring identity");
  const auto completed_state=broker.state();for(const auto& tab:completed_state["tabs"])require(!tab["inputBusy"].get<bool>() && !tab["handoffPending"].get<bool>(),"Malformed completion drains the ownership/input barrier");
}
void security_and_handoff(const std::filesystem::path& directory) {
  FakeEngine engine; Broker broker(engine, directory);
  require(code(call(broker, "unpaired", "workers.list")) == "UNAUTHORIZED", "Unpaired connection cannot list workers");
  auto alice = pair(broker, "alice", "Alice host"); auto bob = pair(broker, "bob", "Bob host");
  auto first = worker(broker, "alice", "worker 1"), second = worker(broker, "bob", "worker 2");
  require(first.workspace != second.workspace, "Workers receive isolated persistent workspaces");
  auto forged = first.base(); forged["clientId"] = alice["clientId"];
  require(code(call(broker, "bob", "tabs.list", forged)) == "SESSION_DENIED", "Claimed client identity cannot forge a worker");
  auto shared = second.base(); shared["workspaceId"] = first.workspace; shared["tabId"] = first.tab["tabId"];
  require(code(call(broker, "bob", "page.observe", shared)) == "WORKSPACE_DENIED", "Workspace access defaults to deny");
  auto handoff = first.base(); handoff["toSessionId"] = second.session; handoff["expectedGeneration"] = first.tab["ownershipGeneration"];
  require(code(call(broker, "alice", "control.handoff", handoff)) == "HANDOFF_DENIED", "Handoff never grants workspace permission itself");
  require(broker.share_workspace(first.workspace, bob["clientId"]), "Human grants workspace access");
  auto parameters = action_params(broker, first, "first-input"); Json first_reply, queued_reply;
  auto account_scope = first.base();
  require(call(broker, "alice", "auth.accounts", account_scope)["result"]["accounts"].empty(), "Pairing does not grant vault accounts");
  auto login = parameters; login["operationId"] = "login-without-grant"; login["accountId"] = "allowed-account";
  require(code(call(broker, "alice", "auth.login", login)) == "ACCOUNT_DENIED", "Login requires a native account grant");
  require(broker.grant_account(alice["clientId"], first.workspace, "allowed-account", "https://example.test"), "Native account grant can bind an exact origin");
  auto accounts = call(broker, "alice", "auth.accounts", account_scope)["result"]["accounts"];
  require(accounts.size() == 1 && accounts[0]["accountId"] == "allowed-account" && !accounts[0].contains("username"), "Account listing exposes only granted metadata");
  require(!broker.grant_account(alice["clientId"], first.workspace, "allowed-account", "http://example.test"), "Credential grants require HTTPS origins");
  login["operationId"] = "granted-login"; login["grantedOrigin"] = "https://attacker.test"; Json login_result;
  broker.dispatch("alice", {{"method", "auth.login"}, {"params", login}}, [&](Json result) { login_result = std::move(result); });
  engine.wait_pending(1);
  require(engine.pending_params("granted-login")["grantedOrigin"] == "https://example.test", "Engine receives broker-validated origin, never a claimed origin");
  engine.complete("granted-login"); require(login_result.value("ok", false), "Granted login can reach protected engine path");
  parameters = action_params(broker, first, "first-input");
  broker.dispatch("alice", {{"method", "page.click"}, {"params", parameters}}, [&](Json result) { first_reply = std::move(result); });
  auto queued = parameters; queued["operationId"] = "queued-input";
  broker.dispatch("alice", {{"method", "page.click"}, {"params", queued}}, [&](Json result) { queued_reply = std::move(result); });
  engine.wait_pending(1);
  auto count = engine.call_count(); auto transfer = call(broker, "alice", "control.handoff", handoff);
  require(transfer["result"]["handoffPending"], "Transfer waits for finite input to balance");
  require(code(queued_reply) == "OWNERSHIP_CHANGED" && queued_reply["dispatchStatus"] == "not_dispatched", "Handoff cancels only undispatched commands");
  require(engine.call_count() == count, "Handoff issues no engine operation");
  engine.complete("first-input");
  auto state = call(broker, "bob", "control.status", shared)["result"];
  require(state["ownerSessionId"] == second.session && !state["handoffPending"], "Owner changes after active transaction finishes");
  require(state["workspaceId"] == first.workspace && state["tabId"] == first.tab["tabId"], "Transfer preserves source workspace and loaded tab");
  require(engine.call_count() == count, "Ownership commit does not focus, reload, or observe");
  auto new_action = shared; new_action["operationId"] = "needs-observation"; new_action["ownershipGeneration"] = state["ownershipGeneration"]; new_action["observationId"] = parameters["observationId"];
  require(code(call(broker, "bob", "page.click", new_action)) == "OBSERVATION_REQUIRED", "Recipient must observe separately after handoff");
  auto snapshot = call(broker, "bob", "page.observe", shared);
  new_action["observationId"] = snapshot["result"]["observationId"]; new_action["operationId"] = "second-input";
  Json second_reply; broker.dispatch("bob", {{"method", "page.click"}, {"params", new_action}}, [&](Json result) { second_reply = std::move(result); });
  engine.wait_pending(1); engine.complete("second-input"); require(second_reply.value("ok", false), "Recipient can use the same loaded tab");
  auto retry_count = engine.call_count(); auto retry = call(broker, "bob", "page.click", new_action);
  require(retry == second_reply && engine.call_count() == retry_count, "Operation retry returns journal entry without second dispatch");
  auto conflict = new_action; conflict["elementRef"] = "different";
  require(code(call(broker, "bob", "page.click", conflict)) == "OPERATION_CONFLICT", "Operation ID cannot be reused for different work");
  engine.event("auth.protected", {{"tabId", first.tab["tabId"]}, {"protected", true}});
  require(code(call(broker, "bob", "page.screenshot", shared)) == "SENSITIVE_AUTH_IN_PROGRESS", "Protected document prevents screenshots");
  require(code(call(broker, "bob", "page.observe", shared)) == "SENSITIVE_AUTH_IN_PROGRESS", "Protected document prevents detailed observations");
  engine.event("auth.protected", {{"tabId", first.tab["tabId"]}, {"protected", false}});
  auto nav = shared; nav["operationId"] = "bad-navigation"; nav["ownershipGeneration"] = state["ownershipGeneration"]; nav["url"] = "file:///C:/private";
  require(code(call(broker, "bob", "page.navigate", nav)) == "URL_DENIED", "File navigation is not delegated");
  count = engine.call_count(); broker.human_acquire(first.tab["tabId"]);
  require(engine.call_count() == count, "Human ownership acquisition changes only broker metadata");
  require(call(broker, "bob", "control.status", shared)["result"]["ownerSessionId"] == "human", "Human can take control");
  broker.human_release(first.tab["tabId"], second.session);
  require(engine.call_count() == count, "Human release changes only broker metadata");
  std::ifstream file(directory / "broker-state.json"); std::string saved((std::istreambuf_iterator<char>(file)), {});
  require(saved.find(alice["token"].get<std::string>()) == std::string::npos, "Pair token is not persisted in plaintext");
}
void observation_races(const std::filesystem::path& directory) {
  FakeEngine engine; Broker broker(engine, directory); pair(broker, "observer", "Observation host"); auto agent = worker(broker, "observer", "reader");
  auto params = action_params(broker, agent, "while-observing"); engine.delay_observe = true; Json read;
  broker.dispatch("observer", {{"method", "page.observe"}, {"params", agent.base()}}, [&](Json result) { read = std::move(result); });
  broker.dispatch("observer", {{"method", "page.click"}, {"params", params}}, [](Json) {});
  engine.wait_pending(1); engine.complete("while-observing"); engine.complete_observation();
  require(code(read) == "OBSERVATION_CHANGED", "Capture racing a dispatched action never returns stale evidence as current");
  broker.dispatch("observer", {{"method", "page.observe"}, {"params", agent.base()}}, [&](Json result) { read = std::move(result); });
  engine.event("auth.protected", {{"tabId", agent.tab["tabId"]}, {"protected", true}}); engine.complete_observation();
  require(code(read) == "SENSITIVE_AUTH_IN_PROGRESS", "In-flight observation is withheld when authentication begins");
  engine.event("auth.protected", {{"tabId", agent.tab["tabId"]}, {"protected", false}});
  broker.dispatch("observer", {{"method", "page.observe"}, {"params", agent.base()}}, [&](Json result) { read = std::move(result); });
  broker.human_acquire(agent.tab["tabId"]); engine.complete_observation();
  require(code(read) == "OWNERSHIP_CHANGED", "Capture racing human takeover requires a new observation");
}
void concurrent_workers_and_disconnect(const std::filesystem::path& directory) {
  FakeEngine engine; Broker broker(engine, directory); auto credentials = pair(broker, "host", "Shared multi-agent host");
  std::vector<Worker> workers;
  std::vector<Json> results(12);
  std::vector<Json> parameters;
  for (unsigned agent = 0; agent < 4; ++agent) {
    auto one = worker(broker, "host", "agent " + std::to_string(agent));
    for (unsigned tab = 0; tab < 3; ++tab) {
      if (tab) { auto result = call(broker, "host", "tabs.create", {{"agentSessionId", one.session}, {"workspaceId", one.workspace}}); require(result.value("ok", false), "Parallel worker can open another tab"); one.tab = result["result"]; }
      workers.push_back(one); parameters.push_back(action_params(broker, one, "parallel_" + std::to_string(agent * 3 + tab)));
    }
  }
  for (size_t i = 0; i < workers.size(); ++i) broker.dispatch("host", {{"method", "page.click"}, {"params", parameters[i]}}, [&, i](Json result) { results[i] = std::move(result); });
  engine.wait_pending(12);
  broker.disconnect("host");
  auto hello = call(broker, "reconnected", "hello", credentials); require(hello.value("ok", false), "Paired client reconnects with persisted token");
  require(code(call(broker, "reconnected", "page.observe", workers[0].base())) == "SESSION_DENIED", "Disconnected worker requires explicit resume");
  for (int i = 11; i >= 0; --i) engine.complete("parallel_" + std::to_string(i));
  for (size_t i = 0; i < results.size(); ++i) require(results[i].value("ok", false), "Already dispatched commands report their actual completion");
  auto journal = call(broker, "reconnected", "operations.get", {{"operationId", "parallel_0"}});
  require(journal["result"]["state"] == "completed", "Reconnect can inspect prior operation outcome");
  auto resumed = call(broker, "reconnected", "workers.resume", {{"agentSessionId", workers[0].session}}); require(resumed.value("ok", false), "Worker can resume after disconnect");
  auto status = call(broker, "reconnected", "control.status", workers[0].base());
  require(status["result"]["ownerSessionId"] == "", "Disconnected workers do not silently regain control");
  auto acquisition = workers[0].base(); acquisition["expectedGeneration"] = status["result"]["ownershipGeneration"];
  auto acquire = call(broker, "reconnected", "control.acquire", acquisition); require(acquire.value("ok", false), "Resumed worker explicitly reacquires tab");
}
void revoked_metadata(const std::filesystem::path& directory) {
  FakeEngine engine; Broker broker(engine, directory); auto principal = pair(broker, "metadata", "Metadata client"); auto agent = worker(broker, "metadata", "reader");
  require(broker.grant_account(principal["clientId"], agent.workspace, "allowed-account", "https://example.test"), "Metadata fixture grants an account");
  engine.delay_metadata = true; Json accounts, downloads;
  broker.dispatch("metadata", {{"method", "auth.accounts"}, {"params", agent.base()}}, [&](Json value) { accounts = std::move(value); });
  broker.dispatch("metadata", {{"method", "files.downloads"}, {"params", agent.base()}}, [&](Json value) { downloads = std::move(value); });
  require(broker.revoke_client(principal["clientId"]) == Broker::RevocationStatus::durable, "Native client revocation succeeds while reads are pending");
  engine.held_metadata.at("auth.accounts")(success({{"accounts", Json::array({{{"accountId", "allowed-account"}, {"origin", "https://example.test"}, {"label", "Private"}}})}}));
  engine.held_metadata.at("files.downloads")(success({{"downloads", Json::array({{{"name", "private-name.txt"}}})}}));
  require(code(accounts) == "ACCESS_REVOKED" && !accounts.contains("result"), "In-flight account metadata is withheld after revocation");
  require(code(downloads) == "ACCESS_REVOKED" && !downloads.contains("result"), "In-flight workspace metadata is withheld after revocation");
}
void popup_group_and_dialog(const std::filesystem::path& directory) {
  FakeEngine engine; Broker broker(engine, directory); pair(broker, "group", "Group owner"); auto other = pair(broker, "other", "Recipient");
  auto source = worker(broker, "group", "source"), recipient = worker(broker, "other", "recipient");
  require(broker.share_workspace(source.workspace, other["clientId"]), "Popup recipient is granted source workspace");
  engine.event("tab.created", {{"tabId", "popup-existing"}, {"workspaceId", source.workspace}, {"openerTabId", source.tab["tabId"]}});
  Worker popup = source; popup.tab = {{"tabId", "popup-existing"}};
  auto parent_params = action_params(broker, source, "group-parent"), popup_params = action_params(broker, popup, "group-popup");
  broker.dispatch("group", {{"method", "page.click"}, {"params", parent_params}}, [](Json) {});
  broker.dispatch("group", {{"method", "page.click"}, {"params", popup_params}}, [](Json) {}); engine.wait_pending(2);
  Json queued_result; popup_params["operationId"] = "queued-popup";
  broker.dispatch("group", {{"method", "page.click"}, {"params", popup_params}}, [&](Json value) { queued_result = std::move(value); });
  auto transfer = source.base(); transfer["toSessionId"] = recipient.session; transfer["expectedGeneration"] = source.tab["ownershipGeneration"];
  const auto count = engine.call_count(); require(call(broker, "group", "control.handoff", transfer)["result"]["handoffPending"], "Group transfer waits for all accepted gestures");
  require(code(queued_result) == "OWNERSHIP_CHANGED" && queued_result["dispatchStatus"] == "not_dispatched", "Group transfer cancels popup queue");
  engine.event("tab.created", {{"tabId", "popup-late"}, {"workspaceId", source.workspace}, {"openerTabId", "popup-existing"}});
  auto late = source.base(); late["tabId"] = "popup-late";
  require(call(broker, "group", "control.status", late)["result"]["handoffPending"], "Popup arriving during transfer inherits pending barrier");
  engine.complete("group-parent"); require(call(broker, "group", "control.status", source.base())["result"]["handoffPending"], "Parent completion cannot strand active popup under old owner");
  engine.complete("group-popup");
  for (auto scope : {source.base(), popup.base(), late}) {
    const auto state = call(broker, "group", "control.status", scope)["result"];
    require(state["ownerSessionId"] == recipient.session && !state["handoffPending"] && state["controlGroupId"] == source.tab["tabId"], "All live group members commit atomically");
  }
  require(engine.call_count() == count, "Popup group ownership commit performs no engine operation");
  broker.human_acquire("popup-late");
  require(call(broker, "group", "control.status", source.base())["result"]["ownerSessionId"] == "human", "Human takeover from a popup covers the group");
  broker.human_release("popup-existing", source.session);
  parent_params = action_params(broker, source, "dialog-opening-click");
  broker.dispatch("group", {{"method", "page.click"}, {"params", parent_params}}, [](Json) {}); engine.wait_pending(1);
  auto dialog = source.base(); dialog["ownershipGeneration"] = parent_params["ownershipGeneration"]; dialog["operationId"] = "dialog-answer"; dialog["accept"] = true;
  broker.dispatch("group", {{"method", "page.dialog"}, {"params", dialog}}, [](Json) {}); engine.wait_pending(2);
  transfer["expectedGeneration"] = parent_params["ownershipGeneration"];
  require(call(broker, "group", "control.handoff", transfer)["result"]["handoffPending"], "Dialog response is part of the group input barrier");
  dialog["operationId"] = "dialog-after-freeze";
  require(code(call(broker, "group", "page.dialog", dialog)) == "NOT_OWNER", "Dialog side lane never bypasses frozen ownership");
  engine.complete("dialog-opening-click"); require(call(broker, "group", "control.status", source.base())["result"]["handoffPending"], "Handoff waits for dialog side reservation too");
  engine.complete("dialog-answer"); require(!call(broker, "group", "control.status", source.base())["result"]["handoffPending"].get<bool>(), "Dialog and input completion allow handoff");
}
void queued_engine_guard(const std::filesystem::path& directory) {
  FakeEngine engine; Broker broker(engine, directory); pair(broker, "guard", "Guard owner"); auto source = worker(broker, "guard", "source");
  auto params = action_params(broker, source, "queued-ui-action"); engine.delay_guard = true; Json result;
  broker.dispatch("guard", {{"method", "page.click"}, {"params", params}}, [&](Json value) { result = std::move(value); }); engine.wait_guard("queued-ui-action");
  const auto count = engine.call_count(); engine.event("human.input", {{"tabId", source.tab["tabId"]}}); engine.event("human.idle", {{"tabId", source.tab["tabId"]}});
  engine.dispatch_guard("queued-ui-action");
  require(code(result) == "DISPATCH_CANCELLED" && result["dispatchStatus"] == "not_dispatched", "UI-thread permit cancels work interrupted after broker scheduling");
  require(engine.call_count() == count && engine.pending_count() == 0, "Denied engine permit sends no browser command");
  const auto status=call(broker, "guard", "control.status", source.base())["result"];
  require(status["ownerSessionId"] == source.session && status["ownershipGeneration"] == params["ownershipGeneration"] && !status["humanPaused"].get<bool>(), "Physical activity and idle preserve the worker's ownership and generation");
  require(status["requiresFreshObservation"].get<bool>(), "Cancelled queued engine operation still requires fresh evidence after idle");
}
void human_activity_pause(const std::filesystem::path& directory) {
  FakeEngine engine; Broker broker(engine,directory); const auto principal=pair(broker,"activity","Activity owner");
  const auto other_principal=pair(broker,"other-activity","Other client");
  auto a=worker(broker,"activity","Main"),independent=worker(broker,"activity","Independent"),other=worker(broker,"other-activity","Other");
  require(broker.share_workspace(a.workspace,other_principal["clientId"]),"Another client may observe the shared workspace without owning this tab");
  auto peer=worker(broker,"other-activity","Shared peer",a.workspace);
  const auto initial=call(broker,a.connection,"control.status",a.base())["result"];
  require(initial["ownerSessionId"]==a.session && initial["humanActivityEpoch"]==0 && !initial["humanPaused"].get<bool>(),"Creating a tab automatically grants its worker ownership without a human pause");
  require(code(call(broker,"unauthenticated","control.activity"))=="UNAUTHORIZED","Activity notices require client authentication");
  const auto activity_before=call(broker,a.connection,"control.activity")["result"]["tabs"];
  require(activity_before.size()==2,"Client activity covers only its connected workers' owned tabs");

  auto params=action_params(broker,a,"activity-running"); engine.delay_guard=true; Json result,queued;
  broker.dispatch(a.connection,{{"method","page.click"},{"params",params}},[&](Json value){result=std::move(value);});
  engine.wait_guard("activity-running"); auto permit=engine.guard_permit("activity-running"); engine.dispatch_guard("activity-running");
  auto next=params;next["operationId"]="activity-queued";
  broker.dispatch(a.connection,{{"method","page.click"},{"params",next}},[&](Json value){queued=std::move(value);});
  const auto calls_before=engine.call_count();
  engine.event("human.input",{{"tabId",a.tab["tabId"]},{"busy",true},{"pauseUntil",nullptr},{"text","must-not-enter-activity-notices"}});
  auto paused=call(broker,a.connection,"control.status",a.base())["result"];
  require(paused["ownerSessionId"]==a.session && paused["ownershipGeneration"]==initial["ownershipGeneration"] && !paused["handoffPending"].get<bool>(),"Physical page input does not transfer or freeze ownership");
  require(paused["humanActivityEpoch"]==1 && paused["humanPaused"].get<bool>() && paused["humanPauseUntil"].is_null() && paused["requiresFreshObservation"].get<bool>(),"Held input reports a pause without a false idle deadline and invalidates evidence");
  require(code(queued)=="HUMAN_ACTIVITY" && queued["dispatchStatus"]=="not_dispatched","Human activity cancels queued mutations without replay");
  require(!permit(),"An in-flight continuation is denied immediately after physical activity");
  require(engine.call_count()==calls_before,"Recording human activity sends no synthetic browser command");
  auto rejected=params;rejected["operationId"]="activity-rejected";
  require(code(call(broker,a.connection,"page.click",rejected))=="HUMAN_INPUT_PAUSED","Mutation admission distinguishes temporary physical pause from lost ownership");
  require(code(call(broker,a.connection,"page.observe",a.base()))=="HUMAN_INPUT_PAUSED" && code(call(broker,a.connection,"page.screenshot",a.base()))=="HUMAN_INPUT_PAUSED","Snapshots cannot become fresh evidence during held activity");
  auto independent_params=action_params(broker,independent,"unrelated-action");
  broker.dispatch(independent.connection,{{"method","page.click"},{"params",independent_params}},[](Json){});engine.wait_guard("unrelated-action");engine.dispatch_guard("unrelated-action");engine.complete("unrelated-action");
  require(!call(broker,independent.connection,"control.status",independent.base())["result"]["humanPaused"].get<bool>(),"Unrelated tabs remain usable during a physical pause");

  constexpr int64_t deadline=2000000000123;
  engine.event("human.input",{{"tabId",a.tab["tabId"]},{"busy",false},{"pauseUntil",deadline}});
  paused=call(broker,a.connection,"control.status",a.base())["result"];
  require(paused["humanActivityEpoch"]==2 && paused["humanPauseUntil"]==deadline && paused["humanPaused"].get<bool>(),"Release records the engine's advisory cooldown deadline without ending the pause");
  const auto notices=call(broker,a.connection,"control.activity");
  require(notices.dump().find("must-not-enter-activity-notices")==std::string::npos && notices.dump().find("https://")==std::string::npos,"Activity notices contain trusted status only, never input text or page URLs");
  const auto other_notices=call(broker,other.connection,"control.activity");
  for(const auto& row:other_notices["result"]["tabs"])
    require(row["tabId"]!=a.tab["tabId"],"Workspace sharing does not disclose another client's owned-tab activity feed");
  const auto before_idle=engine.call_count();engine.event("human.idle",{{"tabId",a.tab["tabId"]}});
  require(engine.call_count()==before_idle && !permit(),"Idle neither replays input nor revives an interrupted continuation");
  engine.complete("activity-running",failure("input_interrupted","Physical activity interrupted this action"));
  require(result["dispatchStatus"]=="dispatched" && call(broker,a.connection,"operations.get",{{"operationId","activity-running"}})["result"]["state"]=="outcome_unknown","Interrupted dispatched input retains an honest uncertain outcome");
  const auto after_idle=call(broker,a.connection,"control.status",a.base())["result"];
  require(after_idle["ownerSessionId"]==a.session && after_idle["ownershipGeneration"]==initial["ownershipGeneration"] && !after_idle["humanPaused"].get<bool>() && after_idle["humanPauseUntil"].is_null() && after_idle["humanActivityEpoch"]==2,"Activity status remains available after idle without changing ownership");
  auto navigate=a.base();navigate["operationId"]="after-human-navigation";navigate["ownershipGeneration"]=initial["ownershipGeneration"];navigate["url"]="https://example.test/fresh";
  require(code(call(broker,a.connection,"page.navigate",navigate))=="OBSERVATION_REQUIRED","Navigation also requires new evidence after physical activity");
  require(code(call(broker,a.connection,"page.click",rejected))=="OBSERVATION_REQUIRED","Old element evidence cannot resume input after idle");
  auto peer_read=peer.base();peer_read["tabId"]=a.tab["tabId"];
  require(call(broker,peer.connection,"page.observe",peer_read).value("ok",false),"Authorized peers can inspect an idle shared tab");
  require(call(broker,a.connection,"control.status",a.base())["result"]["requiresFreshObservation"].get<bool>(),"Another worker's observation does not satisfy the owner's fresh-evidence requirement");
  require(!call(broker,peer.connection,"control.status",peer_read)["result"]["requiresFreshObservation"].get<bool>(),"Fresh-evidence status is specific to the requesting worker");
  require(call(broker,a.connection,"page.observe",a.base()).value("ok",false),"The unchanged owner can observe again after the pause");
  require(!call(broker,a.connection,"control.status",a.base())["result"]["requiresFreshObservation"].get<bool>(),"A fresh observation permits the unchanged owner to continue");
  broker.dispatch(a.connection,{{"method","page.navigate"},{"params",navigate}},[](Json){});engine.wait_guard("after-human-navigation");engine.dispatch_guard("after-human-navigation");engine.complete("after-human-navigation");
  require(engine.command_count("page.navigate")==1,"Fresh evidence permits exactly the newly requested navigation");

  engine.delay_observe=true;Json read;
  broker.dispatch(a.connection,{{"method","page.observe"},{"params",a.base()}},[&](Json value){read=std::move(value);});
  engine.event("human.input",{{"tabId",a.tab["tabId"]},{"busy",true}});engine.complete_observation();
  require(code(read)=="HUMAN_INPUT_PAUSED","An observation in flight when physical input starts is withheld");
  engine.event("human.idle",{{"tabId",a.tab["tabId"]}});engine.delay_observe=false;
  broker.human_acquire(a.tab["tabId"]);
  const auto taken=call(broker,a.connection,"control.status",a.base())["result"];
  require(taken["ownerSessionId"]=="human" && taken["ownershipGeneration"].get<uint64_t>()>initial["ownershipGeneration"].get<uint64_t>(),"Explicit native takeover still changes ownership and generation");
  const auto notices_after_takeover=call(broker,a.connection,"control.activity");
  for(const auto& row:notices_after_takeover["result"]["tabs"])
    require(row["tabId"]!=a.tab["tabId"],"Explicitly human-owned tabs are removed from the worker activity feed");
  broker.human_release(a.tab["tabId"],a.session);
  require(broker.revoke_client(principal["clientId"])==Broker::RevocationStatus::durable,"Physical-pause behavior preserves durable client revocation");
  require(code(call(broker,a.connection,"control.activity"))=="UNAUTHORIZED","Revoked clients cannot read activity notices");
}
void continuation_engine_guard(const std::filesystem::path& directory) {
  FakeEngine engine; Broker broker(engine, directory); auto principal = pair(broker, "continue", "Continuation owner");
  auto source = worker(broker, "continue", "source");
  auto params = action_params(broker, source, "continued-action"); engine.delay_guard = true; Json result;
  broker.dispatch(source.connection, {{"method", "page.click"}, {"params", params}}, [&](Json value) { result = std::move(value); });
  engine.wait_guard("continued-action"); auto permit = engine.guard_permit("continued-action"); engine.dispatch_guard("continued-action");
  const auto operation = call(broker, source.connection, "operations.get", {{"operationId", "continued-action"}});
  const auto status = call(broker, source.connection, "control.status", source.base()); const auto count = engine.call_count();
  for (unsigned i = 0; i < 8; ++i) require(permit(), "An active operation can recheck its existing authority");
  require(engine.call_count() == count, "Continuation checks do not send extra browser commands");
  require(call(broker, source.connection, "operations.get", {{"operationId", "continued-action"}}) == operation &&
          call(broker, source.connection, "control.status", source.base()) == status,
          "Repeated permits preserve the existing dispatch and ownership metadata");
  engine.complete("continued-action"); require(result.value("ok", false) && result["dispatchStatus"] == "dispatched", "Repeated checks complete one dispatched operation");
  engine.delay_observe = true; Json observed;
  broker.dispatch(source.connection, {{"method", "page.observe"}, {"params", source.base()}}, [&](Json value) { observed = std::move(value); });
  require(observed.is_null(), "A fresh observation is pending after action completion");
  require(!permit() && !permit(), "A completed operation cannot continue using its retained permit");
  engine.complete_observation(); require(observed.value("ok", false), "Denied stale permits cannot invalidate a fresh observation");
  engine.delay_observe = false;

  params = action_params(broker, source, "handoff-continuation"); result = Json{};
  broker.dispatch(source.connection, {{"method", "page.click"}, {"params", params}}, [&](Json value) { result = std::move(value); });
  engine.wait_guard("handoff-continuation"); auto handoff_permit = engine.guard_permit("handoff-continuation"); engine.dispatch_guard("handoff-continuation");
  broker.human_acquire(source.tab["tabId"]);
  require(handoff_permit(), "Explicit takeover lets an already dispatched finite action drain under its unchanged authority");
  require(call(broker, source.connection, "control.status", source.base())["result"]["handoffPending"], "Explicit takeover retains the finite operation barrier until completion");
  engine.complete("handoff-continuation");
  require(result.value("ok",false) && result["dispatchStatus"] == "dispatched", "Explicit takeover retains the actual completion of already dispatched input");
  require(call(broker, source.connection, "control.status", source.base())["result"]["ownerSessionId"] == "human", "Engine completion releases the takeover barrier");
  require(!handoff_permit(), "The completed old operation cannot continue after takeover commits");
  broker.human_release(source.tab["tabId"], source.session);

  params = action_params(broker, source, "revoked-continuation"); result = Json{};
  broker.dispatch(source.connection, {{"method", "files.upload"}, {"params", params}}, [&](Json value) { result = std::move(value); });
  engine.wait_guard("revoked-continuation"); auto revoked_permit = engine.guard_permit("revoked-continuation"); engine.dispatch_guard("revoked-continuation");
  const auto before_revoke = engine.call_count();
  require(broker.revoke_client(principal["clientId"]) == Broker::RevocationStatus::durable, "Client revocation persists during an in-flight action");
  require(!revoked_permit() && !revoked_permit(), "Revoked clients cannot continue an already dispatched upload");
  require(engine.call_count() == before_revoke && result.is_null(), "Revocation checks issue no browser command and wait for engine cleanup");
  engine.complete("revoked-continuation", failure("input_interrupted", "Synthetic continuation stopped"));
  require(result["dispatchStatus"] == "dispatched", "Revoked continuation retains honest dispatch history after cleanup");
  const auto final_state = broker.state();
  require(final_state["tabs"].size() == 1 && final_state["tabs"][0]["ownerSessionId"] == "" && !final_state["tabs"][0]["handoffPending"].get<bool>(),
          "Revoked operation completion releases the group without granting a new owner");
}
void human_workspace_persistence(const std::filesystem::path& directory) {
  std::string private_id;
  { FakeEngine engine; Broker broker(engine, directory); Json value;
    broker.open_initial_human_workspace("about:blank", [&](Json result) { value = std::move(result); }); require(value.value("ok", false), "Initial human workspace opens");
    broker.open_initial_human_workspace("about:blank", [&](Json result) { value = std::move(result); }); require(broker.state()["workspaces"].size() == 1, "Initial human workspace uses stable profile identity");
    broker.open_human_workspace("about:blank", [&](Json result) { value = std::move(result); }, true); require(value["result"]["private"], "Native private flag reaches engine"); private_id = value["result"]["workspaceId"];
    auto principal = pair(broker, "private", "Agent"); require(!broker.share_workspace(private_id, principal["clientId"]), "Private human workspace cannot gain persistent agent grants");
  }
  { FakeEngine engine; Broker broker(engine, directory); auto state = broker.state(); require(state["workspaces"].size() == 1 && state["workspaces"][0]["workspaceId"] == "native-default", "Private workspaces are excluded from persistent broker metadata"); }
}
Json workspace_row(Broker& broker,const std::string& id) {
  const auto state=broker.state();for(const auto& row:state["workspaces"])if(row["workspaceId"]==id)return row;
  return Json{};
}
Json await_result(std::future<Json>& result) {
  require(result.wait_for(std::chrono::seconds(5))==std::future_status::ready,"Asynchronous workspace removal must complete");
  return result.get();
}
void workspace_removal_boundary(const std::filesystem::path& directory) {
  FakeEngine engine; Broker broker(engine,directory);
  const auto first=pair(broker,"remove-a","First"),second=pair(broker,"remove-b","Second");
  auto a=worker(broker,"remove-a","Removing"),b=worker(broker,"remove-b","Keep");
  require(broker.share_workspace(a.workspace,second["clientId"]),"Second principal has a workspace grant before removal");
  require(broker.grant_account(first["clientId"],a.workspace,"account-a","https://example.test"),"Removed workspace has an account grant");
  require(broker.grant_account(second["clientId"],b.workspace,"account-b","https://example.test"),"Other workspace has a retained account grant");
  auto old=action_params(broker,a,"remove-closed-dialog");old["action"]="inspect";Json old_result;
  broker.dispatch(a.connection,{{"method","page.dialog"},{"params",old}},[&](Json value){old_result=std::move(value);});engine.wait_pending(1);
  engine.complete("remove-closed-dialog",success({{"dialog",{{"message","closed-tab-dialog-canary"}}}}));
  require(old_result.dump().find("closed-tab-dialog-canary")!=std::string::npos,"Fixture retains a completed website result before removal");
  engine.event("tab.closed",{{"tabId",a.tab["tabId"]}});
  a.tab=call(broker,a.connection,"tabs.create",{{"agentSessionId",a.session},{"workspaceId",a.workspace},{"url","https://example.test/"}})["result"];
  engine.event("tab.navigated",{{"tabId",b.tab["tabId"]},{"documentId","retained-document"}});
  const auto other_before=call(broker,b.connection,"control.status",b.base())["result"];
  require(code(call(broker,a.connection,"workspaces.remove",{{"workspaceId",a.workspace}}))=="HUMAN_REQUIRED","MCP cannot remove workspaces");
  Json denied; broker.remove_workspace("native-default",[&](Json value){denied=std::move(value);});
  require(code(denied)=="WORKSPACE_PROTECTED","Personal workspace cannot be removed");
  broker.remove_workspace("missing",[&](Json value){denied=std::move(value);});require(code(denied)=="WORKSPACE_UNKNOWN","Missing workspace is reported honestly");
  auto params=action_params(broker,a,"remove-active");Json active,queued;
  broker.dispatch(a.connection,{{"method","page.drag"},{"params",params}},[&](Json value){active=std::move(value);});engine.wait_pending(1);
  auto next=params;next["operationId"]="remove-queued";
  broker.dispatch(a.connection,{{"method","page.click"},{"params",next}},[&](Json value){queued=std::move(value);});
  auto handoff=a.base();handoff["expectedGeneration"]=a.tab["ownershipGeneration"];handoff["toSessionId"]=b.session;
  require(call(broker,a.connection,"control.handoff",handoff)["result"]["handoffPending"],"Outgoing transfer waits for the accepted gesture");
  auto removed=std::make_shared<std::promise<Json>>();auto final=removed->get_future();
  const auto engine_before=engine.call_count();
  broker.remove_workspace(a.workspace,[removed](Json value){removed->set_value(std::move(value));});
  require(final.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout,"Removal waits for accepted finite input");
  require(engine.call_count()==engine_before && engine.removal_count()==0,"Removal does not close or inject input while a gesture is active");
  require(queued["dispatchStatus"]=="not_dispatched","Queued mutation is never replayed during removal");
  require(workspace_row(broker,a.workspace)["removalStatus"]=="draining","Native UI can display the input drain");
  require(!broker.share_workspace(a.workspace,first["clientId"]) && !broker.grant_account(first["clientId"],a.workspace,"new","https://example.test"),"Removed workspace cannot be re-granted");
  require(code(create_only(broker,b.connection,a.workspace))=="WORKSPACE_DENIED","Another live worker cannot reopen a removed workspace");
  require(call(broker,a.connection,"workspaces.list")["result"]["workspaces"].empty(),"Removed workspace immediately disappears from MCP grants");
  const auto old_status=call(broker,a.connection,"operations.get",{{"operationId","remove-closed-dialog"}});
  require(old_status["result"]["state"]=="completed" && old_status.dump().find("closed-tab-dialog-canary")==std::string::npos,"Removal scrubs retained content from already-closed tabs without losing outcome metadata");
  Json saved;std::ifstream(directory/"broker-state.json")>>saved;
  require(saved["removedWorkspaces"]==Json::array({a.workspace}),"Deletion marker is durable before browser closure");
  require(saved["workspaces"].size()==1 && saved["accountGrants"].size()==1,"Same durable record revokes workspace and account grants");
  engine.event("tab.created",{{"tabId","late-removal-popup"},{"workspaceId",a.workspace},{"openerTabId",a.tab["tabId"]}});
  engine.complete("remove-active",success({{"observation",{{"observationId","late-sensitive-snapshot"},{"text","removed-page-canary"}}}}));engine.wait_removal();
  require(active.value("ok",false) && active["dispatchStatus"]=="dispatched","Accepted gesture retains factual completion");
  require(active.dump().find("removed-page-canary")==std::string::npos && active["result"]["resultWithheld"].get<bool>(),"Late mutation retains outcome but never discloses removed workspace page data");
  require(workspace_row(broker,a.workspace)["removalStatus"]=="closing","Closure starts only after the finite gesture callback");
  engine.complete_removal(a.workspace,failure("close_failed","Synthetic close failure"));
  require(code(await_result(final))=="WORKSPACE_CLOSE_FAILED","Engine failure never reports a completed removal");
  require(workspace_row(broker,a.workspace)["removalStatus"]=="retry","Failed closure remains selectable for retry");
  auto retried=std::make_shared<std::promise<Json>>();auto retry=retried->get_future();broker.remove_workspace(a.workspace,[retried](Json value){retried->set_value(std::move(value));});
  engine.wait_removal();engine.complete_removal(a.workspace);
  const auto done=await_result(retry);require(done["result"]["status"]=="removed" && done["result"]["profileCleanup"]=="next_start","Completion distinguishes tab removal from next-start profile cleanup");
  require(workspace_row(broker,a.workspace).is_null() && broker.state()["tabs"].size()==1,"Late popup and original tabs disappear only on engine confirmation");
  require(call(broker,b.connection,"control.status",b.base())["result"]==other_before,"Removing one workspace preserves another live tab and its ownership");
  require(broker.state()["clients"].size()==2 && broker.state()["accountGrants"].size()==1,"Pairings and unrelated credential grants survive");
  require(call(broker,a.connection,"operations.get",{{"operationId","remove-active"}})["result"]["state"]=="completed","Operation outcome survives workspace removal");
  broker.remove_workspace(a.workspace,[&](Json value){denied=std::move(value);});require(denied["result"]["status"]=="removed","Repeated completed removal is idempotent");
}
void workspace_removal_pending_creates(const std::filesystem::path& directory) {
  for(const auto& method:{std::string("workspace.ensure"),std::string("tabs.create")}) {
    FakeEngine engine;Broker broker(engine,directory/method);pair(broker,"create","Pending creation");
    auto existing=create_only(broker,"create");const std::string workspace=existing["result"]["workspaceId"],session=existing["result"]["agentSessionId"];
    engine.delay_create=method;Json created;
    const auto api=method=="workspace.ensure"?"workers.create":"tabs.create";
    Json args{{"workspaceId",workspace},{"agentSessionId",session},{"name","Pending"}};
    broker.dispatch("create",{{"method",api},{"params",args}},[&](Json value){created=std::move(value);});
    require(created.is_null(),"Fixture creation callback is pending");
    auto result=std::make_shared<std::promise<Json>>();auto completed=result->get_future();broker.remove_workspace(workspace,[result](Json value){result->set_value(std::move(value));});
    require(completed.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout && engine.removal_count()==0,"Removal waits for pending context/tab creation");
    engine.complete_create();require(!created.value("ok",false),"Late creation cannot return a usable removed workspace or tab");
    engine.wait_removal();engine.complete_removal(workspace);require(await_result(completed).value("ok",false),"Pending creation drains before closure");
    require(broker.state()["tabs"].empty() && broker.state()["workers"].empty(),"Late create does not resurrect tab or worker metadata");
  }
  FakeEngine engine;Broker broker(engine,directory/"native");engine.delay_create="workspace.ensure";Json opened;
  broker.open_human_workspace("about:blank",[&](Json value){opened=std::move(value);},true);
  const auto workspace=broker.state()["workspaces"][0]["workspaceId"].get<std::string>();
  auto removed=std::make_shared<std::promise<Json>>();auto final=removed->get_future();broker.remove_workspace(workspace,[removed](Json value){removed->set_value(std::move(value));});
  engine.complete_create();require(code(opened)=="WORKSPACE_REMOVED" && engine.command_count("tabs.create")==0,"Late native context initialization never starts a removed tab");
  engine.wait_removal();engine.complete_removal(workspace,success({{"profileCleanup","memory_only"},{"filePermissionsCleanupPending",true}}));
  const auto private_result=await_result(final);require(private_result.value("ok",false),"Private workspace removal is supported");
  require(private_result["result"]["profileCleanup"]=="memory_only" && private_result["result"]["filePermissionsCleanupPending"].get<bool>(),"Removal preserves native cleanup metadata without claiming disk deletion");
}
void workspace_removal_reads_and_human_input(const std::filesystem::path& directory) {
  FakeEngine engine;Broker broker(engine,directory);pair(broker,"read","Read owner");auto a=worker(broker,"read","Reader");
  engine.delay_observe=true;Json read;broker.dispatch(a.connection,{{"method","page.observe"},{"params",a.base()}},[&](Json value){read=std::move(value);});
  engine.event("human.input",{{"tabId",a.tab["tabId"]}});
  auto removed=std::make_shared<std::promise<Json>>();auto final=removed->get_future();broker.remove_workspace(a.workspace,[removed](Json value){removed->set_value(std::move(value));});
  require(final.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout && engine.removal_count()==0,"Physical held input blocks closure");
  engine.event("human.idle",{{"tabId",a.tab["tabId"]}});engine.wait_removal();
  require(read.is_null(),"Slow read need not complete before closing a workspace");
  engine.complete_removal(a.workspace);require(await_result(final).value("ok",false),"Human idle releases the removal barrier");
  engine.complete_observation();require(code(read)=="ACCESS_REVOKED","Read completion after removal never discloses stale content");
  require(broker.state()["workers"].empty(),"Late read releases retired callback bookkeeping");
}
void workspace_removal_engine_queue(const std::filesystem::path& directory) {
  FakeEngine engine;Broker broker(engine,directory);pair(broker,"guard","Guard owner");auto a=worker(broker,"guard","Queued");
  auto params=action_params(broker,a,"remove-before-dispatch");engine.delay_guard=true;Json result,queued;
  broker.dispatch(a.connection,{{"method","page.click"},{"params",params}},[&](Json value){result=std::move(value);});engine.wait_guard("remove-before-dispatch");
  auto next=params;next["operationId"]="remove-broker-queue";broker.dispatch(a.connection,{{"method","page.click"},{"params",next}},[&](Json value){queued=std::move(value);});
  auto removed=std::make_shared<std::promise<Json>>();auto final=removed->get_future();broker.remove_workspace(a.workspace,[removed](Json value){removed->set_value(std::move(value));});
  require(code(queued)=="SESSION_RETIRED" && queued["dispatchStatus"]=="not_dispatched","Removal cancels broker-queued mutations before engine entry");
  engine.dispatch_guard("remove-before-dispatch");require(code(result)=="DISPATCH_CANCELLED" && result["dispatchStatus"]=="not_dispatched","Removed workspace cannot pass a queued engine permit");
  engine.wait_removal();engine.complete_removal(a.workspace);require(await_result(final).value("ok",false),"Cancelled pre-dispatch reservation drains correctly");
  require(engine.command_count("page.click")==0,"No page mutation is injected after removal authorization");
}
void uncertain_outcomes(const std::filesystem::path& directory) {
  FakeEngine engine; Broker broker(engine, directory); pair(broker, "uncertain", "Outcome client"); auto source = worker(broker, "uncertain", "source");
  for (const std::string error : {"OUTCOME_UNKNOWN", "input_uncertain", "input_interrupted", "stale_element"}) {
    const auto id = "outcome_" + error; auto params = action_params(broker, source, id); Json result;
    broker.dispatch("uncertain", {{"method", "page.click"}, {"params", params}}, [&](Json value) { result = std::move(value); });
    engine.wait_pending(1); engine.complete(id, failure(error, "Synthetic engine result"));
    auto operation = call(broker, "uncertain", "operations.get", {{"operationId", id}})["result"];
    require(operation["state"] == (error == "stale_element" ? "failed" : "outcome_unknown"), "Only explicitly uncertain dispatched errors map to outcome_unknown");
    require(code(result) == error && result["dispatchStatus"] == "dispatched", "Uncertain outcomes preserve the engine error and factual dispatch status");
  }
}
void persistence_and_deny(const std::filesystem::path& directory) {
  Json credentials;
  { FakeEngine engine; Broker broker(engine, directory); credentials = pair(broker, "first", "Persistent host"); auto session = worker(broker, "first", "persistent worker");
    auto parameters = action_params(broker, session, "durable-completed"); Json reply;
    broker.dispatch("first", {{"method", "page.click"}, {"params", parameters}}, [&](Json value) { reply = std::move(value); });
    engine.wait_pending(1); engine.complete("durable-completed"); require(reply.value("ok", false), "Completed fixture reports success");
    parameters = action_params(broker, session, "durable-unresolved");
    broker.dispatch("first", {{"method", "page.click"}, {"params", parameters}}, [](Json) {});
    engine.wait_pending(1);
    // Engine is intentionally destroyed without a result, modelling a process
    // ending after dispatch. Only metadata survives; there is never a replay.
  }
  { FakeEngine engine; Broker broker(engine, directory); require(call(broker, "next", "hello", credentials).value("ok", false), "Pairing survives browser restart");
    auto list = call(broker, "next", "workspaces.list"); require(!list["result"]["workspaces"].empty(), "Workspace grants survive restart");
    Json denied; broker.dispatch("denied", {{"method", "pair.request"}, {"params", {{"name", "Declined host"}}}}, [&](Json result) { denied = std::move(result); });
    broker.deny_pairing(broker.state()["pairings"][0]["requestId"]); require(code(denied) == "PAIRING_DENIED", "Native denial completes pending request");
    auto wrong = credentials; wrong["token"] = std::string(64, '0'); require(code(call(broker, "forged", "hello", wrong)) == "UNAUTHORIZED", "Wrong token cannot authenticate");
    auto completed = call(broker, "next", "operations.get", {{"operationId", "durable-completed"}});
    require(completed["result"]["state"] == "completed" && code(completed["result"]["response"]) == "RESULT_NOT_RETAINED", "Completed recovery keeps status without page content");
    auto unresolved = call(broker, "next", "operations.get", {{"operationId", "durable-unresolved"}});
    require(unresolved["result"]["state"] == "outcome_unknown" && code(unresolved["result"]["response"]) == "OUTCOME_UNKNOWN", "Unresolved recovery does not pretend an action was cancelled");
    require(engine.call_count() == 0, "Recovering operation metadata never replays browser actions");
    require(broker.revoke_client(credentials["clientId"]) == Broker::RevocationStatus::durable, "Native client revocation succeeds");
    require(code(call(broker, "next", "workspaces.list")) == "UNAUTHORIZED", "Revocation disconnects live principal");
  }
}
#ifdef _WIN32
void workspace_removal_persistence(const std::filesystem::path& directory) {
  Json principal;std::string workspace;
  {
    FakeEngine engine;Broker broker(engine,directory);principal=pair(broker,"persist-remove","Persistent owner");auto a=worker(broker,"persist-remove","Remove later");workspace=a.workspace;
    require(broker.grant_account(principal["clientId"],workspace,"account","https://example.test"),"Removal fixture has a saved account grant");
    const auto before=engine.call_count();Json rejected;
    {
      struct LockedState { HANDLE handle;~LockedState(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);} } locked{
        CreateFileW((directory/"broker-state.json").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
      require(locked.handle!=INVALID_HANDLE_VALUE,"Recovery record is locked against replacement");
      broker.remove_workspace(workspace,[&](Json value){rejected=std::move(value);});
      require(code(rejected)=="PERSISTENCE_FAILED","Undurable deletion is refused");
      require(broker.removed_workspaces().empty() && !workspace_row(broker,workspace)["removing"].get<bool>(),"Failed marker save leaves live workspace intact");
      require(broker.state()["accountGrants"].size()==1 && worker_row(broker,a.session)["connected"].get<bool>(),"Failed marker save does not revoke credentials or retire workers");
      require(engine.call_count()==before && engine.removal_count()==0,"Failed marker save causes zero engine operations");
    }
    engine.event("human.input",{{"tabId",a.tab["tabId"]}});
    broker.remove_workspace(workspace,[&](Json value){rejected=std::move(value);});
    require(broker.removed_workspaces()==std::vector<std::string>{workspace},"Retry records durable deletion intent");
    // Exit while the human gesture still holds the barrier. Startup must
    // recover deletion without re-granting or recreating this workspace.
    broker.stop_all();require(code(rejected)=="REMOVAL_PENDING_RESTART","Interrupted removal reports saved cleanup for next start");
  }
  {
    FakeEngine engine;Broker broker(engine,directory);require(call(broker,"restart","hello",principal).value("ok",false),"Unrelated paired identity survives removal");
    require(broker.removed_workspaces()==std::vector<std::string>{workspace},"Deletion tombstone survives interrupted restart");
    require(broker.state()["workspaces"].empty() && broker.state()["accountGrants"].empty(),"Restart never recreates removed workspace grants");
    require(code(create_only(broker,"restart",workspace))=="WORKSPACE_DENIED" && !broker.share_workspace(workspace,principal["clientId"]),"Saved removed ID cannot be reopened or granted");
    require(engine.call_count()==0,"Loading a deletion marker does not recreate any browser state");
  }
}
void revocation_persistence_failure(const std::filesystem::path& directory) {
  Json principal;
  {
    FakeEngine engine; Broker broker(engine, directory);
    principal = pair(broker, "revocation", "Retryable revocation client");
    auto source = worker(broker, "revocation", "revocation worker");
    require(broker.grant_account(principal["clientId"], source.workspace, "allowed-account", "https://example.test"), "Revocation failure fixture has a credential grant");
    auto params = action_params(broker, source, "revoke-ui-queued");
    engine.delay_guard = true; Json scheduled, queued;
    broker.dispatch("revocation", {{"method", "page.click"}, {"params", params}}, [&](Json value) { scheduled = std::move(value); });
    engine.wait_guard("revoke-ui-queued");
    params["operationId"] = "revoke-broker-queued";
    broker.dispatch("revocation", {{"method", "page.click"}, {"params", params}}, [&](Json value) { queued = std::move(value); });
    {
      // Deny atomic replacement of the real saved file while leaving reads
      // possible. This exercises Windows persistence failure without a hook.
      struct LockedState {
        HANDLE handle;
        ~LockedState() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
      } locked{CreateFileW((directory / "broker-state.json").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
      require(locked.handle != INVALID_HANDLE_VALUE, "Saved grants can be locked against replacement");
      require(broker.revoke_client(principal["clientId"]) == Broker::RevocationStatus::pending, "Failed persistence must never report durable revocation");
      require(code(call(broker, "revocation", "workers.list")) == "UNAUTHORIZED", "Unsaved revocation immediately disconnects the principal");
      require(code(call(broker, "new-revocation-connection", "hello", principal)) == "UNAUTHORIZED", "Unsaved revocation denies fresh authentication in the running process");
      require(code(queued) == "CLIENT_DISCONNECTED" && queued["dispatchStatus"] == "not_dispatched", "Unsaved revocation still cancels broker-queued input");
      engine.dispatch_guard("revoke-ui-queued");
      require(code(scheduled) == "DISPATCH_CANCELLED" && scheduled["dispatchStatus"] == "not_dispatched", "Unsaved revocation denies queued engine dispatch");
      const auto state = broker.state();
      require(state["clients"].empty() && state["accountGrants"].empty(), "Failed save does not restore live client or credential grants");
      require(state["pendingRevocations"].size() == 1 && state["pendingRevocations"][0]["clientId"] == principal["clientId"], "Native state retains the unsaved revocation for retry");
      Json saved; std::ifstream(directory / "broker-state.json") >> saved;
      require(saved["clients"].size() == 1, "Regression genuinely leaves the former durable authorization intact");
      require(broker.revoke_client(principal["clientId"]) == Broker::RevocationStatus::pending, "Retry remains honestly pending while storage is unavailable");
    }
    require(broker.revoke_client(principal["clientId"]) == Broker::RevocationStatus::durable, "Retry durably saves denial after file replacement becomes available");
    require(broker.state()["pendingRevocations"].empty(), "Successful retry clears the pending native entry");
    require(broker.revoke_client(principal["clientId"]) == Broker::RevocationStatus::missing, "Already durably revoked principal is distinguished from an unsaved revocation");
  }
  {
    FakeEngine engine; Broker restarted(engine, directory);
    require(code(call(restarted, "after-retry", "hello", principal)) == "UNAUTHORIZED", "Successful revocation retry remains denied across broker restart");
    require(restarted.state()["accountGrants"].empty(), "Credential grants remain removed after revocation retry and restart");
  }
}
class PipeClient {
 public:
  explicit PipeClient(const std::wstring& name) {
    for (unsigned attempt = 0; attempt < 500; ++attempt) {
      pipe_ = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
      if (pipe_ != INVALID_HANDLE_VALUE) return;
      if (GetLastError() != ERROR_PIPE_BUSY && GetLastError() != ERROR_FILE_NOT_FOUND) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("Cannot connect pipe test client");
  }
  ~PipeClient() { if (pipe_ != INVALID_HANDLE_VALUE) { CancelIoEx(pipe_, nullptr); CloseHandle(pipe_); } }
  void send(const std::string& payload) {
    size_t offset{};
    while (offset < payload.size()) { DWORD count{}; require(io(true, const_cast<char*>(payload.data()) + offset, static_cast<DWORD>(payload.size() - offset), count) && count, "Pipe write must succeed"); offset += count; }
  }
  Json receive() {
    while (buffer_.find('\n') == std::string::npos) { char bytes[8192]; DWORD count{}; require(io(false, bytes, sizeof(bytes), count) && count, "Pipe response must arrive within timeout"); buffer_.append(bytes, count); }
    auto end = buffer_.find('\n'); auto line = buffer_.substr(0, end); buffer_.erase(0, end + 1); return Json::parse(line);
  }
  Json request(int id, const std::string& method, Json params = Json::object()) { send(Json{{"id", id}, {"method", method}, {"params", params}}.dump() + "\n"); return receive(); }
 private:
  HANDLE pipe_{INVALID_HANDLE_VALUE}; std::string buffer_;
  bool io(bool write, char* buffer, DWORD length, DWORD& count) {
    HANDLE signal = CreateEventW(nullptr, TRUE, FALSE, nullptr); OVERLAPPED overlap{}; overlap.hEvent = signal;
    bool success = (write ? WriteFile(pipe_, buffer, length, &count, &overlap) : ReadFile(pipe_, buffer, length, &count, &overlap)) != FALSE;
    if (!success && GetLastError() == ERROR_IO_PENDING) {
      if (WaitForSingleObject(signal, 5000) == WAIT_OBJECT_0) success = GetOverlappedResult(pipe_, &overlap, &count, FALSE) != FALSE;
      else { CancelIoEx(pipe_, &overlap); GetOverlappedResult(pipe_, &overlap, &count, TRUE); }
    }
    CloseHandle(signal); return success;
  }
};
void pipe_transport(const std::filesystem::path& directory) {
  FakeEngine engine; Broker broker(engine, directory);
  auto suffix = local_security::random_hex(8); auto name = L"xenon-test-" + std::wstring(suffix.begin(), suffix.end());
  PipeServer server(broker, name); server.start();
  bool duplicate_rejected{};
  try { PipeServer duplicate(broker, name); duplicate.start(); } catch (const std::exception&) { duplicate_rejected = true; }
  require(duplicate_rejected, "Another server cannot silently bind the existing pipe name");
  PipeClient client(server.name());
  auto denied = client.request(1, "workers.list"); require(denied["id"] == 1 && code(denied) == "UNAUTHORIZED", "Pipe preserves correlation and requires authentication");
  client.send("{not-json}\n"); auto malformed = client.receive(); require(malformed["id"].is_null() && code(malformed) == "INVALID_REQUEST", "Malformed frames fail without disconnecting other clients");
  client.send(Json{{"id", 2}, {"method", "pair.request"}, {"params", {{"name", "Pipe test host"}}}}.dump() + "\n");
  for (unsigned i = 0; i < 500 && broker.state()["pairings"].empty(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  auto state = broker.state(); require(state["pairings"].size() == 1, "Pipe pairing is pending in native UI");
  require(broker.approve_pairing(state["pairings"][0]["requestId"]), "Native UI approves pipe client");
  auto paired = client.receive(); require(paired["id"] == 2 && paired.value("ok", false), "Pair response returns asynchronously with original request id");
  auto created = client.request(3, "workers.create", {{"name", "Pipe worker"}}); require(created.value("ok", false), "Authenticated pipe request reaches broker");
  PipeClient other(server.name());
  require(code(other.request(4, "workers.list")) == "UNAUTHORIZED", "Authentication does not leak between pipe instances");
  std::vector<std::unique_ptr<PipeClient>> busy;
  for (int i = 0; i < 30; ++i) { auto item = std::make_unique<PipeClient>(server.name()); require(code(item->request(10 + i, "workers.list")) == "UNAUTHORIZED", "Every full-capacity connection remains isolated"); busy.push_back(std::move(item)); }
  busy.pop_back();
  PipeClient after_capacity(server.name());
  require(code(after_capacity.request(50, "workers.list")) == "UNAUTHORIZED", "Listener recovers after the connection budget is exhausted and a slot is freed");
  auto started = std::chrono::steady_clock::now(); server.stop();
  require(std::chrono::steady_clock::now() - started < std::chrono::seconds(3), "Server stops idle blocked reads without deadlocking");
}
#endif
}
int main() {
  try {
    human_dialog_notices();
    const auto root = std::filesystem::absolute(std::filesystem::path("build") / "broker-test-data" / local_security::random_hex(8));
    worker_capacity_and_churn(root / "worker-churn");
    worker_retirement_boundaries(root / "worker-retirement");
    worker_late_callbacks(root / "worker-callbacks");
    security_and_handoff(root / "handoff");
    concurrent_workers_and_disconnect(root / "concurrency");
    persistence_and_deny(root / "persistence");
    observation_races(root / "observation-races");
    revoked_metadata(root / "revoked-metadata");
    popup_group_and_dialog(root / "popup-group");
    queued_engine_guard(root / "engine-guard");
    human_activity_pause(root / "human-activity");
    continuation_engine_guard(root / "continuation-guard");
    human_workspace_persistence(root / "human-workspaces");
    workspace_removal_boundary(root / "workspace-remove-boundary");
    workspace_removal_pending_creates(root / "workspace-remove-creates");
    workspace_removal_reads_and_human_input(root / "workspace-remove-reads");
    workspace_removal_engine_queue(root / "workspace-remove-queue");
    uncertain_outcomes(root / "uncertain-outcomes");
#ifdef _WIN32
    workspace_removal_persistence(root / "workspace-remove-persistence");
    revocation_persistence_failure(root / "revocation-persistence-failure");
    pipe_transport(root / "pipe");
#endif
    // Only this run's generated fixture directory is removed.
    std::filesystem::remove_all(root);
    std::cout << "Broker tests passed: authorization, recovery journal, vault grants, 4x3 concurrency, handoff, deduplication, disconnect, protected observations, and Windows pipe transport\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << "Broker test failure: " << error.what() << '\n'; return 1; }
}
