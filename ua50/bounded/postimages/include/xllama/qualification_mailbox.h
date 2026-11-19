#pragma once
#include "dev_run_config.h"
#include "ingress_reason_v2.h"
#include <functional>
#include <limits>
#include <atomic>

namespace xllama::qualification {
inline constexpr size_t prepare_cap=4096, command_cap=4096, profile_cap=16384;
inline constexpr size_t receipt_cap=16384, retained_charge=4u<<20;
inline constexpr uint64_t control_output_charge=128u<<10;
inline constexpr uint64_t ready_wait_ms=120000, manual_wait_ms=120000;
inline constexpr uint64_t clock_slack_ms=2000, poll_ms=500, max_polls=240;
inline constexpr char prepare_leaf[]="ua80-qualification-v1.prepare.json";
enum class Stage { none, identity, proof, cpu_dev, generation };
enum class Mode { manual, diagnostic };
enum class Phase { manual, ready, claiming, running, returned, unknown, expired, abandoned };
enum PreworkMask:uint32_t { absent_prepare=1u<<0,invalid_prepare=1u<<1,foreign_package=1u<<2,profile_refused=1u<<3,quota_refused=1u<<4,retained_namespace=1u<<5,clock_unavailable=1u<<6,wait_elapsed=1u<<7,poll_cap=1u<<8,io_unknown=1u<<9,clock_drift=1u<<10,explicit_manual=1u<<11,epoch_expired=1u<<12,receipt_unavailable=1u<<13,worker_setup=1u<<14 };
inline const char* name(Stage s){switch(s){case Stage::none:return "none";case Stage::identity:return "identity";case Stage::proof:return "proof";case Stage::cpu_dev:return "cpu_dev";case Stage::generation:return "generation";}return "invalid";}
inline const char* name(Phase p){switch(p){case Phase::manual:return "MANUAL";case Phase::ready:return "READY";case Phase::claiming:return "CLAIMING";case Phase::running:return "RUNNING";case Phase::returned:return "WORK_RETURNED_NOT_ACCEPTED";case Phase::unknown:return "UNKNOWN_NO_RETRY";case Phase::expired:return "READY_EXPIRED";case Phase::abandoned:return "ABANDONED_NO_ACCEPTANCE";}return "UNKNOWN";}
inline std::string text(const dev_run::Json& j,const char* key,size_t cap){const auto&v=j.at(key);dev_run::need(v.type==dev_run::Json::Type::string&&v.text.size()<=cap,"mailbox string");return v.text;}
inline std::string hash(const dev_run::Json&j,const char*k){auto s=text(j,k,64);dev_run::need(dev_run::hex(s,64),"mailbox hash");return s;}
inline bool selection_id(const std::string&s){return !s.empty()&&s.size()<=64&&std::all_of(s.begin(),s.end(),[](char c){return(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-';});}
inline std::string bounded_detail(const std::string&s){if(!first_answer::utf8(s))return "diagnostic detail unavailable: invalid UTF8";auto out=s.substr(0,512);while(!out.empty()&&!first_answer::utf8(out))out.pop_back();return out;}
inline Stage stage(const std::string&s){if(s=="none")return Stage::none;if(s=="identity")return Stage::identity;if(s=="proof")return Stage::proof;if(s=="cpu_dev")return Stage::cpu_dev;if(s=="generation")return Stage::generation;throw std::runtime_error("unknown mailbox stage");}
inline std::string leaf(const std::string&run,const char*suffix){dev_run::need(dev_run::run_id(run),"mailbox run");return "ua80-qualification-v1."+run+"."+suffix;}
struct Prepare {
 Mode mode=Mode::manual;Stage work=Stage::none;
 std::string run,selection,package,artifact_sha,profile_sha,prerequisite_sha,selection_sha;
};
inline Prepare prepare(const std::string&raw){
 const auto j=dev_run::Parser(raw).parse(prepare_cap);
 j.keys({"schema","mode","stage","run_id","selection_id","full_package_name","signed_artifact_sha256","profile_sha256","prerequisite_receipt_sha256","selection_receipt_sha256","issuer"});
 j.at("schema").eq("xllama-qualification-prepare-v1");j.at("issuer").eq("nominated-ops-external-authority");
 Prepare p;const auto mode=text(j,"mode",16);dev_run::need(mode=="manual"||mode=="diagnostic","mailbox mode");p.mode=mode=="manual"?Mode::manual:Mode::diagnostic;
 p.work=stage(text(j,"stage",16));p.run=text(j,"run_id",32);dev_run::need(dev_run::run_id(p.run),"prepare run");
 p.selection=text(j,"selection_id",64);dev_run::need(selection_id(p.selection),"prepare selection");
 p.package=text(j,"full_package_name",160);dev_run::need(p.package.size()>=32,"prepare package");
 p.artifact_sha=hash(j,"signed_artifact_sha256");p.profile_sha=hash(j,"profile_sha256");p.prerequisite_sha=hash(j,"prerequisite_receipt_sha256");p.selection_sha=hash(j,"selection_receipt_sha256");
 dev_run::need((p.mode==Mode::manual)==(p.work==Stage::none),"mode/stage mismatch");return p;
}
struct Command {
 std::string run,selection,package,instance,nonce,ready_sha,prepare_sha,config_sha,permit_sha,selection_sha,artifact_sha;
 Stage work=Stage::none;uint64_t phase=0,issued_utc=0,expires=0;
};
inline Command command(const std::string&raw){
 const auto j=dev_run::Parser(raw).parse(command_cap);
 j.keys({"schema","stage","run_id","selection_id","full_package_name","signed_artifact_sha256","instance_id","nonce_sha256","ready_sha256","prepare_sha256","config_sha256","permit_sha256","selection_receipt_sha256","phase","issued_at_epoch_ms","expires_at_epoch_ms"});
 j.at("schema").eq("xllama-qualification-command-v1");Command c;c.work=stage(text(j,"stage",16));
 c.run=text(j,"run_id",32);dev_run::need(dev_run::run_id(c.run),"command run");c.selection=text(j,"selection_id",64);dev_run::need(selection_id(c.selection),"command selection");
 c.package=text(j,"full_package_name",160);c.instance=text(j,"instance_id",32);dev_run::need(dev_run::run_id(c.instance),"command instance");
 c.artifact_sha=hash(j,"signed_artifact_sha256");c.nonce=hash(j,"nonce_sha256");c.ready_sha=hash(j,"ready_sha256");c.prepare_sha=hash(j,"prepare_sha256");c.config_sha=hash(j,"config_sha256");c.permit_sha=hash(j,"permit_sha256");c.selection_sha=hash(j,"selection_receipt_sha256");
 c.phase=j.at("phase").range(1,1);c.issued_utc=j.at("issued_at_epoch_ms").range(1,INT64_MAX);c.expires=j.at("expires_at_epoch_ms").range(1,INT64_MAX);return c;
}
struct Clock {uint64_t utc=0,steady=0;};
struct Anchor {
 Clock first;uint64_t expiry=0;
 bool uncertain(Clock n)const noexcept{
  if(!first.utc||!n.utc||!first.steady||!n.steady||n.steady<first.steady||n.utc<first.utc)return true;
  const auto elapsed=n.steady-first.steady,wall=n.utc-first.utc;
  return elapsed>wall?elapsed-wall>clock_slack_ms:wall-elapsed>clock_slack_ms;
 }
 bool valid(Clock n)const noexcept{return !uncertain(n)&&n.utc<expiry;}
};
struct Session {
 Prepare selected;std::string actual_package,requested_package,instance,nonce,prepare_sha,ready_sha;Anchor clock;
 uint32_t prework_mask=0;std::string prework_detail;Clock last_clock;bool clock_sampled=false;
 struct Quota {uint64_t first=0,current=0,usage=0,bracket_first=0;uint32_t available=0,level=0;} quota;
 Phase state=Phase::manual;uint64_t polls=0,dispatches=0;bool claim_attempted=false;
 void initialize(const Prepare&p,const std::string&actual,const std::string&id,const std::string&n,const std::string&sha,Clock now){
  dev_run::need(state==Phase::manual&&instance.empty(),"instance already latched");
  dev_run::need(dev_run::run_id(id)&&dev_run::hex(n,64)&&dev_run::hex(sha,64),"native instance entropy shape");
  instance=id;nonce=n;prepare_sha=sha;actual_package=actual;requested_package=p.package;selected=p;clock.first=now;last_clock=now;clock_sampled=true;
  if(p.package!=actual){note(foreign_package,"requested PFN differs from actual native PFN");selected=Prepare{};selected.run=id;selected.package=actual;state=Phase::manual;return;}
  if(p.mode!=Mode::diagnostic){note(explicit_manual,"explicit immutable manual mode");state=Phase::manual;return;}
  if(!now.utc||!now.steady||now.utc>uint64_t(INT64_MAX)-1800000)note(clock_unavailable,"native UTC unavailable/overflow");
  dev_run::need(now.utc&&now.steady&&now.utc<=uint64_t(INT64_MAX)-1800000,"native UTC unavailable/overflow");
  clock.expiry=now.utc+(p.work==Stage::identity?300000:1800000);state=Phase::ready;
 }
 void note(uint32_t mask,const std::string&detail){prework_mask|=mask;prework_detail=bounded_detail(detail);}
 void manual_fallback(uint32_t mask,const std::string&detail){
  dev_run::need(!claim_attempted&&dispatches==0,"cannot downgrade work to manual");note(mask,detail);
  selected=Prepare{};selected.run=instance;selected.package=actual_package;clock.expiry=0;state=Phase::manual;
 }
 bool poll(Clock now){if(state!=Phase::ready)return false;last_clock=now;uint32_t mask=0;
  if(++polls>max_polls)mask|=poll_cap;
  if(!clock.first.utc||!now.utc||!clock.first.steady||!now.steady)mask|=clock_unavailable;else if(clock.uncertain(now))mask|=clock_drift;
  if(now.utc&&now.utc>=clock.expiry)mask|=epoch_expired;
  if(now.steady>=clock.first.steady&&now.steady-clock.first.steady>=ready_wait_ms)mask|=wait_elapsed;
  if(mask){note(mask,"readiness admission stopped; see exact predicate mask");state=Phase::expired;return false;}return true;}
 void validate(const Command&c,Clock now)const{
  dev_run::need(state==Phase::ready&&!claim_attempted&&clock.valid(now)&&now.steady-clock.first.steady<ready_wait_ms,"manual/spent/clock refused");
  dev_run::need(c.run==selected.run&&c.selection==selected.selection&&c.package==selected.package&&c.artifact_sha==selected.artifact_sha&&c.instance==instance&&c.nonce==nonce&&c.ready_sha==ready_sha&&c.prepare_sha==prepare_sha&&c.work==selected.work&&c.selection_sha==selected.selection_sha&&c.phase==1&&c.issued_utc==clock.first.utc&&c.expires==clock.expiry,"command binding");
 }
 // The production caller and host fixtures use this same irreversible boundary.
 // A failed durable claim is UNKNOWN and cannot be retried in this instance.
 template<class Claim,class Dispatch>void execute(const Command&c,Clock now,Claim claim,Dispatch dispatch){
  validate(c,now);claim_attempted=true;state=Phase::claiming;
  try{claim();state=Phase::running;++dispatches;dispatch();state=Phase::returned;}
  catch(...){state=Phase::unknown;throw;}
 }
 void refuse()noexcept{if(state==Phase::ready||state==Phase::claiming||state==Phase::running||state==Phase::returned)state=Phase::unknown;}
 void abandon()noexcept{if(state!=Phase::running&&state!=Phase::claiming)state=Phase::abandoned;}
};
template<class Init,class Work,class Failure>bool worker_boundary(Init init,Work work,Failure failure)noexcept{
 try{init();work();return true;}
 catch(const std::exception&e){try{failure(e.what());}catch(...){}return false;}
 catch(...){try{failure("unknown worker failure");}catch(...){}return false;}
}
struct RunGuard {
 std::atomic<bool> active{false},abort{false};Anchor clock;ingress_reason_v2::Recorder reasons;
 void start(const Anchor&a){dev_run::need(!active.load(),"qualification work owner busy");dev_run::need(!abort.load(),"qualification instance interrupted");clock=a;active.store(true,std::memory_order_release);}
 uint32_t mask(Clock n)const noexcept{if(!active.load(std::memory_order_acquire))return 0;
  return (abort.load()?uint32_t(ingress_reason_v2::abort_requested):0u)|
   (n.utc&&n.utc>=clock.expiry?uint32_t(ingress_reason_v2::utc_expired):0u)|
   (clock.uncertain(n)?uint32_t(ingress_reason_v2::clock_unknown):0u);
 }
 bool valid(Clock n)const noexcept{return mask(n)==0;}
 void stop()noexcept{abort.store(true);}
 // clear only after the caller has returned and retained owners are reconciled.
 void clear()noexcept{active.store(false,std::memory_order_release);}
};
inline RunGuard run_guard;
// A public nonce binds an instance, not an independent issuer. The selected
// Portal principal/receipt verifier and cooperating roster remain external.
}
