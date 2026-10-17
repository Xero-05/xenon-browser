#pragma once
#include "xenon/contracts.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace xenon {
// Native shell bookkeeping for agents' help requests, kept free of Windows
// calls so it can be tested. Input is the broker's native tab state; times
// are the shell's tick counts. Nothing here is reachable through MCP.
class AttentionNotices {
 public:
  // At most one desktop alert (notification or taskbar flash) per tab per minute.
  static constexpr uint64_t alert_interval_ms=60000;
  struct Arrival { std::string tab_id; uint64_t sequence{}; };
  // A request the person has not yet been present for; its tab flashes.
  static bool pending(const Json& tab) { return tab.value("attentionRequested",false)&&!tab.value("attentionSeen",false); }
  // Requests that are new since the last update, each returned once. A new
  // sequence for the same tab is a new request. `now` marks when it arrived.
  std::vector<Arrival> update(const std::map<std::string,Json>& tabs,uint32_t now) {
    std::vector<Arrival> arrivals;
    for(auto entry=requests_.begin();entry!=requests_.end();){auto tab=tabs.find(entry->first);
      if(tab==tabs.end()||!tab->second.value("attentionRequested",false))entry=requests_.erase(entry);else ++entry;}
    for(auto entry=alerted_.begin();entry!=alerted_.end();){if(!tabs.contains(entry->first))entry=alerted_.erase(entry);else ++entry;}
    for(const auto& [id,tab]:tabs){
      if(!tab.value("attentionRequested",false))continue;
      const auto found=tab.find("attentionSequence");if(found==tab.end()||!found->is_number_unsigned())continue;
      const auto sequence=found->get<uint64_t>();auto& request=requests_[id];
      if(request.sequence==sequence)continue;
      request={sequence,now,false};if(pending(tab))arrivals.push_back({id,sequence});
    }
    return arrivals;
  }
  // True once per request when the person gave input after it arrived while
  // its tab was in front of them. Input before the request proves nothing.
  bool acknowledge(const std::string& tab_id,uint32_t last_input) {
    auto found=requests_.find(tab_id);
    if(found==requests_.end()||found->second.acknowledged||static_cast<int32_t>(last_input-found->second.since)<=0)return false;
    found->second.acknowledged=true;return true;
  }
  bool allow_alert(const std::string& tab_id,uint64_t now) {
    auto found=alerted_.find(tab_id);
    if(found!=alerted_.end()&&now-found->second<alert_interval_ms)return false;
    alerted_[tab_id]=now;return true;
  }
 private:
  struct Request { uint64_t sequence{}; uint32_t since{}; bool acknowledged{}; };
  std::map<std::string,Request> requests_;
  std::map<std::string,uint64_t> alerted_;
};
}
