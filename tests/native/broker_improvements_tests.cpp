#include "xenon/broker.hpp"
#include "xenon/local_security.hpp"
#include "xenon/pipe_server.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace xenon;

namespace {

void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

std::string code(const Json& value) {
  return value.contains("error") && value["error"].is_object()
             ? value["error"].value("code", std::string{})
             : std::string{};
}

struct TempDir {
  std::filesystem::path path;
  explicit TempDir(const std::string& prefix)
      : path(std::filesystem::temp_directory_path() / (prefix + local_security::random_hex(8))) {
    std::filesystem::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
};

struct FakeEngine final : BrowserEngine {
  struct Pending {
    std::string command;
    Json params;
    Reply reply;
  };
  struct Guarded {
    std::string command;
    Json params;
    std::function<bool()> permit;
    Reply reply;
  };

  EventSink sink;
  std::vector<std::string> calls;
  std::map<std::string, Pending> pending;
  std::map<std::string, Guarded> guards;
  std::mutex mutex;
  unsigned observations{};
  bool delay_guard{};

  void set_event_sink(EventSink target) override {
    std::lock_guard lock(mutex);
    sink = std::move(target);
  }

  void execute_guarded(const std::string& command, const Json& params,
                       std::function<bool()> permit, Reply reply) override {
    if (!delay_guard) {
      BrowserEngine::execute_guarded(command, params, std::move(permit), std::move(reply));
      return;
    }
    std::lock_guard lock(mutex);
    guards.emplace(params.at("operationId").get<std::string>(),
                   Guarded{command, params, std::move(permit), std::move(reply)});
  }

