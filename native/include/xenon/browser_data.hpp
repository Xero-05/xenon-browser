#pragma once
#include "xenon/contracts.hpp"
#include <filesystem>
#include <map>
#include <string>

namespace xenon {
// Human browser metadata only, never exposed through MCP. Private documents
// stay in memory and protected-authentication visits are rejected by visit().
class BrowserData {
 public:
  explicit BrowserData(std::filesystem::path root):root_(std::move(root)){}
  Json list(const std::string& workspace,bool private_mode);
  bool bookmark(const std::string& workspace,bool private_mode,const std::string& url,const std::string& title);
  bool remove_bookmark(const std::string& workspace,bool private_mode,const std::string& url);
  void visit(const std::string& workspace,bool private_mode,bool protected_auth,const std::string& url,const std::string& title);
  bool flush();
  void forget(const std::string& workspace){documents_.erase(workspace);}
 private:
  struct Document {Json value;bool private_mode{},dirty{};};
  std::filesystem::path root_;std::map<std::string,Document> documents_;
  Document& load(const std::string&,bool);
};
}
