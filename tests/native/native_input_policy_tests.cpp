#include "xenon/native_input_policy.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition,const char* message) {
  if(!condition)throw std::runtime_error(message);
}
std::optional<xenon::NativeInputPolicy::Activity> one(std::vector<xenon::NativeInputPolicy::Activity> values){
  require(values.size()<=1,"Expected at most one affected native window");
  if(values.empty())return std::nullopt;
  return values.front();
}
void controls_then_stationary_browser() {
  xenon::NativeInputPolicy policy;policy.seed_pointer(100,100);
  require(policy.move(120,120).empty(),"Page hover does not pause an agent");
  require(policy.move(600,450).empty(),"Moving over Controls does not pause a browser tab");
  require(policy.move(630,480).empty(),"Moving onto Give control remains native UI input");
  // Give control changes broker metadata only. An activation, redraw, or new
  // cursor can make Windows emit another mouse move without physical movement.
  require(policy.move(630,480).empty(),"Stationary Chrome message after Controls must remain passive");
  require(policy.move(631,480).empty(),"Even genuine hover after Give control remains passive");
  require(!policy.press(0,1,true),"A native Controls click cannot start a page gesture");
  require(policy.move(650,490).empty(),"Dragging a native Controls window is not page input");
  require(!policy.tracking(),"Native Controls activity leaves no page gesture behind");
}
void queued_position_is_evidence() {
  xenon::ThreadPointerHistory history;history.seed(-900,250);
  // At processing time the live cursor may be on a different display/window.
  // The hook passes the original MSG.pt, never that unrelated live position.
  require(!history.move(-900,250),"A delayed stationary message must not turn later cursor position into old-tab input");
  require(history.move(2000,300),"Queued movement outside Chrome advances the shared screen-space baseline");
  require(!history.move(2000,300),"Moving/showing Chrome underneath the pointer is not pointer movement");
  require(history.move(2000,301),"Negative coordinates or another display must not disable real movement");
}
void drag_retains_original_page() {
  xenon::NativeInputPolicy policy;policy.seed_pointer(20,30);
  const auto down=policy.press(101,1,true,true);
  require(down&&down->window==101&&down->busy&&down->credential_input,"Page click begins a held, physically qualified gesture");
  require(policy.move(20,30).empty(),"A stationary message does not extend a held gesture");
  const auto moved=policy.move(25,35);
  require(moved.size()==1&&moved[0].window==101&&moved[0].busy&&!moved[0].credential_input,"Actual drag movement extends only its originating page pause");
  // The caller processes release even if its destination is Controls or another
  // Chrome window. No destination is accepted by release(): it cannot retarget.
  const auto up=one(policy.release(1));
  require(up&&up->window==101&&!up->busy&&!up->credential_input,"Cross-window release finishes the original page gesture");
  require(policy.move(30,40).empty(),"Hover after release returns to passive immediately");
  require(policy.release(1).empty(),"Repeated/untracked release does not create page activity");
}
void keyboard_is_renderer_qualified() {
  xenon::NativeInputPolicy policy;
  require(!policy.press(0,65,false,true),"Unqualified top-level/native UI typing cannot pause a page");
  auto key=policy.press(201,65,false,true,{16});
  require(key&&key->window==201&&key->busy&&key->credential_input,"Renderer-directed native key pauses its page");
  require(policy.move(10,20).empty()&&policy.move(11,20).empty(),"A held keyboard key does not turn hover into a drag");
  key=policy.press(202,65,false,true);
  require(key&&key->window==202&&key->substantive,"Repeating a held key pauses its actual new destination too");
  const auto released=policy.release(65);
  require(released.size()==2&&released[0].window==201&&released[0].busy&&released[1].window==202&&!released[1].busy,"One key release balances both windows and preserves the original modifier hold");
  require(!released[0].substantive&&!released[1].substantive,"Balancing a cross-window key cannot imply fresh input to either active tab");
  auto up=one(policy.release(16));
  require(up&&!up->busy&&!policy.tracking(),"Last key release finishes the clean input boundary");
}
void passive_modifiers_and_browser_focus(){
  xenon::NativeInputPolicy policy;
  for(const unsigned key:{0x10u,0x11u,0x12u,0x5bu,0x5cu,0xa0u,0xa1u,0xa2u,0xa3u,0xa4u,0xa5u}){
    require(!policy.press(701,key,false,true),"Standalone modifiers cannot start a page pause");
    require(policy.release(key).empty(),"An unqualified modifier release remains passive");
  }
  require(!policy.tracking(),"Window-switch modifier sequences leave no page gesture");
  for(const unsigned key:{static_cast<unsigned>('L'),static_cast<unsigned>('T'),static_cast<unsigned>('N'),static_cast<unsigned>('W'),0x09u})
    require(xenon::native_browser_focus_shortcut(key,true,false),"Known Control browser-focus shortcuts remain passive");
  for(const unsigned key:{0x09u,0x1bu,static_cast<unsigned>('D')})
    require(xenon::native_browser_focus_shortcut(key,false,true),"Known Alt window/browser-focus shortcuts remain passive");
  require(xenon::native_browser_focus_shortcut(0x75,false,false),"F6 focus change remains passive");
  require(!xenon::native_browser_focus_shortcut(0x74,false,false),"F5 reload still counts as page intent");
  require(!xenon::native_browser_focus_shortcut(0x25,false,true),"Alt+Left navigation still counts as page intent");
  require(!xenon::native_browser_focus_shortcut('A',true,false),"Control+A page editing remains actual page input");
  policy.press(701,'A',false,true,{0x11});
  require(one(policy.release('A'))->busy,"A qualified editing chord remains held until its modifier releases");
  require(!one(policy.release(0xa3))->busy&&!policy.tracking(),"Right Control release balances a canonical Control hold");
}
void wheel_and_independent_pages() {
  xenon::NativeInputPolicy policy;
  require(!policy.pulse(0),"Toolbar wheel does not become a page scroll");
  const auto wheel=policy.pulse(301);
  require(wheel&&wheel->window==301&&!wheel->busy&&!wheel->credential_input,"A page wheel event starts cooldown without a held gesture");
  policy.press(301,1,true);policy.press(302,65,false);
  const auto first=one(policy.release(1));
  require(first&&first->window==301&&!first->busy&&policy.busy(302),"A release on one page does not release another page's held key");
  require(policy.pulse(302)->busy,"Wheel/pulse cannot erase an existing held gesture");
}
void mixed_keyboard_pointer_boundary(){
  xenon::NativeInputPolicy policy;
  // Native adapters canonicalize both the legacy page child and renderer key
  // source to the same Chrome root after independently proving page routing.
  require(!policy.press(601,16,false),"Modifier before a click is initially passive");
  policy.press(601,1,true,false,{16});
  const auto mouse_up=one(policy.release(1));
  require(mouse_up&&mouse_up->window==601&&mouse_up->busy,"Mouse release must not unpause a page while a qualified keyboard modifier remains held");
  policy.press(601,1,true);
  const auto key_up=one(policy.release(16));
  require(key_up&&key_up->busy,"Key release must not unpause a page during its remaining drag");
  require(!one(policy.release(1))->busy&&!policy.tracking(),"Combined keyboard/mouse hold ends only after every part releases");
}
void outside_thread_release_and_ime() {
  xenon::NativeInputPolicy policy;
  policy.press(401,1,true);policy.press(401,65,false);policy.press(402,66,false);
  auto reconciled=policy.reconcile([](unsigned key){return key==65;},[](auto){return true;});
  require(reconciled.size()==2&&reconciled[0].window==401&&reconciled[0].busy&&reconciled[1].window==402&&!reconciled[1].busy,"Physical reconciliation releases only keys no longer held and preserves other gestures");
  reconciled=policy.reconcile([](unsigned){return true;},[](auto page){return page!=401;});
  require(reconciled.empty()&&!policy.tracking(),"Destroyed source windows are forgotten without notifying a reused destination");
  require(!policy.composition_start(0)&&!policy.composition_update(0),"Unqualified native IME input cannot start a page gesture");
  auto ime=policy.composition_start(501);
  require(ime&&ime->window==501&&ime->busy&&!ime->credential_input,"Qualified composition starts a clean-boundary hold");
  ime=policy.composition_update(0);
  require(ime&&ime->window==501&&ime->busy&&!ime->credential_input&&!ime->substantive,"Unqualified IME continuation keeps the old hold without crediting a new credential edit");
  reconciled=policy.reconcile([](unsigned){return false;},[](auto){return true;});
  require(reconciled.empty()&&policy.tracking(),"Released keys do not prematurely end active IME composition");
  policy.press(501,13,false);
  ime=one(policy.composition_end());
  require(ime&&ime->window==501&&ime->busy,"IME end preserves an independently held key");
  require(!one(policy.release(13))->busy&&!policy.tracking(),"IME plus final key release reaches a balanced boundary");
}
void tab_switch_keeps_sources_and_current_provenance(){
  using Routing=xenon::NativeTabGestureTargets;
  const auto first=Routing::route({}, {"tab-a"},true,true);
  require(first.affected==std::vector<std::string>{"tab-a"}&&first.retained==first.affected,"Initial held page input pins its source tab");
  const auto hover=Routing::route(first.retained,{"tab-b"},true,false);
  require(hover.affected==first.affected&&hover.credential_targets.empty(),"Continuation after switching tabs cannot credit input to either current or old tab");
  const auto typing=Routing::route(first.retained,{"tab-b"},true,true);
  require(typing.affected==std::vector<std::string>({"tab-a","tab-b"})&&typing.retained==typing.affected,"New typing in tab B pauses B while preserving held A");
  require(typing.credential_targets==std::vector<std::string>{"tab-b"},"Only the actual new target receives credential provenance");
  const auto release=Routing::route(typing.retained,{"tab-c"},false,false);
  require(release.affected==typing.affected&&release.retained.empty()&&release.credential_targets.empty(),"Final release balances A and B without pausing newly active C or qualifying credential edits");
  const auto untracked=Routing::route({}, {"tab-c"},false,false);
  require(untracked.affected.empty(),"An untracked completion never falls back to the current tab");
  xenon::NativeInputPolicy policy;
  policy.press(801,'A',false,true,{16});policy.press(802,'B',false,true,{16});
  require(one(policy.release('A'))->busy&&one(policy.release('B'))->busy,"A shared physical modifier keeps both qualified windows held");
  const auto modifier=policy.release(16);
  require(modifier.size()==2&&modifier[0].window==801&&modifier[1].window==802&&!modifier[0].busy&&!modifier[1].busy,"One shared modifier release balances every original window");
  require(!modifier[0].credential_input&&!modifier[1].credential_input&&!modifier[0].substantive&&!modifier[1].substantive,"Cross-window balancing releases carry no credential provenance");
}
}
int main() {
  try {
    controls_then_stationary_browser();queued_position_is_evidence();drag_retains_original_page();
    keyboard_is_renderer_qualified();passive_modifiers_and_browser_focus();wheel_and_independent_pages();mixed_keyboard_pointer_boundary();outside_thread_release_and_ime();tab_switch_keeps_sources_and_current_provenance();
    std::cout<<"Native page input policy tests passed: passive hover/Controls, held drag, renderer keys, independent pages, release reconciliation, and IME\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
