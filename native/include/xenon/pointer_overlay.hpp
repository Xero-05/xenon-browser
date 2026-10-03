#pragma once
#include <cstdint>
namespace xenon {
enum class PointerVisual {none,human,park,synchronize,follow};
// Only selects visual transitions. This class has no page-input capability.
struct PointerOverlayTransitions {
  bool agent{},paused{};
  uint64_t consumed_human{},sync{},input{};
  PointerVisual update(bool available,bool human_paused,uint64_t human_revision,uint64_t sync_revision,uint64_t input_revision){
    if(!available||(!agent&&!human_paused))consumed_human=human_revision;
    PointerVisual transition=PointerVisual::none;
    if(available&&human_paused&&human_revision!=consumed_human){transition=PointerVisual::human;consumed_human=human_revision;}
    else if(available&&!human_paused&&paused)transition=PointerVisual::park;
    else if(available&&!human_paused&&sync_revision!=sync)transition=PointerVisual::synchronize;
    else if(available&&!human_paused&&input_revision!=input)transition=PointerVisual::follow;
    agent=available;paused=human_paused;sync=sync_revision;input=input_revision;return transition;
  }
};
}
