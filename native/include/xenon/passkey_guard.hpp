#pragma once
#include "xenon/contracts.hpp"
#include <chrono>
#include <cstddef>
#include <map>
#include <optional>
#include <string>

namespace xenon {
// Fixed main-world script. Chromium's own WebAuthn stack and the Windows
// authenticator still perform every ceremony; this wrapper only asks the
// native gate before forwarding a publicKey request. It sends one bounded
// JSON control message and never reads or returns options, credentials,
// challenges, user handles or page strings. A page can see the wrapper; the
// native gate, not this script, decides. Without the native binding every
// publicKey request is refused.
inline constexpr const char* passkey_guard_script = R"PASSKEYJS((()=>{
  const g=globalThis,C=g.CredentialsContainer,send=g.__xenon_passkey;
  try{delete g.__xenon_passkey}catch{}
  if(typeof C!=='function'||!C.prototype)return;
  const proto=C.prototype,mark='__xenonPasskeyGuard';
  if(Object.prototype.hasOwnProperty.call(proto,mark))return;
  const createDescriptor=Object.getOwnPropertyDescriptor(proto,'create'),getDescriptor=Object.getOwnPropertyDescriptor(proto,'get');
  if(!createDescriptor||!getDescriptor||typeof createDescriptor.value!=='function'||typeof getDescriptor.value!=='function')return;
  const bind=f=>Function.prototype.call.bind(f),apply=Reflect.apply,stringify=JSON.stringify,P=Promise,Err=g.DOMException;
  const mapGet=bind(Map.prototype.get),mapSet=bind(Map.prototype.set),mapDelete=bind(Map.prototype.delete),then=bind(P.prototype.then);
  const includes=bind(Array.prototype.includes),settledPromise=P.resolve.bind(P);
  const delay=g.setTimeout,define=Object.defineProperty,random=g.crypto&&g.crypto.getRandomValues?bind(g.crypto.getRandomValues):null;
  const modes=['optional','required','silent','conditional','immediate'];
  const pending=new Map();let next=0,prefix='';
  if(random){const bytes=random(g.crypto,new Uint8Array(12));for(const b of bytes)prefix+=(b<16?'0':'')+b.toString(16)}
  else prefix=String(Date.now());
  const refusal=()=>new Err('Xenon allows passkey requests only from the person using the browser.','NotAllowedError');
  const notify=(type,id,extra)=>{try{send(stringify({type,id,...extra}));return true}catch{return false}};
  define(g,'__xenonPasskeyDecision',{value:(id,allowed)=>{const entry=mapGet(pending,id);if(!entry)return;mapDelete(pending,id);entry(allowed===true)},writable:false,configurable:false,enumerable:false});
  const wrap=(operation,original)=>({[operation](options){
    if(options===null||typeof options!=='object'||options.publicKey===undefined)return apply(original,this,arguments);
    const self=this,args=arguments,mediation=includes(modes,options.mediation)?options.mediation:'optional',id=prefix+'-'+(++next);
    return new P((resolve,reject)=>{
      mapSet(pending,id,allowed=>{
        if(!allowed){reject(refusal());return}
        let result;
        try{result=apply(original,self,args)}catch(error){notify('settled',id);reject(error);return}
        then(settledPromise(result),value=>{notify('settled',id);resolve(value)},error=>{notify('settled',id);reject(error)});
      });
      if(!notify('request',id,{operation,mediation})){mapDelete(pending,id);reject(refusal());return}
      // Fail closed if the native gate cannot answer.
      delay(()=>{const entry=mapGet(pending,id);if(entry){mapDelete(pending,id);entry(false)}},10000);
    });
  }})[operation];
  define(proto,'create',{...createDescriptor,value:wrap('create',createDescriptor.value)});
  define(proto,'get',{...getDescriptor,value:wrap('get',getDescriptor.value)});
  define(proto,mark,{value:true,writable:false,configurable:false,enumerable:false});
})())PASSKEYJS";

