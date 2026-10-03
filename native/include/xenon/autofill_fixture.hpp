#pragma once
// This driver is compiled only into the separately named, never-packaged
// AuthTest browser. It has no renderer binding, native pipe or MCP endpoint.
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
#include "xenon/broker.hpp"
#include "xenon/cef_engine.hpp"
#include "xenon/local_security.hpp"
#include "xenon/vault.hpp"
#include "include/cef_task.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>

namespace xenon {
class NativeAutofillFixture : public std::enable_shared_from_this<NativeAutofillFixture> {
 public:
  NativeAutofillFixture(std::filesystem::path root, Broker& broker, CefEngine& engine, Vault& vault)
      : root_(std::filesystem::absolute(root).lexically_normal()), broker_(broker), engine_(engine), vault_(vault) {
    if (root_.parent_path().filename() != L".cache" || !root_.filename().wstring().starts_with(L"auth-integration-autofill-"))
      throw std::runtime_error("Native autofill fixture requires a disposable profile");
    const auto attributes = GetFileAttributesW(root_.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
      throw std::runtime_error("Invalid native fixture profile");
    std::ifstream marker(root_ / "SYNTHETIC_TEST_PROFILE", std::ios::binary);
    const std::string contents{std::istreambuf_iterator<char>(marker), {}};
    if (contents != "XENON_SYNTHETIC_AUTH_FIXTURE\n" || std::filesystem::exists(root_ / "native-autofill-result.json"))
      throw std::runtime_error("Native fixture marker is missing or already used");
  }
  void start() { CefPostDelayedTask(TID_UI, new Tick(weak_from_this()), 100); }

 private:
  class Tick final : public CefTask {
   public:
    explicit Tick(std::weak_ptr<NativeAutofillFixture> owner) : owner_(std::move(owner)) {}
    void Execute() override { if (auto owner = owner_.lock()) owner->poll(); }
   private:
    std::weak_ptr<NativeAutofillFixture> owner_;
    IMPLEMENT_REFCOUNTING(Tick);
  };
  static bool handle(const Json& value) {
    if (!value.is_string()) return false;
    const auto& text = value.get_ref<const std::string&>();
    return !text.empty() && text.size() <= 256 && text.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:-") == std::string::npos;
  }
  static bool fixture_origin(const Json& value) {
    return value == "https://127.0.0.1:18766" || value == "https://localhost:18766";
  }
  // Even test diagnostics never serialize credentials, page content or an
  // arbitrary native result. Fixture telemetry separately reports booleans.
  static Json safe_reply(const Json& reply) {
    if (!reply.value("ok", false)) {
      const auto error = reply.value("error", Json::object());
      return failure(handle(error.value("code", Json())) ? error.at("code").get<std::string>() : "FIXTURE_FAILED", "Native autofill request was rejected");
    }
    const auto value = reply.value("result", Json::object());
    Json result = Json::object();
    for (const auto key : {"offerId", "tabId", "documentId"}) if (value.contains(key) && handle(value.at(key))) result[key] = value.at(key);
    if (value.contains("origin") && fixture_origin(value.at("origin"))) result["origin"] = value.at("origin");
    if (value.contains("status") && value.at("status") == "filled") result["status"] = "filled";
    for (const auto key : {"submitted", "valid", "locked", "dismissed"}) if (value.contains(key) && value.at(key).is_boolean()) result[key] = value.at(key);
    for (const auto key : {"heldOfferSuppressed", "humanPaused", "pickerShown"}) if (value.contains(key) && value.at(key).is_boolean()) result[key] = value.at(key);
    if (value.contains("offerElapsedMs") && value.at("offerElapsedMs").is_number_integer()) result["offerElapsedMs"] = value.at("offerElapsedMs");
    if (value.contains("phase") && (value.at("phase") == "credentials" || value.at("phase") == "username" || value.at("phase") == "password")) result["phase"] = value.at("phase");
    if (value.contains("accounts") && value.at("accounts").is_array()) {
      result["accounts"] = Json::array();
      for (const auto& account : value.at("accounts")) {
        if (!account.is_object() || !account.contains("accountId") || !handle(account.at("accountId"))) continue;
        Json row{{"accountId", account.at("accountId")}};
        if (account.contains("origin") && fixture_origin(account.at("origin"))) row["origin"] = account.at("origin");
        result["accounts"].push_back(std::move(row));
        if (result["accounts"].size() >= 64) break;
      }
    }
    return success(std::move(result));
  }
  void finish(uint64_t id, Json value) {
    try {
      const auto temporary = root_ / "native-autofill-result.tmp";
      auto output = safe_reply(value); output["id"] = id;
      if (value.contains("fixtureOfferId") && handle(value.at("fixtureOfferId"))) output["fixtureOfferId"] = value.at("fixtureOfferId");
      if (value.contains("guardPendingAtCancel") && value.at("guardPendingAtCancel").is_boolean()) output["guardPendingAtCancel"] = value.at("guardPendingAtCancel");
      { std::ofstream file(temporary, std::ios::binary | std::ios::trunc); file << output.dump(); file.flush(); if (!file) throw std::runtime_error("Fixture result write failed"); }
      local_security::restrict_path(temporary);
      // The harness can briefly hold the previous result open while polling.
      // Retry only the already-written diagnostic, never the native operation.
      pending_commit_tries_ = 20;
      commit_result();
    } catch (const std::exception&) { /* The harness reports a bounded timeout. */ }
    start();
  }
  void commit_result() {
    if (!pending_commit_tries_) return;
    const auto path = root_ / "native-autofill-result.json", temporary = root_ / "native-autofill-result.tmp";
    if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) pending_commit_tries_ = 0;
    else {
      const auto error = GetLastError();
      if (error == ERROR_SHARING_VIOLATION || error == ERROR_ACCESS_DENIED) --pending_commit_tries_;
      else pending_commit_tries_ = 0;
    }
  }
  bool known_tab(const std::string& id) {
    const auto state = broker_.state();
    for (const auto& tab : state.value("tabs", Json::array()))
      if (tab.value("tabId", "") == id && tab.value("workspaceId", "") == "auth_fixture_shared") return true;
    return false;
  }
  void poll() {
    if (pending_commit_tries_) { commit_result(); start(); return; }
    uint64_t id = 0;
    try {
      const auto path = root_ / "native-autofill-request.json";
      if (!std::filesystem::exists(path)) { start(); return; }
      const auto attributes = GetFileAttributesW(path.c_str());
      if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) || std::filesystem::file_size(path) > 4096)
        throw std::runtime_error("Invalid fixture request");
      std::ifstream file(path, std::ios::binary); const auto request = Json::parse(file);
      id = request.at("id").get<uint64_t>();
      if (!id || id <= last_id_) { start(); return; }
      if (id > 200) throw std::runtime_error("Fixture request limit reached");
      last_id_ = id;
      const auto action = request.at("action").get<std::string>();
      const auto weak = weak_from_this();
      if (action == "lock" || action == "unlock") {
        const bool locked = action == "lock"; vault_.set_locked(locked);
        if (locked) { engine_.cancel_login_prompts(); broker_.stop_all(); }
        finish(id, success({{"locked", locked}})); return;
      }
      if (action == "cancel_pending") {
        const auto tab = request.at("tabId").get<std::string>(), account = request.at("accountId").get<std::string>();
        if (!known_tab(tab)) throw std::runtime_error("Unknown fixture tab");
        auto drained = std::make_shared<bool>(false);
        // The guarded call invalidates previous offers. Obtain the fresh offer
        // only AFTER reserving the finite action, then pulse cancellation while
        // the accepted native fill is waiting for that action to drain.
        engine_.execute_guarded("page.wait", {{"tabId", tab}, {"workspaceId", "auth_fixture_shared"}, {"text", ""}, {"timeoutMs", 700}},
          [] { return true; }, [drained](Json) { *drained = true; });
        engine_.request_autofill(tab, [weak, id, tab, account, drained](Json offered) {
          auto self = weak.lock(); if (!self) return;
          if (*drained || !offered.value("ok", false) || !offered.contains("result") || !offered.at("result").contains("offerId")) {
            self->finish(id, failure("FIXTURE_FAILED", "Could not establish pending guarded fill")); return;
          }
          const auto offer = offered.at("result").at("offerId").get<std::string>();
          self->offers_[offer] = tab;
          auto replied = std::make_shared<bool>(false), pulsed = std::make_shared<bool>(false);
          self->engine_.fill_saved_account(offer, account, [weak, id, offer, replied, pulsed](Json result) {
            *replied = true;
            if (auto owner = weak.lock()) {
              if (!*pulsed) { owner->finish(id, failure("FIXTURE_FAILED", "Fill did not remain pending before cancellation")); return; }
              result["fixtureOfferId"] = offer; result["guardPendingAtCancel"] = true; owner->finish(id, std::move(result));
            }
          });
          if (*replied) return;
          if (*drained) { self->finish(id, failure("FIXTURE_FAILED", "Finite action drained before cancellation")); return; }
          *pulsed = true;
          self->vault_.set_locked(true); self->engine_.cancel_login_prompts(); self->broker_.stop_all(); self->vault_.set_locked(false);
        }); return;
      }
      if (action == "request" || action == "focus_offer") {
        const auto tab = request.at("tabId").get<std::string>();
        if (!known_tab(tab)) throw std::runtime_error("Unknown fixture tab");
        auto answer = [weak, id, tab](Json reply) {
          if (auto self = weak.lock()) {
            if (reply.value("ok", false) && reply.contains("result") && reply.at("result").contains("offerId") && handle(reply.at("result").at("offerId")))
              self->offers_[reply.at("result").at("offerId").get<std::string>()] = tab;
            self->finish(id, std::move(reply));
          }
        };
        if (action == "focus_offer") engine_.fixture_autofill_focus(tab, std::move(answer));
        else engine_.request_autofill(tab, std::move(answer));
        return;
      }
      const auto offer = request.at("offerId").get<std::string>();
      if (!offers_.contains(offer)) throw std::runtime_error("Unknown fixture offer");
      if (action == "fill") {
        const auto account = request.at("accountId").get<std::string>();
        engine_.fill_saved_account(offer, account, [weak, id](Json reply) { if (auto self = weak.lock()) self->finish(id, std::move(reply)); }); return;
      }
      if (action == "valid") { finish(id, success({{"valid", engine_.autofill_offer_valid(offer)}})); return; }
      if (action == "dismiss") { engine_.dismiss_autofill(offer); finish(id, success({{"dismissed", true}})); return; }
      throw std::runtime_error("Unknown fixture action");
    } catch (const std::exception&) { finish(id, failure("FIXTURE_FAILED", "Native fixture request failed")); }
  }
  std::filesystem::path root_;
  Broker& broker_; CefEngine& engine_; Vault& vault_;
  uint64_t last_id_{};
  unsigned pending_commit_tries_{};
  std::map<std::string, std::string> offers_;
};
}
#endif