  void wait_guard(const std::string& id) {
    for (unsigned i = 0; i < 1000; ++i) {
      {
        std::lock_guard lock(mutex);
        if (guards.contains(id)) return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("Timed out waiting for engine UI dispatch queue");
  }

  void dispatch_guard(const std::string& id) {
    Guarded value;
    {
      std::lock_guard lock(mutex);
      auto it = guards.find(id);
      require(it != guards.end(), "Expected queued UI dispatch");
      value = std::move(it->second);
      guards.erase(it);
    }
    if (!value.permit()) {
      value.reply(failure("DISPATCH_CANCELLED", "Control changed before dispatch"));
      return;
    }
    execute(value.command, value.params, std::move(value.reply));
  }

  std::function<bool()> guard_permit(const std::string& id) {
    std::lock_guard lock(mutex);
    return guards.at(id).permit;
  }

  void execute(const std::string& command, const Json& params, Reply reply) override {
    unsigned obs_id{};
    {
      std::lock_guard lock(mutex);
      calls.push_back(command);
      if (command == "page.observe" || command == "page.inspect") obs_id = ++observations;
    }
    if (command == "workspace.ensure" || command == "tabs.create") {
      reply(success(params));
      return;
    }
    if (command == "page.observe" || command == "page.inspect") {
      reply(success({{"observationId", "snapshot_" + std::to_string(obs_id)}, {"nodes", Json::array()}}));
      return;
    }
    if (command == "page.screenshot") {
      reply(success({{"mimeType", "image/png"}, {"data", "test"}}));
      return;
    }
    if (!params.contains("operationId")) {
      reply(failure("UNSUPPORTED", "Fake engine deliberately rejects unsupported commands"));
      return;
    }
    std::lock_guard lock(mutex);
    pending.emplace(params.at("operationId").get<std::string>(), Pending{command, params, std::move(reply)});
  }

  size_t call_count() {
    std::lock_guard lock(mutex);
    return calls.size();
  }

  std::vector<std::string> calls_snapshot() {
    std::lock_guard lock(mutex);
    return calls;
  }

  size_t pending_count() {
    std::lock_guard lock(mutex);
    return pending.size();
  }

  void wait_pending(size_t count) {
    for (unsigned i = 0; i < 1000; ++i) {
      if (pending_count() == count) return;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("Timed out waiting for engine dispatch");
  }

  std::string pending_command(const std::string& id) {
    std::lock_guard lock(mutex);
    return pending.at(id).command;
  }

  Json pending_params(const std::string& id) {
    std::lock_guard lock(mutex);
    return pending.at(id).params;
  }

  void complete(const std::string& id, Json result = success()) {
    Reply reply;
    {
      std::lock_guard lock(mutex);
      auto it = pending.find(id);
      require(it != pending.end(), "Expected a dispatched engine operation");
      reply = std::move(it->second.reply);
      pending.erase(it);
    }
    reply(std::move(result));
  }

  void event(const std::string& type, Json data) {
    data["type"] = type;
    EventSink target;
    {
      std::lock_guard lock(mutex);
      target = sink;
    }
    if (target) target(data);
  }
};

Json call(Broker& broker, const std::string& connection, const std::string& method, Json params = Json::object()) {
  Json output;
  broker.dispatch(connection, {{"method", method}, {"params", std::move(params)}},
                  [&](Json value) { output = std::move(value); });
  require(!output.is_null(), "Expected a synchronous broker result");
  return output;
}

Json pair(Broker& broker, const std::string& connection, const std::string& name) {
  Json output;
  broker.dispatch(connection, {{"method", "pair.request"}, {"params", {{"name", name}}}},
                  [&](Json value) { output = std::move(value); });
  require(output.is_null(), "Pairing must await native approval");
  auto pending = broker.state()["pairings"];
  require(pending.size() == 1, "Pairing appears in native state");
  require(broker.approve_pairing(pending[0]["requestId"]), "Native pairing approval succeeds");
  require(output.value("ok", false), "Pairing returns credentials only after approval");
  require(broker.configure_client(output["result"]["clientId"], ClientPolicy::legacy(256)),
          "Synthetic fixture explicitly enables agent capabilities");
  return output["result"];
}

struct Worker {
  std::string connection, session, workspace;
  Json tab;
  Json base() const {
    return {{"agentSessionId", session}, {"workspaceId", workspace}, {"tabId", tab.value("tabId", std::string{})}};
  }
};

Worker worker(Broker& broker, const std::string& connection, const std::string& name,
              const std::string& workspace = {}) {
  Json params{{"name", name}};
  if (!workspace.empty()) params["workspaceId"] = workspace;
  auto result = call(broker, connection, "workers.create", std::move(params));
  require(result.value("ok", false), "Worker creation succeeds");
  Worker value{connection, result["result"]["agentSessionId"], result["result"]["workspaceId"], Json::object()};
  auto tab = call(broker, connection, "tabs.create",
                  {{"agentSessionId", value.session}, {"workspaceId", value.workspace}, {"url", "https://example.test/"}});
  require(tab.value("ok", false), "Tab creation succeeds");
  value.tab = tab["result"];
  return value;
}

Json action_params(Broker& broker, const Worker& w, const std::string& operation) {
  auto base = w.base();
  auto status = call(broker, w.connection, "control.status", base);
  require(status.value("ok", false), "Control status succeeds");
  auto snapshot = call(broker, w.connection, "page.observe", base);
  require(snapshot.value("ok", false), "Observation succeeds");
  base["operationId"] = operation;
  base["ownershipGeneration"] = status["result"]["ownershipGeneration"];
  base["observationId"] = snapshot["result"]["observationId"];
  base["elementRef"] = "node_1";
  return base;
}

std::future<Json> submit(Broker& broker, const Worker& w, const std::string& method, const Json& params) {
  auto promise = std::make_shared<std::promise<Json>>();
  auto future = promise->get_future();
  broker.dispatch(w.connection, {{"method", method}, {"params", params}},
                  [promise](Json result) { promise->set_value(std::move(result)); });
  return future;
}

Json await_result(std::future<Json>& future, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
  require(future.wait_for(timeout) == std::future_status::ready, "Asynchronous broker operation must complete");
  return future.get();
}

Json native_tab(Broker& broker, const Json& tab_id) {
  const auto state = broker.state();
  for (const auto& row : state["tabs"]) {
    if (row["tabId"] == tab_id) return row;
  }
  throw std::runtime_error("Expected the tab in native state");
}

void attention_kind_and_defaults(const std::filesystem::path& directory) {
  FakeEngine engine;
  Broker broker(engine, directory);
  pair(broker, "att-kind", "Attention kind fixture");
  auto a = worker(broker, "att-kind", "owner");
  const auto tab_id = a.tab["tabId"];
  const auto generation = a.tab["ownershipGeneration"];
  const auto calls_before = engine.call_count();

  const auto initial = call(broker, a.connection, "control.status", a.base())["result"];
  require(!initial["attentionRequested"].get<bool>() && !initial.contains("attentionKind"),
          "No attentionKind is reported when no attention request is active");
  require(!native_tab(broker, tab_id).contains("attentionKind"),
          "Native state omits attentionKind when inactive");

  auto make_request = [&]() {
    auto req = a.base();
    req["ownershipGeneration"] = generation;
    return req;
  };

  // persist: false and no kind -> attentionKind == "help"
  {
    auto req = make_request();
    req["persist"] = false;
    const auto res = call(broker, a.connection, "attention.request", req);
    require(res.value("ok", false), "attention.request with persist=false succeeds");
    require(res["result"]["attentionRequested"].get<bool>() &&
                !res["result"]["attentionPersists"].get<bool>() &&
                res["result"]["attentionKind"] == "help",
            "persist=false without kind defaults attentionKind to help");
    require(native_tab(broker, tab_id)["attentionKind"] == "help",
            "Native state reports default attentionKind help");
    require(call(broker, a.connection, "attention.clear", make_request()).value("ok", false),
            "Clearing attention succeeds");
  }

  // Omitting both persist and kind -> attentionKind == "help"
  {
    const auto res = call(broker, a.connection, "attention.request", make_request());
    require(res.value("ok", false) && !res["result"]["attentionPersists"].get<bool>() &&
                res["result"]["attentionKind"] == "help",
            "Omitting persist and kind defaults attentionKind to help");
    require(call(broker, a.connection, "attention.clear", make_request()).value("ok", false),
            "Clearing attention succeeds");
  }

  // persist: true and no kind -> attentionKind == "review"
  {
    auto req = make_request();
    req["persist"] = true;
    const auto res = call(broker, a.connection, "attention.request", req);
    require(res.value("ok", false), "attention.request with persist=true succeeds");
    require(res["result"]["attentionRequested"].get<bool>() &&
                res["result"]["attentionPersists"].get<bool>() &&
                res["result"]["attentionKind"] == "review",
            "persist=true without kind defaults attentionKind to review");
    require(native_tab(broker, tab_id)["attentionKind"] == "review",
            "Native state reports default attentionKind review");
    require(call(broker, a.connection, "attention.clear", make_request()).value("ok", false),
            "Clearing attention succeeds");
  }

  // Explicit kind: "review" with persist: false
  {
    auto req = make_request();
    req["persist"] = false;
    req["kind"] = "review";
    const auto res = call(broker, a.connection, "attention.request", req);
    require(res.value("ok", false) && !res["result"]["attentionPersists"].get<bool>() &&
                res["result"]["attentionKind"] == "review",
            "Explicit kind=review overrides persist=false default");
    require(native_tab(broker, tab_id)["attentionKind"] == "review",
            "Native state reports explicit kind=review");
    require(call(broker, a.connection, "attention.clear", make_request()).value("ok", false),
            "Clearing attention succeeds");
  }

  // Explicit kind: "help" with persist: true
  {
    auto req = make_request();
    req["persist"] = true;
    req["kind"] = "help";
    const auto res = call(broker, a.connection, "attention.request", req);
    require(res.value("ok", false) && res["result"]["attentionPersists"].get<bool>() &&
                res["result"]["attentionKind"] == "help",
            "Explicit kind=help overrides persist=true default");
    require(native_tab(broker, tab_id)["attentionKind"] == "help",
            "Native state reports explicit kind=help");
    require(call(broker, a.connection, "attention.clear", make_request()).value("ok", false),
            "Clearing attention succeeds");
  }

  // Invalid kind values -> INVALID_REQUEST
  for (const Json& bad_kind : {Json("urgent"), Json(""), Json("HELP"), Json(42), Json(true)}) {
    auto req = make_request();
    req["kind"] = bad_kind;
    const auto res = call(broker, a.connection, "attention.request", req);
    require(!res.value("ok", true) && code(res) == "INVALID_REQUEST",
            "Invalid attention kind is rejected with INVALID_REQUEST");
    require(!native_tab(broker, tab_id)["attentionRequested"].get<bool>() &&
                !native_tab(broker, tab_id).contains("attentionKind"),
            "Rejected attention kind leaves no active request");
  }

  // Updating an active request changes kind without bumping attentionSequence
  {
    auto req = make_request();
    req["persist"] = false;
    req["kind"] = "help";
    req["reason"] = "Solve OTP challenge";
    const auto first = call(broker, a.connection, "attention.request", req);
    require(first.value("ok", false) && first["result"]["attentionKind"] == "help",
            "Initial active request starts with kind=help");
    const auto seq = native_tab(broker, tab_id)["attentionSequence"].get<uint64_t>();
    require(seq > 0, "Active request receives a positive attentionSequence");

    // Invalid update leaves active request and sequence intact
    auto bad_update = req;
    bad_update["kind"] = "urgent";
    require(code(call(broker, a.connection, "attention.request", bad_update)) == "INVALID_REQUEST",
            "Invalid kind on active update fails with INVALID_REQUEST");
    require(native_tab(broker, tab_id)["attentionKind"] == "help" &&
                native_tab(broker, tab_id)["attentionSequence"].get<uint64_t>() == seq,
            "Rejected update preserves existing attentionKind and attentionSequence");

    // Valid update changes kind and persist without bumping attentionSequence
    auto update_review = make_request();
    update_review["kind"] = "review";
    update_review["persist"] = true;
    update_review["reason"] = "Ready for human review";
    const auto second = call(broker, a.connection, "attention.request", update_review);
    require(second.value("ok", false) && second["result"]["attentionKind"] == "review" &&
                second["result"]["attentionPersists"].get<bool>() &&
                second["result"]["attentionReason"] == "Ready for human review",
            "Updating active request changes kind, persist and reason");
    auto row = native_tab(broker, tab_id);
    require(row["attentionKind"] == "review" && row["attentionSequence"].get<uint64_t>() == seq,
            "Updating active request changes kind without bumping attentionSequence");

    auto update_help = make_request();
    update_help["kind"] = "help";
    update_help["persist"] = false;
    update_help["reason"] = "Need another code";
    const auto third = call(broker, a.connection, "attention.request", update_help);
    require(third.value("ok", false) && third["result"]["attentionKind"] == "help" &&
                !third["result"]["attentionPersists"].get<bool>(),
            "Updating active request back to help succeeds");
    row = native_tab(broker, tab_id);
    require(row["attentionKind"] == "help" && row["attentionSequence"].get<uint64_t>() == seq,
            "Repeated active kind change keeps attentionSequence unchanged");

    require(call(broker, a.connection, "attention.clear", make_request()).value("ok", false),
            "Final attention.clear succeeds");
    require(!native_tab(broker, tab_id)["attentionRequested"].get<bool>() &&
                !native_tab(broker, tab_id).contains("attentionKind"),
            "Cleared request removes attentionKind");
  }

  require(engine.call_count() == calls_before, "Attention kind metadata operations send no browser commands");
}

void human_help_hold_and_complete(const std::filesystem::path& directory) {
  FakeEngine engine;
  Broker broker(engine, directory);
  pair(broker, "help-hold", "Help hold fixture");
  auto a = worker(broker, "help-hold", "owner");
  const auto tab_id = a.tab["tabId"].get<std::string>();
  const auto initial = call(broker, a.connection, "control.status", a.base())["result"];
  require(!initial["humanPaused"].get<bool>() && !initial["humanHelpHold"].get<bool>() &&
              initial["attentionResolvedEpoch"].get<uint64_t>() == 0,
          "Fresh tab starts unpaused with no help hold and attentionResolvedEpoch == 0");

  // Holding without an active help request or human input is a no-op
  broker.set_human_help_hold(tab_id, true);
  const auto unchanged = call(broker, a.connection, "control.status", a.base())["result"];
  require(!unchanged["humanPaused"].get<bool>() && !unchanged["humanHelpHold"].get<bool>(),
          "set_human_help_hold does nothing when no attention or human input is active");

  // Request kind: "help" on owned tab
  auto att_req = a.base();
  att_req["ownershipGeneration"] = initial["ownershipGeneration"];
  att_req["kind"] = "help";
  att_req["reason"] = "Solve CAPTCHA and click Continue";
  const auto att_res = call(broker, a.connection, "attention.request", att_req);
  require(att_res.value("ok", false) && att_res["result"]["attentionKind"] == "help",
          "Worker raises an active help request");

  // Dispatch one active click and queue a second click behind it
  auto running_params = action_params(broker, a, "hold-active");
  engine.delay_guard = true;
  auto running = submit(broker, a, "page.click", running_params);
  engine.wait_guard("hold-active");
  auto permit = engine.guard_permit("hold-active");
  engine.dispatch_guard("hold-active");
  engine.wait_pending(1);
  engine.delay_guard = false;

  auto queued_params = running_params;
  queued_params["operationId"] = "hold-queued";
  auto queued = submit(broker, a, "page.click", queued_params);
  require(queued.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout,
          "Second click remains queued behind active operation");

  // Activate human help hold
  broker.set_human_help_hold(tab_id, true);

  const auto queued_res = await_result(queued);
  require(code(queued_res) == "HUMAN_ACTIVITY" && queued_res["dispatchStatus"] == "not_dispatched",
          "set_human_help_hold cancels queued work with HUMAN_ACTIVITY");
  require(!permit(), "set_human_help_hold immediately denies in-flight continuation");

  engine.complete("hold-active", failure("input_interrupted", "Interrupted by human help hold"));
  const auto running_res = await_result(running);
  require(running_res["dispatchStatus"] == "dispatched",
          "Already-dispatched operation retains dispatched status");

  const auto held = call(broker, a.connection, "control.status", a.base())["result"];
  require(held["ownerSessionId"] == a.session && held["ownershipGeneration"] == initial["ownershipGeneration"],
          "Human help hold preserves worker ownership and generation");
  require(held["humanPaused"].get<bool>(), "Human help hold sets humanPaused == true");
  require(held["humanHelpHold"].get<bool>(), "Human help hold sets humanHelpHold == true");
  require(held["humanPauseUntil"].is_null(), "Human help hold sets humanPauseUntil == null");
  require(held["requiresFreshObservation"].get<bool>(), "Human help hold requires a fresh observation");
  require(held["attentionRequested"].get<bool>() && held["attentionKind"] == "help",
          "Help request remains active during human help hold");

  auto rejected_click = running_params;
  rejected_click["operationId"] = "hold-rejected";
  require(code(call(broker, a.connection, "page.observe", a.base())) == "HUMAN_INPUT_PAUSED",
          "page.observe is rejected with HUMAN_INPUT_PAUSED while held");
  require(code(call(broker, a.connection, "page.click", rejected_click)) == "HUMAN_INPUT_PAUSED",
          "page.click is rejected with HUMAN_INPUT_PAUSED while held");

  // Physical input with an advisory cooldown still keeps humanPauseUntil == null while held
  engine.event("human.input", {{"tabId", tab_id}, {"busy", false}, {"pauseUntil", int64_t{2000000000123}}});
  const auto held_after_input = call(broker, a.connection, "control.status", a.base())["result"];
  require(held_after_input["humanPaused"].get<bool>() && held_after_input["humanHelpHold"].get<bool>() &&
              held_after_input["humanPauseUntil"].is_null(),
          "Advisory pauseUntil stays null while humanHelpHold is active");

  // Engine emits human.idle -> tab REMAINS paused because humanHelpHold is true
  const auto calls_before_idle = engine.call_count();
  engine.event("human.idle", {{"tabId", tab_id}});
  const auto after_idle = call(broker, a.connection, "control.status", a.base())["result"];
  require(after_idle["humanPaused"].get<bool>() && after_idle["humanHelpHold"].get<bool>() &&
              after_idle["humanPauseUntil"].is_null() && after_idle["requiresFreshObservation"].get<bool>(),
          "human.idle leaves tab paused while humanHelpHold is active");
  require(code(call(broker, a.connection, "page.observe", a.base())) == "HUMAN_INPUT_PAUSED",
          "page.observe stays rejected after human.idle under help hold");
  require(code(call(broker, a.connection, "page.click", rejected_click)) == "HUMAN_INPUT_PAUSED",
          "page.click stays rejected after human.idle under help hold");
  require(engine.call_count() == calls_before_idle && engine.pending_count() == 0,
          "human.idle under help hold dispatches no browser commands");

  // Native complete_human_help resolves the request and hold, requiring fresh observation
  broker.complete_human_help(tab_id);
  const auto resolved = call(broker, a.connection, "control.status", a.base())["result"];
  require(!resolved["attentionRequested"].get<bool>() && !resolved.contains("attentionKind"),
          "complete_human_help clears attentionRequested and attentionKind");
  require(!resolved["humanHelpHold"].get<bool>(), "complete_human_help clears humanHelpHold");
  require(!resolved["humanPaused"].get<bool>(), "complete_human_help clears humanPaused");
  require(resolved["humanPauseUntil"].is_null(), "complete_human_help keeps humanPauseUntil null");
  require(resolved["attentionResolvedEpoch"].get<uint64_t>() == 1,
          "complete_human_help increments attentionResolvedEpoch to 1");
  require(resolved["requiresFreshObservation"].get<bool>(),
          "complete_human_help keeps requiresFreshObservation == true");
  require(resolved["ownerSessionId"] == a.session && resolved["ownershipGeneration"] == initial["ownershipGeneration"],
          "complete_human_help preserves ownership and generation");
  require(engine.call_count() == calls_before_idle && engine.pending_count() == 0,
          "complete_human_help never replays canceled work");

  // Stale observation cannot mutate; agent must re-observe first
  require(code(call(broker, a.connection, "page.click", rejected_click)) == "OBSERVATION_REQUIRED",
          "Mutating without fresh observation is rejected after complete_human_help");

  auto fresh_params = action_params(broker, a, "after-complete-help");
  require(!call(broker, a.connection, "control.status", a.base())["result"]["requiresFreshObservation"].get<bool>(),
          "Fresh observation clears requiresFreshObservation");
  auto resumed = submit(broker, a, "page.click", fresh_params);
  engine.wait_pending(1);
  engine.complete("after-complete-help");
  require(await_result(resumed).value("ok", false),
          "Freshly observed mutation succeeds after complete_human_help");
}

void control_wait_ready(const std::filesystem::path& directory) {
  FakeEngine engine;
  Broker broker(engine, directory);
  pair(broker, "wait-main", "Wait ready host");
  auto a = worker(broker, "wait-main", "waiter");
  const auto tab_id = a.tab["tabId"].get<std::string>();
  const auto generation = a.tab["ownershipGeneration"];

  // 1. Immediate return when tab is not paused
  {
    const auto immediate = call(broker, a.connection, "control.wait_ready", a.base());
    require(immediate.value("ok", false) && immediate["result"]["status"] == "ready" &&
                !immediate["result"]["control"]["humanPaused"].get<bool>() &&
                immediate["result"]["control"]["tabId"] == tab_id,
            "control.wait_ready returns ready immediately when tab is not paused");

    auto bad_timeout = a.base();
    bad_timeout["timeoutMs"] = "100";
    require(code(call(broker, a.connection, "control.wait_ready", bad_timeout)) == "INVALID_REQUEST",
            "Non-integer timeoutMs is rejected");
    auto bad_flag = a.base();
    bad_flag["waitForHelpResolution"] = "yes";
    require(code(call(broker, a.connection, "control.wait_ready", bad_flag)) == "INVALID_REQUEST",
            "Non-boolean waitForHelpResolution is rejected");

    // Review attention alone does not block waitForHelpResolution
    auto review_req = a.base();
    review_req["ownershipGeneration"] = generation;
    review_req["kind"] = "review";
    require(call(broker, a.connection, "attention.request", review_req).value("ok", false),
            "Review request succeeds");
    auto wait_review = a.base();
    wait_review["waitForHelpResolution"] = true;
    const auto review_ready = call(broker, a.connection, "control.wait_ready", wait_review);
    require(review_ready.value("ok", false) && review_ready["result"]["status"] == "ready",
            "Active kind=review does not block waitForHelpResolution");
    require(call(broker, a.connection, "attention.clear", review_req).value("ok", false),
            "Clearing review request succeeds");
  }

  // 2. Blocks while paused by human.input and wakes immediately on human.idle
  {
    engine.event("human.input", {{"tabId", tab_id}, {"busy", true}, {"pauseUntil", nullptr}});
    auto wait_params = a.base();
    wait_params["timeoutMs"] = 5000;
    auto future = submit(broker, a, "control.wait_ready", wait_params);
    require(future.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout,
            "control.wait_ready blocks while tab is paused by human.input");

    const auto start = std::chrono::steady_clock::now();
    engine.event("human.idle", {{"tabId", tab_id}});
    const auto result = await_result(future, std::chrono::seconds(2));
    require(std::chrono::steady_clock::now() - start < std::chrono::seconds(1),
            "control.wait_ready wakes immediately on human.idle");
    require(result.value("ok", false) && result["result"]["status"] == "ready",
            "Woken waiter reports status == ready");
    require(!result["result"]["control"]["humanPaused"].get<bool>() &&
                result["result"]["control"]["requiresFreshObservation"].get<bool>() &&
                result["result"]["control"]["humanActivityEpoch"].get<uint64_t>() >= 1,
            "Woken waiter returns updated post-idle control state");
  }

  // 3. waitForHelpResolution: true with kind: "help" + human.input:
  //    human.idle alone does not wake it; complete_human_help wakes it immediately
  {
    auto help_req = a.base();
    help_req["ownershipGeneration"] = generation;
    help_req["kind"] = "help";
    help_req["reason"] = "Complete verification step";
    require(call(broker, a.connection, "attention.request", help_req).value("ok", false),
            "Help request succeeds");
    engine.event("human.input", {{"tabId", tab_id}, {"busy", true}, {"pauseUntil", nullptr}});

    auto wait_params = a.base();
    wait_params["timeoutMs"] = 5000;
    wait_params["waitForHelpResolution"] = true;
    auto future = submit(broker, a, "control.wait_ready", wait_params);
    require(future.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout,
            "waitForHelpResolution blocks during human.input");

    engine.event("human.idle", {{"tabId", tab_id}});
    const auto status_after_idle = call(broker, a.connection, "control.status", a.base())["result"];
    require(!status_after_idle["humanPaused"].get<bool>() && status_after_idle["attentionRequested"].get<bool>() &&
                status_after_idle["attentionKind"] == "help",
            "Tab is unpaused after human.idle while help request remains active");
    require(future.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout,
            "human.idle alone does not resolve waitForHelpResolution while help request is active");

    const auto start = std::chrono::steady_clock::now();
    broker.complete_human_help(tab_id);
    const auto result = await_result(future, std::chrono::seconds(2));
    require(std::chrono::steady_clock::now() - start < std::chrono::seconds(1),
            "complete_human_help wakes waitForHelpResolution immediately");
    require(result.value("ok", false) && result["result"]["status"] == "ready",
            "Resolved help waiter returns ok:true and status:ready");
    require(!result["result"]["control"]["attentionRequested"].get<bool>() &&
                !result["result"]["control"]["humanPaused"].get<bool>() &&
                result["result"]["control"]["attentionResolvedEpoch"].get<uint64_t>() >= 1,
            "Resolved help waiter includes attentionResolvedEpoch >= 1 and cleared help state");
  }

  // 4. Short timeoutMs: 120 while still paused returns ok: true, status: "timeout", humanPaused: true
  {
    engine.event("human.input", {{"tabId", tab_id}, {"busy", true}, {"pauseUntil", nullptr}});
    auto wait_params = a.base();
    wait_params["timeoutMs"] = 120;
    auto future = submit(broker, a, "control.wait_ready", wait_params);
    const auto result = await_result(future, std::chrono::seconds(3));
    require(result.value("ok", false) && result["result"]["status"] == "timeout",
            "Short timeoutMs returns ok:true and status:timeout");
    require(result["result"]["control"]["humanPaused"].get<bool>() &&
                result["result"]["control"]["tabId"] == tab_id,
            "Timed-out control.wait_ready returns live paused control status");
  }

  // 5a. tab.closed cancels pending waiter with TAB_CLOSED
  {
    auto wait_params = a.base();
    wait_params["timeoutMs"] = 5000;
    auto future = submit(broker, a, "control.wait_ready", wait_params);
    require(future.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout,
            "Waiter is pending before tab.closed");
    engine.event("tab.closed", {{"tabId", tab_id}});
    const auto result = await_result(future, std::chrono::seconds(2));
    require(!result.value("ok", true) && code(result) == "TAB_CLOSED",
            "tab.closed cancels pending control.wait_ready with TAB_CLOSED");
  }

  // 5b. disconnect() cancels pending waiter with SESSION_CHANGED
  {
    pair(broker, "wait-disconnect", "Disconnect waiter host");
    auto b = worker(broker, "wait-disconnect", "disconnect-waiter");
    const auto tab_b = b.tab["tabId"].get<std::string>();
    engine.event("human.input", {{"tabId", tab_b}, {"busy", true}, {"pauseUntil", nullptr}});
    auto wait_params = b.base();
    wait_params["timeoutMs"] = 5000;
    auto future = submit(broker, b, "control.wait_ready", wait_params);
    require(future.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout,
            "Waiter is pending before disconnect()");
    broker.disconnect("wait-disconnect");
    const auto result = await_result(future, std::chrono::seconds(2));
    require(!result.value("ok", true) && code(result) == "SESSION_CHANGED",
            "disconnect() cancels pending control.wait_ready with SESSION_CHANGED");
  }
}

void batch_hover_and_key(const std::filesystem::path& directory) {
  FakeEngine engine;
  Broker broker(engine, directory);
  pair(broker, "batch-hk", "Batch hover and key fixture");
  auto a = worker(broker, "batch-hk", "owner");

  auto base = action_params(broker, a, "batch-hover-key");
  base.erase("elementRef");
  const auto calls_before_invalid = engine.call_count();

  // Invalid hover/key batch plans are rejected with INVALID_BATCH before any engine call
  for (auto steps : {
           Json::array({{{"action", "key"}, {"elementRef", "btn"}, {"key", "Enter"}}}),
           Json::array({{{"action", "key"}, {"key", "bad key with spaces"}}}),
           Json::array({{{"action", "key"}, {"key", ""}}}),
           Json::array({{{"action", "key"}, {"key", "Enter\n"}}}),
           Json::array({{{"action", "key"}, {"key", std::string(81, 'a')}}}),
           Json::array({{{"action", "key"}}}),
           Json::array({{{"action", "hover"}}}),
           Json::array({{{"action", "hover"}, {"elementRef", "btn"}, {"key", "Enter"}}}),
       }) {
    auto invalid = base;
    invalid["steps"] = std::move(steps);
    const auto res = call(broker, a.connection, "page.batch", invalid);
    require(!res.value("ok", true) && code(res) == "INVALID_BATCH",
            "Malformed hover or key batch step is rejected with INVALID_BATCH");
  }
  require(engine.call_count() == calls_before_invalid,
          "Rejected batch plans dispatch no engine commands");

  // Valid hover + key batch dispatches page.hover then page.key in order
  auto valid = base;
  valid["steps"] = Json::array({
      {{"action", "hover"}, {"elementRef", "btn"}},
      {{"action", "key"}, {"key", "Enter"}},
  });
  auto final = submit(broker, a, "page.batch", valid);

  engine.wait_pending(1);
  require(engine.pending_command("batch-hover-key") == "page.hover",
          "First batch step dispatches page.hover");
  const auto hover_params = engine.pending_params("batch-hover-key");
  require(hover_params["elementRef"] == "btn",
          "page.hover step receives expected elementRef");
  engine.complete("batch-hover-key");

  engine.wait_pending(1);
  require(engine.pending_command("batch-hover-key") == "page.key",
          "Second batch step dispatches page.key after hover completes");
  const auto key_params = engine.pending_params("batch-hover-key");
  require(key_params["key"] == "Enter" && !key_params.contains("elementRef"),
          "page.key step receives key without elementRef");
  engine.complete("batch-hover-key");

  const auto result = await_result(final);
  require(result.value("ok", false) && result["result"]["status"] == "completed",
          "hover + key batch completes cleanly");
  const auto& steps = result["result"]["steps"];
  require(steps.size() == 2, "Batch reports both step results");
  require(steps[0]["index"] == 0 && steps[0]["action"] == "hover" &&
              steps[0]["dispatchStatus"] == "dispatched" && steps[0]["response"].value("ok", false),
          "First step result records completed hover");
  require(steps[1]["index"] == 1 && steps[1]["action"] == "key" &&
              steps[1]["dispatchStatus"] == "dispatched" && steps[1]["response"].value("ok", false),
          "Second step result records completed key");

  const auto recorded = engine.calls_snapshot();
  require(recorded.size() >= 2 && recorded[recorded.size() - 2] == "page.hover" &&
              recorded[recorded.size() - 1] == "page.key",
          "Engine received page.hover followed by page.key in order");
}

#ifdef _WIN32
class PipeClient {
 public:
  std::deque<Json> pending_events;
  std::vector<Json> seen_events;

  explicit PipeClient(const std::wstring& name) {
    for (unsigned attempt = 0; attempt < 500; ++attempt) {
      pipe_ = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                          FILE_FLAG_OVERLAPPED, nullptr);
      if (pipe_ != INVALID_HANDLE_VALUE) return;
      if (GetLastError() != ERROR_PIPE_BUSY && GetLastError() != ERROR_FILE_NOT_FOUND) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("Cannot connect pipe test client");
  }

  ~PipeClient() {
    if (pipe_ != INVALID_HANDLE_VALUE) {
      CancelIoEx(pipe_, nullptr);
      CloseHandle(pipe_);
    }
  }

  PipeClient(const PipeClient&) = delete;
  PipeClient& operator=(const PipeClient&) = delete;

  void send(const std::string& payload) {
    size_t offset{};
    while (offset < payload.size()) {
      DWORD count{};
      require(io(true, const_cast<char*>(payload.data()) + offset,
                 static_cast<DWORD>(payload.size() - offset), count, 5000) &&
                  count > 0,
              "Pipe write must succeed");
      offset += count;
    }
  }

  std::optional<Json> try_receive(DWORD timeout_ms = 5000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (buffer_.find('\n') == std::string::npos) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= deadline) return std::nullopt;
      const auto rem_ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
      const auto slice = static_cast<DWORD>(rem_ms > 0 ? rem_ms : 1);
      char bytes[8192];
      DWORD count{};
      if (!io(false, bytes, sizeof(bytes), count, slice) || count == 0) return std::nullopt;
      buffer_.append(bytes, count);
    }
    const auto end = buffer_.find('\n');
    auto line = buffer_.substr(0, end);
    buffer_.erase(0, end + 1);
    return Json::parse(line);
  }

  Json receive_reply(int id, DWORD timeout_ms = 5000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
      const auto now = std::chrono::steady_clock::now();
      require(now < deadline, "Timed out waiting for pipe RPC reply");
      const auto rem_ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
      const auto slice = static_cast<DWORD>(rem_ms > 0 ? rem_ms : 1);
      auto frame = try_receive(slice);
      require(frame.has_value(), "Pipe response must arrive within timeout");
      if (frame->is_object() && frame->contains("event") && !frame->contains("id")) {
        seen_events.push_back(*frame);
        pending_events.push_back(std::move(*frame));
        continue;
      }
      require(frame->contains("id") && (*frame)["id"] == id,
              "Pipe RPC reply must match request id");
      return std::move(*frame);
    }
  }

