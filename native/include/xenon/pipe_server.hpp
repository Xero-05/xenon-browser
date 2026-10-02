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
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
