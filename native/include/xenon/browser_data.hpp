#pragma once
#include "xenon/contracts.hpp"
#include "xenon/browser_import.hpp"
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
  // Deletes exactly these visits (URL and time).
  bool remove_visits(const std::string& workspace,bool private_mode,const std::vector<std::pair<std::string,int64_t>>& visits);
  // Deletes visits at or after `since_unix_ms` (0: all history).
  bool clear_history(const std::string& workspace,bool private_mode,int64_t since_unix_ms);
  // Bookmark folders. "" is the top level; folder IDs are opaque local tokens.
  // Bookmarks stay unique by URL and carry the ID of the folder they are in.
  std::string add_folder(const std::string& workspace,bool private_mode,const std::string& parent,const std::string& name);
  bool rename_folder(const std::string& workspace,bool private_mode,const std::string& folder,const std::string& name);
  // Deletes the folder, the folders inside it and all of their bookmarks.
  bool remove_folder(const std::string& workspace,bool private_mode,const std::string& folder);
  bool move_folder(const std::string& workspace,bool private_mode,const std::string& folder,const std::string& parent);
  bool rename_bookmark(const std::string& workspace,bool private_mode,const std::string& url,const std::string& title);
  bool move_bookmark(const std::string& workspace,bool private_mode,const std::string& url,const std::string& folder);
  // Adds another browser's bookmarks under a new top-level folder named
  // `folder_name`, and its history visits to URLs not already in history.
  // URLs already bookmarked are skipped. Returns the counts actually added.
  struct ImportCounts {size_t bookmarks{},folders{},visits{};bool ok{};};
  ImportCounts import(const std::string& workspace,bool private_mode,const browser_import::Data& data,const std::string& folder_name);
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
