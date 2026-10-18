#include "xenon/tab_render_pacing.hpp"
#include <iostream>
#include <stdexcept>
using namespace xenon;
void require_at(bool value,int line){if(!value)throw std::runtime_error("Tab render pacing regression at line "+std::to_string(line));}
#define require(value) require_at(value,__LINE__)
int main(){
  try{
    using namespace std::chrono_literals;
    const auto start=TabRenderPacing::Clock::time_point{}+1h;
    TabRenderPacing pacing;
    // Unknown and selected tabs are never throttled.
    require(pacing.rate("missing",start)==TabRenderPacing::kFullRate);
    pacing.set_covered("tab",false);require(pacing.rate("tab",start)==TabRenderPacing::kFullRate);
    // A covered tab with no agent activity paints at the idle rate.
    pacing.set_covered("tab",true);require(pacing.rate("tab",start)==TabRenderPacing::kIdleRate);
    // A running operation restores full rate for as long as it runs.
    pacing.agent_started("tab");require(pacing.rate("tab",start+1h)==TabRenderPacing::kFullRate);
    // The lease keeps full rate after completion, then lapses.
    const auto lease=pacing.agent_finished("tab",start+10s);
    require(lease==start+10s+TabRenderPacing::kAgentLease);
    require(pacing.rate("tab",lease-1ms)==TabRenderPacing::kFullRate);
    require(pacing.rate("tab",lease)==TabRenderPacing::kIdleRate);
    // Overlapping operations keep full rate until the last one finishes.
    pacing.agent_started("tab");pacing.agent_started("tab");pacing.agent_finished("tab",start+1min);
    require(pacing.rate("tab",start+10min)==TabRenderPacing::kFullRate);
    pacing.agent_finished("tab",start+2min);require(pacing.rate("tab",start+2min+TabRenderPacing::kAgentLease)==TabRenderPacing::kIdleRate);
    // A later operation extends the lease; an earlier completion never shortens it.
    pacing.agent_finished("tab",start+3min);pacing.agent_finished("tab",start+3min-10s);
    require(pacing.rate("tab",start+3min+TabRenderPacing::kAgentLease-1ms)==TabRenderPacing::kFullRate);
    // A completion without a matching start (a late reply) does not underflow.
    TabRenderPacing late;late.set_covered("late",true);late.agent_finished("late",start);
    require(late.rate("late",start+TabRenderPacing::kAgentLease)==TabRenderPacing::kIdleRate);
    // Selecting a covered tab restores full rate at once, even without a lease.
    pacing.set_covered("tab",false);require(pacing.rate("tab",start+1h)==TabRenderPacing::kFullRate);
    // Tabs are independent and forgotten when closed.
    pacing.set_covered("other",true);require(pacing.rate("tab",start+1h)==TabRenderPacing::kFullRate&&pacing.rate("other",start)==TabRenderPacing::kIdleRate);
    pacing.remove("other");require(pacing.rate("other",start)==TabRenderPacing::kFullRate);
    std::cout<<"Tab render pacing tests passed\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
