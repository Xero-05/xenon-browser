#pragma once
// This driver is compiled only into the separately named, never-packaged
// AuthTest browser. It has no renderer binding, native pipe or MCP endpoint.
// It performs one fixed gesture: a synthetic touchpad pinch on a broker tab of
// a marked disposable profile, the gesture a person makes on a precision
// touchpad. It can also record a person's passive hover position, as the mouse
// hook does, without sending input. Results carry viewport numbers only, never
// page content.
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
#include "xenon/cef_engine.hpp"
#include "xenon/local_security.hpp"
#include "include/cef_task.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>

namespace xenon {
class NativePageScaleFixture : public std::enable_shared_from_this<NativePageScaleFixture> {
 public:
  NativePageScaleFixture(std::filesystem::path root, CefEngine& engine)
      : root_(std::filesystem::absolute(root).lexically_normal()), engine_(engine) {
    if (root_.parent_path().filename() != L".cache" || !root_.filename().wstring().starts_with(L"pinch-zoom-integration-"))
      throw std::runtime_error("Native page scale fixture requires a disposable profile");
    const auto attributes = GetFileAttributesW(root_.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
      throw std::runtime_error("Invalid native fixture profile");
    std::ifstream marker(root_ / "SYNTHETIC_TEST_PROFILE", std::ios::binary);
    const std::string contents{std::istreambuf_iterator<char>(marker), {}};
    if (contents != "XENON_SYNTHETIC_PINCH_FIXTURE\n" || std::filesystem::exists(root_ / "page-scale-result.json"))
      throw std::runtime_error("Native fixture marker is missing or already used");
  }
  void start() { CefPostDelayedTask(TID_UI, new Tick(weak_from_this()), 100); }

 private:
  class Tick final : public CefTask {
   public:
    explicit Tick(std::weak_ptr<NativePageScaleFixture> owner) : owner_(std::move(owner)) {}
    void Execute() override { if (auto owner = owner_.lock()) owner->poll(); }
   private:
    std::weak_ptr<NativePageScaleFixture> owner_;
    IMPLEMENT_REFCOUNTING(Tick);
  };
  static bool handle(const Json& value) {
    if (!value.is_string()) return false;
    const auto& text = value.get_ref<const std::string&>();
    return !text.empty() && text.size() <= 256 && text.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:-") == std::string::npos;
  }
  static Json numbers(const Json& value) {
    Json result = Json::object();
    if (!value.is_object()) return result;
    for (const auto& [key, item] : value.items())
      if (handle(Json(key)) && item.is_number() && std::isfinite(item.get<double>())) result[key] = item;
    return result;
  }
  static Json safe_reply(const Json& reply) {
    if (!reply.value("ok", false)) {
      const auto error = reply.value("error", Json::object());
      return failure(handle(error.value("code", Json())) ? error.at("code").get<std::string>() : "FIXTURE_FAILED", "Native page scale request was rejected");
    }
    const auto value = reply.value("result", Json::object());
    Json result = Json::object();
    for (const auto* phase : {"before", "after"}) {
      if (!value.contains(phase) || !value.at(phase).is_object()) continue;
      for (const auto* viewport : {"cssLayoutViewport", "cssVisualViewport", "visualViewport"})
        result[phase][viewport] = numbers(value.at(phase).value(viewport, Json::object()));
    }
    return success(std::move(result));
  }
  void finish(uint64_t id, Json value) {
    try {
      const auto temporary = root_ / "page-scale-result.tmp";
      auto output = safe_reply(value); output["id"] = id;
      { std::ofstream file(temporary, std::ios::binary | std::ios::trunc); file << output.dump(); file.flush(); if (!file) throw std::runtime_error("Fixture result write failed"); }
      local_security::restrict_path(temporary);
      // The harness can briefly hold the previous result open while polling.
      // Retry only the already-written diagnostic, never the gesture.
      pending_commit_tries_ = 20;
      commit_result();
    } catch (const std::exception&) { /* The harness reports a bounded timeout. */ }
    start();
  }
  void commit_result() {
    if (!pending_commit_tries_) return;
    const auto path = root_ / "page-scale-result.json", temporary = root_ / "page-scale-result.tmp";
    if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) pending_commit_tries_ = 0;
    else {
      const auto error = GetLastError();
      if (error == ERROR_SHARING_VIOLATION || error == ERROR_ACCESS_DENIED) --pending_commit_tries_;
      else pending_commit_tries_ = 0;
    }
  }
  void poll() {
    if (pending_commit_tries_) { commit_result(); start(); return; }
    uint64_t id = 0;
    try {
      const auto path = root_ / "page-scale-request.json";
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
      const auto tab = request.at("tabId");
      if (!handle(tab)) throw std::runtime_error("Invalid fixture tab");
      const auto weak = weak_from_this();
      // A person's passive hover at a page-window client pixel. It records the
      // position the mouse hook would; it sends no input and pauses nothing.
      if (action == "hover") {
        const auto x = request.at("clientX").get<int>(), y = request.at("clientY").get<int>();
        if (x < 0 || y < 0 || x > 16384 || y > 16384) throw std::runtime_error("Invalid fixture point");
        engine_.fixture_human_pointer(tab.get<std::string>(), x, y, [weak, id](Json reply) { if (auto self = weak.lock()) self->finish(id, std::move(reply)); });
        return;
      }
      // A relative pinch factor; the reciprocal of an earlier pinch restores it.
      // Factor 1 reads the viewports without a gesture.
      double scale = 1;
      if (action == "pinch") {
        scale = request.at("scale").get<double>();
        if (!std::isfinite(scale) || scale < 0.2 || scale > 5 || std::abs(scale - 1) < 0.01) throw std::runtime_error("Invalid fixture scale");
      } else if (action != "metrics") throw std::runtime_error("Unknown fixture action");
      engine_.fixture_page_scale(tab.get<std::string>(), scale, [weak, id](Json reply) { if (auto self = weak.lock()) self->finish(id, std::move(reply)); });
    } catch (const std::exception&) { finish(id, failure("FIXTURE_FAILED", "Native fixture request failed")); }
  }
  std::filesystem::path root_;
  CefEngine& engine_;
  uint64_t last_id_{};
  unsigned pending_commit_tries_{};
};
}
#endif