  Json request(int id, const std::string& method, Json params = Json::object()) {
    send(Json{{"id", id}, {"method", method}, {"params", std::move(params)}}.dump() + "\n");
    return receive_reply(id);
  }

  template <typename Pred>
  Json wait_event(Pred&& pred, DWORD timeout_ms = 5000) {
    while (!pending_events.empty()) {
      auto frame = std::move(pending_events.front());
      pending_events.pop_front();
      if (pred(frame)) return frame;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
      const auto now = std::chrono::steady_clock::now();
      require(now < deadline, "Timed out waiting for unsolicited push event");
      const auto rem_ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
      const auto slice = static_cast<DWORD>(rem_ms > 0 ? rem_ms : 1);
      auto frame = try_receive(slice);
      require(frame.has_value(), "Timed out waiting for unsolicited push event");
      require(frame->is_object() && frame->contains("event") && !frame->contains("id"),
              "Expected an unsolicited push event frame");
      seen_events.push_back(*frame);
      if (pred(*frame)) return std::move(*frame);
    }
  }

  std::vector<Json> drain_events(DWORD quiet_ms = 200) {
    std::vector<Json> drained;
    while (!pending_events.empty()) {
      drained.push_back(std::move(pending_events.front()));
      pending_events.pop_front();
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(quiet_ms);
    while (true) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= deadline) break;
      const auto rem_ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
      const auto slice = static_cast<DWORD>(rem_ms > 0 ? rem_ms : 1);
      auto frame = try_receive(slice);
      if (!frame) break;
      require(frame->is_object() && frame->contains("event") && !frame->contains("id"),
              "Unexpected non-event frame while draining push events");
      seen_events.push_back(*frame);
      drained.push_back(std::move(*frame));
    }
    return drained;
  }

