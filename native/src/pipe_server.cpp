#include "xenon/pipe_server.hpp"
#include "xenon/broker.hpp"
#include "xenon/local_security.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace xenon {
#ifdef _WIN32
namespace {
constexpr size_t max_request_bytes = 1024 * 1024;
constexpr size_t max_response_bytes = 16 * 1024 * 1024;
constexpr size_t max_queued_bytes = 32 * 1024 * 1024;
constexpr DWORD min_retry_ms = 10, max_retry_ms = 1000;
struct Handle {
  HANDLE value{INVALID_HANDLE_VALUE};
  explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) : value(handle) {}
  ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
  Handle(const Handle&) = delete;
};
bool transfer(HANDLE pipe, HANDLE stop, bool writing, void* data, DWORD size, DWORD& count) {
  Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr)); if (!event.value) return false;
  OVERLAPPED operation{}; operation.hEvent = event.value;
  auto started = writing ? WriteFile(pipe, data, size, &count, &operation) : ReadFile(pipe, data, size, &count, &operation);
  if (started) return true;
  if (GetLastError() != ERROR_IO_PENDING) return false;
  HANDLE signals[]{stop, event.value};
  auto status = WaitForMultipleObjects(2, signals, FALSE, INFINITE);
  if (status != WAIT_OBJECT_0 + 1) {
    CancelIoEx(pipe, &operation);
    GetOverlappedResult(pipe, &operation, &count, TRUE);
    return false;
  }
  return GetOverlappedResult(pipe, &operation, &count, FALSE) != FALSE;
}
}
struct PipeServer::Impl {
  struct Connection : std::enable_shared_from_this<Connection> {
    Broker& broker;
    Handle pipe, stop_event;
    std::string id;
    std::atomic<bool> alive{true}, reader_done{}, writer_done{};
    std::atomic<size_t> pending{};
    std::mutex mutex;
    std::condition_variable condition;
    std::deque<std::string> writes;
    size_t queued_bytes{};
    std::thread reader, writer;
    Connection(Broker& target, HANDLE handle) : broker(target), pipe(handle), stop_event(CreateEventW(nullptr, TRUE, FALSE, nullptr)), id("conn_" + local_security::random_hex(16)) {
      if (!stop_event.value) throw std::runtime_error("Cannot create IPC stop event");
    }
    ~Connection() { stop(); if (reader.joinable()) reader.join(); if (writer.joinable()) writer.join(); }
    void stop() { alive = false; SetEvent(stop_event.value); condition.notify_all(); CancelIoEx(pipe.value, nullptr); }
    void enqueue(Json value) {
      if (!alive) return;
      std::string encoded;
      try { encoded = value.dump(); } catch (...) { stop(); return; }
      if (encoded.size() > max_response_bytes) { auto error = failure("RESPONSE_TOO_LARGE", "Response exceeds the local IPC limit; request a smaller observation"); error["id"] = value.value("id", Json(nullptr)); encoded = error.dump(); }
      encoded.push_back('\n');
      { std::lock_guard lock(mutex); if (queued_bytes + encoded.size() > max_queued_bytes) { stop(); return; } queued_bytes += encoded.size(); writes.push_back(std::move(encoded)); }
      condition.notify_one();
    }
    void start() {
      // The server's connection collection owns the object until both threads
      // are joined. Capturing shared_ptr here could destroy/join a thread from
      // inside itself after the last external owner goes away.
      auto* self = this;
      writer = std::thread([self] {
        while (self->alive) {
          std::string data;
          { std::unique_lock lock(self->mutex); self->condition.wait(lock, [&] { return !self->alive || !self->writes.empty(); });
            if (!self->alive) break;
            data = std::move(self->writes.front()); self->writes.pop_front(); self->queued_bytes -= data.size();
          }
          size_t offset{};
          while (self->alive && offset < data.size()) {
            DWORD count{};
            if (!transfer(self->pipe.value, self->stop_event.value, true, data.data() + offset, static_cast<DWORD>(data.size() - offset), count) || !count) { self->stop(); break; }
            offset += count;
          }
        }
        self->writer_done = true;
      });
      reader = std::thread([self] {
        std::string buffer; char chunk[8192];
        while (self->alive) {
          DWORD count{};
          if (!transfer(self->pipe.value, self->stop_event.value, false, chunk, sizeof(chunk), count) || !count) break;
          buffer.append(chunk, count);
          while (self->alive) {
            auto newline = buffer.find('\n');
            if (newline == std::string::npos) { if (buffer.size() > max_request_bytes) self->stop(); break; }
            if (newline > max_request_bytes) { self->stop(); break; }
            auto message = buffer.substr(0, newline); buffer.erase(0, newline + 1);
            Json request = Json::parse(message, nullptr, false);
            if (!request.is_object() || !request.contains("id") || !(request["id"].is_number_integer() || request["id"].is_string()) || (request["id"].is_string() && request["id"].get_ref<const std::string&>().size() > 128)) {
              self->enqueue({{"id", nullptr}, {"ok", false}, {"error", {{"code", "INVALID_REQUEST"}, {"message", "Expected a JSON request with an integer or string id"}}}}); continue;
            }
            auto request_id = request["id"];
            if (self->pending.fetch_add(1) >= 64) { --self->pending; auto error = failure("CAPACITY_EXCEEDED", "Too many outstanding requests on this connection"); error["id"] = request_id; self->enqueue(std::move(error)); continue; }
            auto answered = std::make_shared<std::atomic<bool>>(false);
            std::weak_ptr<Connection> weak = self->shared_from_this();
            auto reply = [weak, request_id, answered](Json result) {
              if (answered->exchange(true)) return;
              if (auto connection = weak.lock()) { --connection->pending; if (!result.is_object()) result = failure("INTERNAL_ERROR", "Invalid response"); result["id"] = request_id; connection->enqueue(std::move(result)); }
            };
            try { self->broker.dispatch(self->id, request, reply); }
            catch (...) { reply(failure("INTERNAL_ERROR", "The local browser could not process this request")); }
          }
        }
        self->stop(); self->broker.disconnect(self->id); self->reader_done = true;
      });
    }
  };
  Broker& broker;
  std::wstring pipe_name;
  Handle stop_event;
  std::atomic<bool> running{}, listening{};
  std::thread acceptor;
  std::mutex mutex;
  std::vector<std::shared_ptr<Connection>> connections;
  Impl(Broker& target, std::wstring name) : broker(target), pipe_name(std::move(name)), stop_event(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
    if (pipe_name.rfind(L"\\\\.\\pipe\\", 0) != 0) pipe_name = L"\\\\.\\pipe\\" + pipe_name;
    if (!stop_event.value) throw std::runtime_error("Cannot create IPC stop event");
    if (pipe_name.size() > 240 || pipe_name.substr(9).find_first_of(L"\\/") != std::wstring::npos) throw std::runtime_error("Invalid local pipe name");
  }
  HANDLE create(bool first) {
    local_security::SecurityDescriptor security;
    auto handle = CreateNamedPipeW(pipe_name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
      32, 65536, 65536, 0, &security.attributes);
    if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create private browser pipe (another instance may already own it)");
    return handle;
  }
  HANDLE try_create() noexcept { try { return create(false); } catch (...) { return INVALID_HANDLE_VALUE; } }
  // The client opened and closed this instance before or while it was being
  // accepted. Only this instance is unusable.
  static bool abandoned(DWORD error) { return error == ERROR_NO_DATA || error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED; }
  void start() {
    if (running.exchange(true)) return;
    ResetEvent(stop_event.value);
    HANDLE initial;
    try { initial = create(true); } catch (...) { running = false; throw; }
    listening = true;
    acceptor = std::thread([this, initial] {
      HANDLE pending = initial;
      DWORD delay = min_retry_ms;
      // Bounded backoff for failures that clients did not cause; stop() ends it.
      auto pause = [&] {
        const bool stopping = WaitForSingleObject(stop_event.value, delay) == WAIT_OBJECT_0;
        delay = (std::min)(delay * 2, max_retry_ms);
        return running && !stopping;
      };
      while (running) {
        if (pending == INVALID_HANDLE_VALUE) {
          // A listener that cannot be recreated now is retried, not abandoned
          // until the next browser start.
          pending = try_create(); listening = pending != INVALID_HANDLE_VALUE;
          if (!listening) { if (!pause()) break; continue; }
        }
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event.value) { if (!pause()) break; continue; }
        OVERLAPPED overlapped{}; overlapped.hEvent = event.value;
        bool connected = ConnectNamedPipe(pending, &overlapped) != FALSE;
        DWORD error{};
        if (!connected) {
          error = GetLastError();
          if (error == ERROR_PIPE_CONNECTED) connected = true;
          else if (error == ERROR_IO_PENDING) {
            HANDLE signals[]{stop_event.value, event.value};
            if (WaitForMultipleObjects(2, signals, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) { DWORD count{}; connected = GetOverlappedResult(pending, &overlapped, &count, FALSE) != FALSE; if (!connected) error = GetLastError(); }
            else { CancelIoEx(pending, &overlapped); DWORD count{}; GetOverlappedResult(pending, &overlapped, &count, TRUE); }
          }
        }
        if (!running) break;
        if (!connected) {
          // Replace only the failed instance. The replacement is created before
          // the close so this process never releases the pipe name in between.
          auto replacement = try_create();
          CloseHandle(pending); pending = replacement;
          if (!abandoned(error) && !pause()) break;
          continue;
        }
        delay = min_retry_ms;
        std::shared_ptr<Connection> connection;
        try {
          // Ownership moves before construction: a throwing constructor has
          // already closed the handle through its member.
          connection = std::make_shared<Connection>(broker, std::exchange(pending, INVALID_HANDLE_VALUE));
          { std::lock_guard lock(mutex);
            for (auto it = connections.begin(); it != connections.end();) { if ((*it)->reader_done && (*it)->writer_done) it = connections.erase(it); else ++it; }
            connections.push_back(connection);
          }
          connection->start();
        } catch (...) {
          // Drop only this client; the listener keeps accepting.
          if (connection) { std::lock_guard lock(mutex); std::erase(connections, connection); }
          connection.reset();
          if (!pause()) break;
          continue;
        }
        connection.reset();
        // Reserve a listener only when an instance slot is available. Hitting
        // the connection budget must not permanently terminate the acceptor.
        while (running) {
          size_t count{};
          { std::lock_guard lock(mutex);
            for (auto it = connections.begin(); it != connections.end();) { if ((*it)->reader_done && (*it)->writer_done) it = connections.erase(it); else ++it; }
            count = connections.size();
          }
          if (count < 32) break;
          if (WaitForSingleObject(stop_event.value, 100) == WAIT_OBJECT_0) break;
        }
      }
      if (pending != INVALID_HANDLE_VALUE) CloseHandle(pending);
      listening = false;
      running = false;
    });
  }
  void stop() {
    running = false; SetEvent(stop_event.value);
    if (acceptor.joinable()) acceptor.join();
    std::vector<std::shared_ptr<Connection>> close;
    { std::lock_guard lock(mutex); close.swap(connections); }
    for (auto& connection : close) connection->stop();
    for (auto& connection : close) { if (connection->reader.joinable()) connection->reader.join(); if (connection->writer.joinable()) connection->writer.join(); }
  }
};
#else
struct PipeServer::Impl {
  std::wstring pipe_name;
  bool listening{};
  Impl(Broker&, std::wstring name) : pipe_name(std::move(name)) {}
  void start() { throw std::runtime_error("Windows named pipes are required"); }
  void stop() {}
};
#endif
PipeServer::PipeServer(Broker& broker, std::wstring name) : impl_(std::make_unique<Impl>(broker, std::move(name))) {}
PipeServer::~PipeServer() { stop(); }
void PipeServer::start() { impl_->start(); }
void PipeServer::stop() { impl_->stop(); }
std::wstring PipeServer::name() const { return impl_->pipe_name; }
bool PipeServer::listening() const { return impl_->listening; }
}
