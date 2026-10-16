#pragma once
#include "xenon/contracts.hpp"
#include "xenon/omnibox.hpp"
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

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
  // Address-bar suggestions from this workspace's own bookmarks and history.
  std::vector<omnibox::Match> suggest(const std::string& workspace,bool private_mode,const std::string& input,bool inline_completion,size_t limit=omnibox::default_limit);
  // Deletes every history visit to this exact URL; a bookmark for it is kept.
  bool remove_history(const std::string& workspace,bool private_mode,const std::string& url);
  bool flush();
  void forget(const std::string& workspace){documents_.erase(workspace);}
 private:
  // The suggestion index follows new visits in place and is rebuilt on first
  // use after other changes.
  struct Document {Json value;bool private_mode{},dirty{};std::shared_ptr<omnibox::Index> index;};
  std::filesystem::path root_;std::map<std::string,Document> documents_;
  Document& load(const std::string&,bool);
};
}
