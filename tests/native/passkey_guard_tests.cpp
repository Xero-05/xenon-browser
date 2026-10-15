#include "xenon/passkey_guard.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using xenon::PasskeyGate;
using xenon::PasskeyMessage;
using xenon::parse_passkey_message;
void require(bool condition,const char* message) {
  if(!condition)throw std::runtime_error(message);
}
void parses_only_bounded_control_messages() {
  const auto request=parse_passkey_message(R"({"type":"request","id":"0a1b2c-1","operation":"get","mediation":"optional"})");
  require(request&&request->type==PasskeyMessage::Type::request&&request->id=="0a1b2c-1"&&!request->create&&!request->conditional,"Modal get request parses");
  const auto conditional=parse_passkey_message(R"({"type":"request","id":"a-2","operation":"get","mediation":"conditional"})");
  require(conditional&&conditional->conditional,"Conditional mediation is recognized");
  const auto create=parse_passkey_message(R"({"type":"request","id":"a-3","operation":"create","mediation":"required"})");
  require(create&&create->create&&!create->conditional,"Create request parses");
  const auto settled=parse_passkey_message(R"({"type":"settled","id":"a-3"})");
  require(settled&&settled->type==PasskeyMessage::Type::settled&&settled->id=="a-3","Settled notice parses");
  require(!parse_passkey_message(""),"Empty payload is rejected");
  require(!parse_passkey_message("not json"),"Malformed JSON is rejected");
  require(!parse_passkey_message(R"(["request"])"),"Non-object payload is rejected");
  require(!parse_passkey_message(R"({"type":"request","id":"a-4","operation":"get","mediation":"optional","challenge":"x"})"),"Extra request fields are rejected");
  require(!parse_passkey_message(R"({"type":"settled","id":"a-4","value":1})"),"Extra settled fields are rejected");
  require(!parse_passkey_message(R"({"type":"request","id":"a 5","operation":"get","mediation":"optional"})"),"IDs outside the token alphabet are rejected");
  require(!parse_passkey_message(R"({"type":"request","id":"\"),alert(1)//","operation":"get","mediation":"optional"})"),"IDs cannot carry script syntax");
  require(!parse_passkey_message(R"({"type":"request","id":"a-6","operation":"store","mediation":"optional"})"),"Unknown operations are rejected");
  require(!parse_passkey_message(R"({"type":"request","id":"a-7","operation":"get","mediation":"auto"})"),"Unknown mediation is rejected");
  require(!parse_passkey_message(R"({"type":"request","id":7,"operation":"get","mediation":"optional"})"),"Non-string IDs are rejected");
  require(!parse_passkey_message(R"({"type":"request","id":"","operation":"get","mediation":"optional"})"),"Empty IDs are rejected");
  require(!parse_passkey_message(R"({"type":"request","id":")"+std::string(81,'a')+R"(","operation":"get","mediation":"optional"})"),"IDs over 80 bytes are rejected");
  require(!parse_passkey_message(std::string(257,' ')),"Payloads over 256 bytes are rejected");
}
void human_controlled_tabs_are_unchanged() {
  PasskeyGate gate;const auto now=PasskeyGate::Clock::now();
  require(gate.allow(true,false,now),"Human-controlled modal request needs no recent gesture");
  require(gate.allow(true,true,now),"Human-controlled passkey autofill stays available");
  gate.agent_action();
  require(gate.allow(true,false,now),"Agent history does not restrict a tab the human now controls");
}
void agent_tabs_require_the_humans_own_gesture() {
  PasskeyGate gate;const auto start=PasskeyGate::Clock::now();
  require(!gate.allow(false,false,start),"Without human input an agent tab cannot start a ceremony");
  gate.human_gesture(start);
  require(gate.allow(false,false,start+std::chrono::seconds(3)),"The human's own click can start a ceremony without a handoff");
  require(gate.allow(false,false,start+PasskeyGate::human_window),"The window includes its bound");
  require(!gate.allow(false,false,start+PasskeyGate::human_window+std::chrono::milliseconds(1)),"An old gesture no longer authorizes a ceremony");
  require(!gate.allow(false,false,start-std::chrono::seconds(1)),"A clock earlier than the gesture is refused");
  require(!gate.allow(false,true,start+std::chrono::seconds(1)),"Passkey autofill is refused in agent tabs even after human input");
  gate.agent_action();
  require(!gate.allow(false,false,start+std::chrono::seconds(1)),"An agent page action after the gesture refuses later requests");
  gate.human_gesture(start+std::chrono::seconds(2));
  require(gate.allow(false,false,start+std::chrono::seconds(3)),"A new human gesture restores the window");
}
void forwarded_ceremonies_hold_the_pause() {
  PasskeyGate gate;const auto now=PasskeyGate::Clock::now();
  require(!gate.pending(),"No ceremony is pending initially");
  require(gate.begin("|4|a-1",now),"First ceremony is tracked");
  require(!gate.begin("|4|a-1",now),"A duplicate key is refused");
  require(gate.begin("s1|9|b-1",now),"Frame-session ceremonies are tracked separately");
  require(!gate.settle("|4|unknown"),"Unknown settled notices change nothing");
  require(!gate.settle("|4|a-1")&&gate.pending(),"Settling one of two keeps the pause");
  require(gate.settle("s1|9|b-1")&&!gate.pending(),"Settling the last ends the pause");
  require(!gate.begin("",now),"Empty keys are refused");
  for(size_t i=0;i<PasskeyGate::max_pending;++i)require(gate.begin("|4|c-"+std::to_string(i),now),"Ceremonies up to the bound are tracked");
  require(!gate.begin("|4|c-over",now),"The concurrent ceremony bound refuses more");
  require(!gate.drop_prefix("|5|"),"Another context's prefix drops nothing");
  require(gate.drop_prefix("|4|")&&!gate.pending(),"Destroying the context ends its ceremonies");
  require(gate.begin("s2|1|d-1",now)&&gate.begin("s3|1|d-2",now+std::chrono::minutes(5)),"Ceremonies with different start times");
  require(!gate.expire(now+PasskeyGate::ceremony_limit-std::chrono::seconds(1)),"Nothing expires before the bound");
  require(!gate.expire(now+PasskeyGate::ceremony_limit)&&gate.pending(),"Only the older ceremony expires at its bound");
  require(gate.expire(now+std::chrono::minutes(5)+PasskeyGate::ceremony_limit)&&!gate.pending(),"Expiring the last ceremony ends the pause");
}
}

int main() {
  try {
    parses_only_bounded_control_messages();
    human_controlled_tabs_are_unchanged();
    agent_tabs_require_the_humans_own_gesture();
    forwarded_ceremonies_hold_the_pause();
  } catch(const std::exception& error) {
    std::cerr<<error.what()<<"\n";return 1;
  }
  std::cout<<"passkey guard tests passed\n";
}
