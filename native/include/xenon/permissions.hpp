#pragma once
#include "xenon/contracts.hpp"
#include <cstddef>
#include <algorithm>
#include <set>
#include <string>

namespace xenon {
// Native configuration only. These types are never accepted from an MCP tool.
struct Capabilities {
  bool interaction{}, uploads{}, downloads{}, saved_accounts{};
  static Capabilities full() { return {true,true,true,true}; }
  Capabilities intersect(const Capabilities& other) const {
    return {interaction&&other.interaction, uploads&&other.uploads,
            downloads&&other.downloads, saved_accounts&&other.saved_accounts};
  }
  Json json() const { return {{"interaction",interaction},{"uploads",uploads},
    {"downloads",downloads},{"savedAccounts",saved_accounts}}; }
  static Capabilities read(const Json& value) {
    return {value.value("interaction",false),value.value("uploads",false),
      value.value("downloads",false),value.value("savedAccounts",false)};
  }
  bool operator==(const Capabilities&) const = default;
};
struct ClientPolicy {
  Capabilities capabilities;
  bool automatic_workspaces{};
  // Zero means no additional client quota, for migrated legacy clients.
  size_t max_workers{4}, max_automatic_workspaces{4};
  static ClientPolicy legacy(size_t global_workers) {
    return {Capabilities::full(),true,global_workers,0};
  }
  bool valid() const { return max_workers<=256&&max_automatic_workspaces<=64; }
  Json json() const {
    auto value=capabilities.json(); value["automaticWorkspaces"]=automatic_workspaces;
    value["maxWorkers"]=max_workers;value["maxAutomaticWorkspaces"]=max_automatic_workspaces;return value;
  }
  static ClientPolicy read(const Json& value) {
    return {Capabilities::read(value),value.value("automaticWorkspaces",false),
      value.value("maxWorkers",size_t{4}),value.value("maxAutomaticWorkspaces",size_t{4})};
  }
};
struct WorkspaceAccess {
  Capabilities capabilities;
  bool inherit_files{}, inherit_accounts{};
  // A restriction with an empty set intentionally grants no inherited resource.
  bool restrict_files{}, restrict_accounts{};
  std::set<std::string> file_ids, account_ids;
  static WorkspaceAccess legacy() { WorkspaceAccess value;value.capabilities=Capabilities::full();return value; }
  static WorkspaceAccess automatic(const ClientPolicy& policy) {
    WorkspaceAccess value;value.capabilities=policy.capabilities;
    value.inherit_files=true;value.inherit_accounts=true;return value;
  }
  Json json() const {
    auto value=capabilities.json();value["inheritFiles"]=inherit_files;value["inheritAccounts"]=inherit_accounts;
    value["restrictFiles"]=restrict_files;value["restrictAccounts"]=restrict_accounts;
    value["fileIds"]=file_ids;value["accountIds"]=account_ids;return value;
  }
  static WorkspaceAccess read(const Json& value) {
    WorkspaceAccess access;access.capabilities=Capabilities::read(value);
    access.inherit_files=value.value("inheritFiles",false);access.inherit_accounts=value.value("inheritAccounts",false);
    access.restrict_files=value.value("restrictFiles",false);access.restrict_accounts=value.value("restrictAccounts",false);
    for(const auto& id:value.value("fileIds",Json::array()))if(id.is_string())access.file_ids.insert(id.get<std::string>());
    for(const auto& id:value.value("accountIds",Json::array()))if(id.is_string())access.account_ids.insert(id.get<std::string>());
    return access;
  }
};
inline std::string client_file_scope(const std::string& client) { return "client:"+client; }
inline bool valid_workspace_name(const std::string& name){
  if(name.empty()||name.size()>320||std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32||c==127;}))return false;
  return std::count_if(name.begin(),name.end(),[](unsigned char c){return (c&0xc0)!=0x80;})<=80;
}
}
