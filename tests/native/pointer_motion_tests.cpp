#include "xenon/pointer_motion.hpp"
#include "xenon/pointer_overlay.hpp"
#include <stdexcept>
#include <iostream>
using namespace xenon;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
  for(unsigned seed=0;seed<128;++seed){PointerMotion path({10,20},{610,320},seed);
    require(path.at(0).x==10&&path.at(0).y==20&&path.at(1).x==610&&path.at(1).y==320,"Path preserves exact endpoints");
    require(path.duration_ms()>=120&&path.duration_ms()<=360,"Movement duration is bounded");
    auto last=path.at(0);for(int i=1;i<=360;++i){const auto point=path.at(i/360.0);require(std::isfinite(point.x)&&std::isfinite(point.y),"Every sample is finite");
      require(std::hypot(point.x-last.x,point.y-last.y)<8,"Movement has no coordinate discontinuity");last=point;}
    require(std::hypot(path.at(.001).x-10,path.at(.001).y-20)<.01,"Movement starts at rest");
    require(std::hypot(path.at(.999).x-610,path.at(.999).y-320)<.01,"Movement ends at rest");
    PointerMotion again({10,20},{610,320},seed);require(path.at(.45).x==again.at(.45).x,"A fixed native test seed is deterministic");
  }
  PointerMotion still({3,7},{3,7},3);require(still.at(1).x==3&&still.at(1).y==7,"Zero-distance endpoints remain exact");
  PointerOverlayTransitions overlay;
  require(overlay.update(true,false,0,0,0)==PointerVisual::none,"Ownership alone sends no input transition");
  require(overlay.update(true,false,1,0,0)==PointerVisual::none,"Pointer event may arrive before broker pause");
  require(overlay.update(true,true,1,0,0)==PointerVisual::human,"Delayed pause still glides to the human event");
  require(overlay.update(true,true,2,0,0)==PointerVisual::human,"A human drag updates the parked position");
  require(overlay.update(true,false,2,0,0)==PointerVisual::park,"Resume only changes visual parking");
  require(overlay.update(true,true,2,0,0)==PointerVisual::none,"Keyboard-only pause leaves the pointer parked");
  require(overlay.update(true,false,2,0,0)==PointerVisual::park,"Keyboard pause resumes visually");
  require(overlay.update(true,false,2,1,0)==PointerVisual::synchronize,"Pointer synchronization is visually distinct from page input");
  require(overlay.update(true,false,2,1,1)==PointerVisual::follow,"Authorized movement follows actual page input");
  require(overlay.update(false,false,3,1,1)==PointerVisual::none,"Unavailable ownership cannot synthesize input");
  PointerOverlayTransitions immediate;
  require(immediate.update(true,true,1,0,0)==PointerVisual::human,"A first-frame pause still shows the qualifying human click");
  std::cout<<"Pointer paths passed: endpoints, smoothness, bounded timing and deterministic seeds\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
