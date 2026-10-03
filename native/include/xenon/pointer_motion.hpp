#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <utility>

namespace xenon {
struct PointerPoint {double x{},y{};};
// A bounded path with zero endpoint velocity. Test callers supply a fixed seed;
// production seeds locally, never from website data or an MCP request.
class PointerMotion {
 public:
  PointerMotion(PointerPoint from,PointerPoint to,uint32_t seed):from_(from),to_(to) {
    std::mt19937 random(seed);std::uniform_real_distribution<double> unit(-1,1);
    const auto dx=to.x-from.x,dy=to.y-from.y,length=std::hypot(dx,dy);
    duration_=static_cast<int>(std::clamp(100+length*.35,120.0,360.0));
    const auto bend=unit(random)*std::min(24.0,length*.08);
    const auto nx=length?(-dy/length):0,ny=length?(dx/length):0;
    one_={from.x+dx*.30+nx*bend,from.y+dy*.30+ny*bend};
    two_={from.x+dx*.78+nx*bend*.6,from.y+dy*.78+ny*bend*.6};
    correction_={unit(random)*std::min(1.2,length*.02),unit(random)*std::min(1.2,length*.02)};
  }
  int duration_ms() const noexcept{return duration_;}
  PointerPoint at(double fraction) const noexcept {
    const double f=std::clamp(fraction,0.0,1.0);if(f==0)return from_;if(f==1)return to_;
    const auto t=f*f*f*(10+f*(-15+6*f)),u=1-t;
    PointerPoint point{u*u*u*from_.x+3*u*u*t*one_.x+3*u*t*t*two_.x+t*t*t*to_.x,
      u*u*u*from_.y+3*u*u*t*one_.y+3*u*t*t*two_.y+t*t*t*to_.y};
    const double correction=f>.82?std::sin((f-.82)/.18*3.141592653589793)*(1-f)/.18:0;
    point.x+=correction_.x*correction;point.y+=correction_.y*correction;return point;
  }
 private:
  PointerPoint from_,to_,one_,two_,correction_;int duration_{};
};
}