 private:
  HANDLE pipe_{INVALID_HANDLE_VALUE};
  std::string buffer_;

  bool io(bool write, char* buffer, DWORD length, DWORD& count, DWORD timeout_ms) {
    HANDLE signal = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!signal) return false;
    OVERLAPPED overlap{};
    overlap.hEvent = signal;
    bool ok = (write ? WriteFile(pipe_, buffer, length, &count, &overlap)
                     : ReadFile(pipe_, buffer, length, &count, &overlap)) != FALSE;
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
      if (WaitForSingleObject(signal, timeout_ms) == WAIT_OBJECT_0) {
        ok = GetOverlappedResult(pipe_, &overlap, &count, FALSE) != FALSE;
      } else {
        CancelIoEx(pipe_, &overlap);
        ok = GetOverlappedResult(pipe_, &overlap, &count, TRUE) != FALSE;
      }
    }
    CloseHandle(signal);
    return ok;
  }
};

Json pair_pipe_client(Broker& broker, PipeClient& client, int request_id, const std::string& name) {
  client.send(Json{{"id", request_id}, {"method", "pair.request"}, {"params", {{"name", name}}}}.dump() + "\n");
  std::string pending_id;
  for (unsigned i = 0; i < 500 && pending_id.empty(); ++i) {
    const auto state = broker.state();
    for (const auto& row : state["pairings"]) {
      if (row["name"] == name) {
        pending_id = row["requestId"].get<std::string>();
        break;
      }
    }
    if (pending_id.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  require(!pending_id.empty(), "Pipe pairing request appears in native state");
  require(broker.approve_pairing(pending_id), "Native pairing approval succeeds for pipe client");
  auto paired = client.receive_reply(request_id);
  require(paired.value("ok", false), "Pipe pairing returns credentials after approval");
  require(broker.configure_client(paired["result"]["clientId"], ClientPolicy::legacy(256)),
          "Pipe fixture enables agent capabilities for paired client");
  return paired["result"];
}

bool event_has_tab(const Json& ev, const std::string& tab_id, std::optional<bool> paused = std::nullopt,
                   std::optional<bool> requires_fresh = std::nullopt, uint64_t min_epoch = 0) {
  if (!ev.is_object() || ev.value("event", std::string{}) != "control.activity") return false;
  const auto tabs = ev.find("tabs");
  if (tabs == ev.end() || !tabs->is_array()) return false;
  for (const auto& row : *tabs) {
    if (!row.is_object() || row.value("tabId", std::string{}) != tab_id) continue;
    if (paused.has_value() && row.value("humanPaused", !*paused) != *paused) continue;
    if (requires_fresh.has_value() && row.value("requiresFreshObservation", !*requires_fresh) != *requires_fresh)
      continue;
    if (row.value("humanActivityEpoch", uint64_t{}) < min_epoch) continue;
    return true;
  }
  return false;
}

void pipe_server_push_events(const std::filesystem::path& directory) {
  FakeEngine engine;
  Broker broker(engine, directory);
  const auto suffix = local_security::random_hex(8);
  const auto pipe_name = L"xenon-test-push-" + std::wstring(suffix.begin(), suffix.end());
  PipeServer server(broker, pipe_name);
  broker.set_connection_event_sink([&](const std::string& conn, Json ev) {
    server.send_event(conn, std::move(ev));
  });

  struct ShutdownGuard {
    Broker& b;
    PipeServer& s;
    ~ShutdownGuard() {
      b.set_connection_event_sink({});
      s.stop();
    }
  } guard{broker, server};

  server.start();
  require(server.listening(), "PipeServer reports listening after start");

  PipeClient client_a(server.name());
  PipeClient client_b(server.name());

  const auto creds_a = pair_pipe_client(broker, client_a, 1, "Push Client A");
  const auto creds_b = pair_pipe_client(broker, client_b, 1, "Push Client B");
  require(creds_a["clientId"] != creds_b["clientId"], "Clients A and B are distinct paired principals");

  // Give Client B its own worker, workspace and tab, then settle B's initial push events
  const auto worker_b = client_b.request(2, "workers.create", {{"name", "Worker B"}});
  require(worker_b.value("ok", false), "Client B worker creation succeeds");
  const auto session_b = worker_b["result"]["agentSessionId"].get<std::string>();
  const auto workspace_b = worker_b["result"]["workspaceId"].get<std::string>();
  const auto tab_b_res = client_b.request(
      3, "tabs.create",
      {{"agentSessionId", session_b}, {"workspaceId", workspace_b}, {"url", "https://b.example.test/"}});
  require(tab_b_res.value("ok", false), "Client B tab creation succeeds");
  const auto tab_b = tab_b_res["result"]["tabId"].get<std::string>();
  client_b.wait_event([&](const Json& ev) { return event_has_tab(ev, tab_b, false); });
  client_b.drain_events(100);

  // Client A creates its own worker, separate workspace, and tab
  const auto worker_a = client_a.request(2, "workers.create", {{"name", "Worker A"}});
  require(worker_a.value("ok", false), "Client A worker creation succeeds");
  const auto session_a = worker_a["result"]["agentSessionId"].get<std::string>();
  const auto workspace_a = worker_a["result"]["workspaceId"].get<std::string>();
  require(workspace_a != workspace_b, "Client A and Client B have separate workspaces");

  const auto tab_a_res = client_a.request(
      3, "tabs.create",
      {{"agentSessionId", session_a}, {"workspaceId", workspace_a}, {"url", "https://a.example.test/"}});
  require(tab_a_res.value("ok", false), "Client A tab creation succeeds");
  const auto tab_a = tab_a_res["result"]["tabId"].get<std::string>();
  client_a.wait_event([&](const Json& ev) { return event_has_tab(ev, tab_a, false); });
  client_a.pending_events.clear();

  // Trigger human.input on A's tab: Client A receives unsolicited push with humanPaused: true
  engine.event("human.input", {{"tabId", tab_a}, {"busy", true}, {"pauseUntil", nullptr}});
  const auto paused_ev = client_a.wait_event(
      [&](const Json& ev) { return event_has_tab(ev, tab_a, true, true, 1); });
  require(paused_ev["event"] == "control.activity" && !paused_ev.contains("id"),
          "Client A receives unsolicited control.activity push with humanPaused=true");

  // Regular RPC calls continue working cleanly alongside push frames while paused
  const auto workers_rpc = client_a.request(10, "workers.list");
  require(workers_rpc.value("ok", false) && workers_rpc["id"] == 10,
          "Regular RPC workers.list succeeds alongside push events");
  const auto status_paused = client_a.request(
      11, "control.status", {{"agentSessionId", session_a}, {"workspaceId", workspace_a}, {"tabId", tab_a}});
  require(status_paused.value("ok", false) && status_paused["result"]["humanPaused"].get<bool>(),
          "Regular RPC control.status succeeds while paused");

  // Trigger human.idle on A's tab: Client A receives unsolicited push with humanPaused: false
  engine.event("human.idle", {{"tabId", tab_a}});
  const auto idle_ev = client_a.wait_event(
      [&](const Json& ev) { return event_has_tab(ev, tab_a, false, true, 1); });
  require(idle_ev["event"] == "control.activity" && !idle_ev.contains("id"),
          "Client A receives unsolicited control.activity push with humanPaused=false");

  // Regular RPC calls continue working cleanly after idle
  const auto status_idle = client_a.request(
      12, "control.status", {{"agentSessionId", session_a}, {"workspaceId", workspace_a}, {"tabId", tab_a}});
  require(status_idle.value("ok", false) && !status_idle["result"]["humanPaused"].get<bool>() &&
              status_idle["result"]["requiresFreshObservation"].get<bool>(),
          "Regular RPC control.status reflects post-idle state cleanly");
  const auto obs_a = client_a.request(
      13, "page.observe", {{"agentSessionId", session_a}, {"workspaceId", workspace_a}, {"tabId", tab_a}});
  require(obs_a.value("ok", false), "Regular RPC page.observe succeeds after idle");
  client_a.wait_event([&](const Json& ev) { return event_has_tab(ev, tab_a, false, false, 1); });
  const auto final_rpc_a = client_a.request(14, "workers.list");
  require(final_rpc_a.value("ok", false) && final_rpc_a["id"] == 14,
          "Client A RPC channel remains healthy after multiple push events");

  // Verify Client B never receives Client A's tab in any push event, and B's RPCs still work
  const auto status_b = client_b.request(
      20, "control.status", {{"agentSessionId", session_b}, {"workspaceId", workspace_b}, {"tabId", tab_b}});
  require(status_b.value("ok", false) && status_b["result"]["tabId"] == tab_b &&
              !status_b["result"]["humanPaused"].get<bool>(),
          "Client B RPC channel remains healthy and unaffected by Client A activity");
  const auto b_after = client_b.drain_events(200);
  require(b_after.empty(), "Client B receives no unsolicited push frames from Client A activity");
  for (const auto& ev : client_b.seen_events) {
    require(!event_has_tab(ev, tab_a), "Client B never receives Client A's tab in any push event");
    if (ev.contains("tabs") && ev["tabs"].is_array()) {
      for (const auto& row : ev["tabs"]) {
        require(row.value("tabId", std::string{}) != tab_a &&
                    row.value("workspaceId", std::string{}) != workspace_a,
                "Client B push payloads never disclose Client A's tab or workspace");
      }
    }
  }

  // Clean shutdown
  broker.set_connection_event_sink({});
  server.stop();
  require(!server.listening(), "Stopped PipeServer reports no listener");
}
#endif

}  // namespace

int main() {
  try {
    TempDir root("xenon-broker-improvements-");
    attention_kind_and_defaults(root.path / "attention-kind");
    human_help_hold_and_complete(root.path / "human-help-hold");
    control_wait_ready(root.path / "control-wait-ready");
    batch_hover_and_key(root.path / "batch-hover-key");
#ifdef _WIN32
    pipe_server_push_events(root.path / "pipe-push-events");
#endif
    std::cout << "Broker improvements tests passed: attention kinds, human help hold/complete, "
                 "control.wait_ready, batch hover/key, and PipeServer push events\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Broker improvements test failure: " << error.what() << '\n';
    return 1;
  }
}