// One bounded control message from the main-world wrapper. IDs are opaque
// tokens the wrapper generated; nothing else is accepted.
struct PasskeyMessage {
  enum class Type { request, settled };
  Type type = Type::request;
  std::string id;
  bool create = false;
  bool conditional = false;
};
inline std::optional<PasskeyMessage> parse_passkey_message(const std::string& payload) {
  if(payload.empty()||payload.size()>256)return std::nullopt;
  const auto value=Json::parse(payload,nullptr,false);
  if(!value.is_object())return std::nullopt;
  const auto text=[&](const char* key)->std::optional<std::string>{
    const auto i=value.find(key);if(i==value.end()||!i->is_string())return std::nullopt;return i->get<std::string>();
  };
  const auto type=text("type"),id=text("id");
  if(!type||!id||id->empty()||id->size()>80)return std::nullopt;
  for(const char c:*id)if(!((c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='-'))return std::nullopt;
  PasskeyMessage message;message.id=*id;
  if(*type=="settled"){
    if(value.size()!=2)return std::nullopt;
    message.type=PasskeyMessage::Type::settled;return message;
  }
  if(*type!="request"||value.size()!=4)return std::nullopt;
  const auto operation=text("operation"),mediation=text("mediation");
  if(!operation||(*operation!="create"&&*operation!="get")||!mediation)return std::nullopt;
  if(*mediation!="optional"&&*mediation!="required"&&*mediation!="silent"&&*mediation!="conditional"&&*mediation!="immediate")return std::nullopt;
  message.create=*operation=="create";message.conditional=*mediation=="conditional";
  return message;
}

// Per-tab passkey policy. Tabs the human controls behave as in Chromium.
// In any other tab a request reaches the authenticator only after the
// human's own physical input in that tab, with no agent page action since.
// Conditional (autofill) requests have no gesture of their own, so they are
// refused unless the human controls the tab. Ceremonies the gate forwarded
// keep agent input paused until the page reports that they settled.
class PasskeyGate {
 public:
  using Clock = std::chrono::steady_clock;
  static constexpr auto human_window = std::chrono::seconds(15);
  static constexpr auto ceremony_limit = std::chrono::minutes(10);
  static constexpr size_t max_pending = 8;

  void human_gesture(Clock::time_point now) { human_at_ = now; agent_since_ = false; }
  void agent_action() { agent_since_ = true; }
  bool allow(bool human_controlled, bool conditional, Clock::time_point now) const {
    if(human_controlled)return true;
    if(conditional||agent_since_||!human_at_||now<*human_at_)return false;
    return now-*human_at_<=human_window;
  }
  // Records a forwarded modal ceremony. Refuse it when the bound is reached.
  bool begin(const std::string& key, Clock::time_point now) {
    if(key.empty()||pending_.contains(key))return false;
    if(pending_.size()>=max_pending)return false;
    pending_.emplace(key,now);return true;
  }
  // Each of these returns true only when it ended the last pending ceremony.
  bool settle(const std::string& key) { return pending_.erase(key)&&pending_.empty(); }
  bool drop_prefix(const std::string& prefix) {
    bool removed=false;
    for(auto i=pending_.begin();i!=pending_.end();)if(i->first.starts_with(prefix)){i=pending_.erase(i);removed=true;}else ++i;
    return removed&&pending_.empty();
  }
  bool expire(Clock::time_point now) {
    bool removed=false;
    for(auto i=pending_.begin();i!=pending_.end();)if(now>=i->second&&now-i->second>=ceremony_limit){i=pending_.erase(i);removed=true;}else ++i;
    return removed&&pending_.empty();
  }
  bool pending() const { return !pending_.empty(); }

 private:
  std::optional<Clock::time_point> human_at_;
  bool agent_since_ = true;
  std::map<std::string, Clock::time_point> pending_;
};
} // namespace xenon
