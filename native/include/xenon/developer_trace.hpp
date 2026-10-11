#pragma once
#include "xenon/contracts.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <deque>
#include <mutex>

namespace xenon {
// Native-human-only, volatile diagnostics. Never included in broker state or MCP.
class DeveloperTrace {
 public:
  void enable(bool enabled) { std::lock_guard lock(mutex_); enabled_=enabled; clear_locked(); }
  void clear() { std::lock_guard lock(mutex_); clear_locked(); }
  Json snapshot() const {
    std::lock_guard lock(mutex_);Json entries=Json::array();
    for(const auto& entry:entries_)entries.push_back(entry);
    return {{"enabled",enabled_},{"entries",std::move(entries)}};
  }
  uint64_t begin(const std::string& connection,const Json& request) {
    std::lock_guard lock(mutex_);if(!enabled_)return 0;
    auto id=++sequence_;const auto method=request.value("method",Json()).is_string()?request["method"].get<std::string>():std::string{};
    Json entry{{"traceId",std::to_string(id)},{"connectionId",connection},{"method",method},
      {"startedAtUnixMs",now()},{"status","pending"},{"request",sanitize(request,0,true)}};
    bound(entry);entries_.push_back(std::move(entry));trim();return id;
  }
  void finish(uint64_t id,const Json& response) {
    if(!id)return;
    std::lock_guard lock(mutex_);if(!enabled_)return;
    for(auto& entry:entries_)if(entry["traceId"]==std::to_string(id)){
      const bool ok=response.is_object()&&response.value("ok",false);
      entry["response"]=sanitize(response);entry["status"]=ok?"completed":"error";
      entry["elapsedMs"]=now()-entry["startedAtUnixMs"].get<int64_t>();
      // Only engine-returned PNG evidence is eligible for preview.
      if((entry["method"]=="page.inspect"||entry["method"]=="page.screenshot")&&ok){
        const auto result=response.value("result",Json::object());
        if(result.is_object()&&result.value("mimeType",std::string{})=="image/png"&&result.contains("data")&&result["data"].is_string()&&result["data"].get_ref<const std::string&>().size()<=max_entry_bytes/2)
          entry["screenshot"]=result["data"];
      }
      bound(entry);break;
    }trim();
  }
 private:
  static constexpr size_t max_entry_bytes=2*1024*1024,max_bytes=8*1024*1024,max_entries=128;
  mutable std::mutex mutex_;bool enabled_{};uint64_t sequence_{};std::deque<Json> entries_;
  static int64_t now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
  static Json sanitize(const Json& value,size_t depth=0,bool input=false){
    if(depth>32)return "[depth limit]";
    if(value.is_object()){Json out=Json::object();for(auto it=value.begin();it!=value.end();++it){
      std::string key=it.key();std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
      if(key.find("token")!=std::string::npos||key.find("password")!=std::string::npos||key.find("secret")!=std::string::npos||key=="authorization"||key=="cookie"||(input&&(key=="text"||key=="value"||key=="values")))out[it.key()]="[redacted]";
      else if(key=="data")out[it.key()]="[binary data omitted; screenshot preview when available]";
      else out[it.key()]=sanitize(it.value(),depth+1,input);
    }return out;}
    if(value.is_array()){Json out=Json::array();for(const auto& item:value)out.push_back(sanitize(item,depth+1,input));return out;}
    return value;
  }
  static void bound(Json& entry){if(entry.dump().size()>max_entry_bytes){entry.erase("screenshot");entry["request"]="[entry size limit]";entry["response"]="[entry size limit]";entry["truncated"]=true;}}
  void trim(){size_t bytes=0;for(const auto& entry:entries_)bytes+=entry.dump().size();while(!entries_.empty()&&(entries_.size()>max_entries||bytes>max_bytes)){bytes-=entries_.front().dump().size();entries_.pop_front();}}
  void clear_locked(){entries_.clear();} // Keep sequence monotonic: late replies cannot refill a cleared trace.
};
}
