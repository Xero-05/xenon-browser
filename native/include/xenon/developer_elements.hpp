#pragma once
#include "xenon/contracts.hpp"
#include <set>
#include <string>

namespace xenon {
// A diagnostic classification, never an authorization decision. Dispatch still
// requires the agent's own fresh observation and the native broker checks.
inline std::string developer_element_actions(const Json& node) {
  const auto states=node.value("states",Json::object());
  if(!node.value("targetAvailable",false)||node.value("protected",false)||states.value("disabled",false))return {};
  const auto role=node.value("role",std::string{}),tag=node.value("tag",std::string{}),type=node.value("inputType",std::string{});
  if(type=="file")return "Upload";
  if(role=="checkbox"||role=="radio"||role=="switch")return "Check";
  if(role=="textbox"||role=="searchbox"||tag=="TEXTAREA"||(tag=="INPUT"&&
    std::set<std::string>{"text","email","number","search","tel","url"}.contains(type)))
    return states.value("readonly",false)?"Focus / key":"Fill / key";
  if(role=="combobox"||role=="listbox"||tag=="SELECT")return "Select";
  if(tag=="BUTTON"||tag=="A"||std::set<std::string>{"button","link","menuitem","tab","option","treeitem","slider","spinbutton"}.contains(role))return "Click / key";
  return {};
}
// Bind each asynchronous panel read to the selected tab, document and human
// input revision. A late reply cannot restore a cleared or switched panel.
class DeveloperElementsIdentity {
 public:
  uint64_t reset(const Json& identity){identity_=identity;return ++generation_;}
  bool matches(uint64_t generation,const Json& identity) const{return generation==generation_&&identity==identity_;}
  bool changed(const Json& identity) const{return identity!=identity_;}
 private:
  Json identity_;uint64_t generation_{};
};
}
