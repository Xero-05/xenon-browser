#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace xenon {
// Windows virtual-key values, kept independent of windows.h for pure tests.
inline unsigned canonical_native_key(unsigned key) noexcept {
  if(key==0xa0||key==0xa1)return 0x10; // Left/right Shift.
  if(key==0xa2||key==0xa3)return 0x11; // Left/right Control.
  if(key==0xa4||key==0xa5)return 0x12; // Left/right Alt.
  return key;
}
inline bool native_modifier_key(unsigned key) noexcept {
  key=canonical_native_key(key);
  return key==0x10||key==0x11||key==0x12||key==0x5b||key==0x5c;
}
inline bool native_browser_focus_shortcut(unsigned key,bool control,bool alt) noexcept {
  return key==0x75 || // F6: focus browser UI.
    (control&&(key=='L'||key=='T'||key=='N'||key=='W'||key==0x09)) ||
    (alt&&(key==0x09||key==0x1b||key=='D'));
}

// Use queued MSG.pt screen coordinates for every UI-thread mouse move, including
// moves over native Controls. A newly shown window may receive a stationary
// WM_MOUSEMOVE; the current GetCursorPos is not evidence about an older message.
class ThreadPointerHistory {
 public:
  void seed(long x,long y) noexcept {x_=x;y_=y;initialized_=true;}
  bool move(long x,long y) noexcept {
    const bool changed=initialized_&&(x!=x_||y!=y_);
    seed(x,y);return changed;
  }
 private:
  long x_{},y_{};
  bool initialized_{};
};

// Native page classification is supplied by the caller. This class never turns
// focus, activation, hover, or input beginning on Controls into page activity.
// Retain the page window where a gesture began so its release can arrive on a
// different native window without pausing that unrelated destination.
class NativeInputPolicy {
 public:
  using Window=std::uintptr_t;
  struct Activity {Window window{};bool busy{};bool credential_input{};bool substantive{};};
  void seed_pointer(long x,long y) noexcept {pointer_.seed(x,y);}
  bool tracking() const noexcept {return !held_.empty()||!compositions_.empty();}
  bool busy(Window page) const {
    if(compositions_.contains(page))return true;
    for(const auto& [key,hold]:held_)if(hold.windows.contains(page))return true;
    return false;
  }
  std::optional<Activity> press(Window page,unsigned key,bool pointer,bool credential=false,
                                const std::vector<unsigned>& modifiers={}) {
    key=canonical_native_key(key);
    // A modifier by itself may precede a window switch or browser accelerator.
    // It joins a held page gesture only when an actual page key/click follows.
    if(!page||(!pointer&&native_modifier_key(key)))return std::nullopt;
    for(auto modifier:modifiers)if(native_modifier_key(modifier))
      held_[canonical_native_key(modifier)].windows.insert(page);
    // A held key can repeat into a different window. Keep every original hold
    // for completion, but route this new input to its actual current destination.
    auto& hold=held_[key];hold.windows.insert(page);hold.pointer=pointer;
    return Activity{page,true,credential,true};
  }
  std::vector<Activity> release(unsigned key) {
    key=canonical_native_key(key);
    const auto entry=held_.find(key);if(entry==held_.end())return {};
    const auto pages=entry->second.windows;held_.erase(entry);
    std::vector<Activity> result;
    for(const auto page:pages)result.push_back({page,busy(page),false,false});
    return result;
  }
  std::optional<Activity> pulse(Window page,bool credential=false) const {
    if(!page)return std::nullopt;
    return Activity{page,busy(page),credential,true};
  }
  std::vector<Activity> move(long x,long y) {
    if(!pointer_.move(x,y))return {};
    std::set<Window> pages;
    for(const auto& [key,hold]:held_)if(hold.pointer)pages.insert(hold.windows.begin(),hold.windows.end());
    std::vector<Activity> out;
    for(const auto page:pages)out.push_back({page,true,false});
    return out;
  }
  std::optional<Activity> composition_start(Window page) {
    if(!page)return std::nullopt;
    compositions_.insert(page);
    return Activity{page,true,false,true};
  }
  std::optional<Activity> composition_update(Window page) {
    if(page){compositions_.insert(page);return Activity{page,true,true,true};}
    // Unknown routing keeps an existing composition held, without attributing a
    // fresh credential edit to whichever tab later became active in that window.
    if(compositions_.size()==1)return Activity{*compositions_.begin(),true,false,false};
    return std::nullopt;
  }
  std::vector<Activity> composition_end() {
    const auto pages=std::move(compositions_);compositions_.clear();
    std::vector<Activity> result;
    for(const auto page:pages)result.push_back({page,busy(page),false,false});
    return result;
  }
  // A button/key release outside this UI thread produces no queued message here.
  // A bounded timer may reconcile physical state without reporting fresh input.
  template<class Down,class Exists>
  std::vector<Activity> reconcile(Down down,Exists exists) {
    std::set<Window> changed;
    for(auto entry=held_.begin();entry!=held_.end();) {
      if(!down(entry->first)){
        changed.insert(entry->second.windows.begin(),entry->second.windows.end());entry=held_.erase(entry);
      }else{
        for(auto page=entry->second.windows.begin();page!=entry->second.windows.end();)
          if(!exists(*page)){changed.insert(*page);page=entry->second.windows.erase(page);}else ++page;
        if(entry->second.windows.empty())entry=held_.erase(entry);else ++entry;
      }
    }
    for(auto page=compositions_.begin();page!=compositions_.end();)
      if(!exists(*page)){changed.insert(*page);page=compositions_.erase(page);}else ++page;
    std::vector<Activity> out;
    for(const auto page:changed)if(exists(page))out.push_back({page,busy(page),false});
    return out;
  }
 private:
  struct Hold {std::set<Window> windows;bool pointer{};};
  ThreadPointerHistory pointer_;
  std::map<unsigned,Hold> held_;
  std::set<Window> compositions_;
};

// Used by the engine after resolving native root windows to live tab IDs. Fresh
// input includes the current tab; continuation/release never retargets a held
// gesture merely because a tab switch changed native focus.
struct NativeTabGestureTargets {
  std::vector<std::string> affected,retained,credential_targets;
  static NativeTabGestureTargets route(const std::vector<std::string>& previous,
                                      const std::vector<std::string>& current,
                                      bool busy,bool substantive){
    NativeTabGestureTargets result;result.affected=previous;
    if(substantive)for(const auto& id:current){
      if(std::find(result.affected.begin(),result.affected.end(),id)==result.affected.end())result.affected.push_back(id);
      if(std::find(result.credential_targets.begin(),result.credential_targets.end(),id)==result.credential_targets.end())result.credential_targets.push_back(id);
    }
    if(busy)result.retained=result.affected;
    return result;
  }
};
}
