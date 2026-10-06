#pragma once
#include <memory>
#include <string>

namespace xenon {
class Broker;
// Local Windows transport. MCP remains stdio in the adapter; this is private IPC.
class PipeServer {
 public:
  explicit PipeServer(Broker& broker, std::wstring pipe_name = L"xenon-browser");
  ~PipeServer();
  PipeServer(const PipeServer&) = delete;
  PipeServer& operator=(const PipeServer&) = delete;
  void start();
  void stop();
  std::wstring name() const;
  // False before start(), after stop(), and while a listener instance cannot
  // be created and the acceptor is retrying. Waiting at the connection budget
  // is not reported as unavailable.
  bool listening() const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
