#pragma once
#include "dev-run-input.h"
#include "gpu-identity-input.h"
#include "gpu-proof-input.h"
#include <chrono>
#include <optional>
#include <set>

namespace xllama::bridge {
struct IngressScan {
 ingress_v2::Inventory inventory;ingress_v2::Decision decision;
 std::vector<SelectedIngress> entries;std::vector<DevHandle> held_intents;DevHandle held_legacy_identity;
};
inline bool ingress_file_exists(const std::filesystem::path& p){const DWORD a=GetFileAttributesW(p.c_str());if(a==INVALID_FILE_ATTRIBUTES){if(GetLastError()==ERROR_FILE_NOT_FOUND)return false;throw std::runtime_error("ingress file presence unknown");}if(a&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE))throw std::runtime_error("ingress file shape");return true;}
inline void ingress_charge(ingress_v2::Inventory& v,size_t n){ingress_v2::charge_scan_bytes(v.bytes,n,ingress_v2::max_scan_bytes);}
inline std::string ingress_read(ingress_v2::Inventory&v,DevHandle&h,size_t cap){auto s=read_dev_file(h,cap);ingress_charge(v,s.size());return s;}
inline bool ingress_same(const ingress_v2::Attempt&a,const ingress_v2::Intent&i){return a.stage==i.stage&&a.run==i.run&&a.ticket_sha==i.ticket_sha&&a.package==i.package;}
inline bool ingress_same(const ingress_v2::Terminal&t,const ingress_v2::Intent&i){return t.stage==i.stage&&t.run==i.run&&t.ticket_sha==i.ticket_sha&&t.package==i.package;}
inline IngressScan scan_ingress_v2(const std::filesystem::path& root,const std::string& package,bool ui){
 using namespace ingress_v2;IngressScan out;
 const auto started=std::chrono::steady_clock::now();
 auto elapsed=[&]{return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count());};
 const auto old=root/L"ua68-gpu-identity-v1.ticket.json";
 if(ingress_file_exists(old)){
  out.inventory.old_identity_present=true;open_dev_file(out.held_legacy_identity,old,true);const auto raw=ingress_read(out.inventory,out.held_legacy_identity,gpu_identity::request_cap);
  out.inventory.old_identity_exact_spent=dev_sha(raw)==spent_identity_v1_sha&&raw.find("d9423262070c2b5f3443f1c456cfe73d")!=std::string::npos;
 }
 out.inventory.old_proof_present=ingress_file_exists(root/L"ua68-gpu-proof-v1.ticket.json");
 out.inventory.old_cpu_present=ingress_file_exists(root/L"ua50-dev-run-v1.ticket.json");
 for(const auto* name:{L"ua50-cpu-lfm24-cache-a2-v1.flag",L"ua50-cpu-lfm24-cache-b512-v1.flag",L"ua50-cpu-lfm24-finished-answer-v2.flag",L"ua50-cpu-lfm24-first-answer-v1.flag",L"ua50-cpu-lfm24-pilot.flag",L"diffuse.flag",L"bench.flag",L"membw.flag",L"diskbw.flag",L"gpubw.flag",L"gpugemv.flag",L"d3d12be.flag",L"gpustep.flag",L"ramceil.flag",L"mic.flag",L"logits.flag",L"oprepro.flag",L"train.flag",L"native-capture.flag",L"diffuse-inproc.flag",L"gpustep-inproc.flag",L"autopilot.flag"})if(ingress_file_exists(root/name))out.inventory.legacy_flag_present=true;
 std::error_code ec;std::filesystem::directory_iterator it(root,ec),end;if(ec)throw std::runtime_error("ingress enumeration unavailable");
 const std::string prefix="ua68-headless-intent-v2.";
 std::set<std::string> ticket_names,window_names,identity_result_names;
 size_t visited=0;
 for(;it!=end;it.increment(ec)){
  if(ec)throw std::runtime_error("ingress enumeration failure");if(elapsed()>max_scan_ms)throw std::runtime_error("ingress enumeration deadline");
  charge_scan_entry(visited);
  const auto name=it->path().filename().string();
  const auto identity_name=classify_identity_name(name);
  if(identity_name==IdentityNameKind::invalid)throw std::runtime_error("ingress identity reserved name");
  if(identity_name==IdentityNameKind::result)identity_result_names.insert(name);
  bool v2_ticket=identity_name==IdentityNameKind::ticket;
  for(const auto stage:{Stage::proof,Stage::cpu_dev}){
   const std::string p=ticket_prefix(stage);
   if(ascii_prefix_ci(name,p)&&name.rfind(p,0)!=0)throw std::runtime_error("ingress reserved ticket name case variant");
   if(name.rfind(p,0)==0){v2_ticket=true;break;}
  }
  if(v2_ticket){
   if(name.size()>128||name.size()<48||name.substr(name.size()-12)!=".ticket.json"||!ticket_names.insert(name).second)throw std::runtime_error("ingress v2 ticket name/duplicate");
  }
  const std::string window_prefix="ua68-headless-window-v2.";
  if(ascii_prefix_ci(name,window_prefix)&&name.rfind(window_prefix,0)!=0)throw std::runtime_error("ingress reserved window name case variant");
  if(name.rfind(window_prefix,0)==0){
   if(name.size()!=window_prefix.size()+32+5||name.substr(name.size()-5)!=".json"||!dev_run::run_id(name.substr(window_prefix.size(),32))||!window_names.insert(name).second)throw std::runtime_error("ingress operator window name/duplicate");
  }
  if(ascii_prefix_ci(name,prefix)&&name.rfind(prefix,0)!=0)throw std::runtime_error("ingress reserved intent name case variant");
  if(name.rfind(prefix,0)!=0)continue;
  if(out.inventory.candidates.size()>=max_candidates||name.size()>128||name.size()!=prefix.size()+32+5||name.substr(name.size()-5)!=".json")throw std::runtime_error("ingress candidate name/count cap");
  SelectedIngress s;open_dev_file(s.intent_file,it->path(),false);s.intent_raw=ingress_read(out.inventory,s.intent_file,intent_cap);s.intent=parse_intent(s.intent_raw);
  if(name!=intent_leaf(s.intent.run))throw std::runtime_error("ingress filename/run mismatch");
  Candidate c;c.intent=s.intent;c.exact_binding=true;
  const auto window_name=window_leaf(s.intent.run);const auto wp=root/std::filesystem::path(std::wstring(window_name.begin(),window_name.end()));
  open_dev_file(s.window_file,wp,false);s.window_raw=ingress_read(out.inventory,s.window_file,4096);s.window_sha=dev_sha(s.window_raw);
  const auto window=parse_operator_window(s.window_raw);
  if(window.run!=s.intent.run||window.selection!=s.intent.selection||window.intent_sha!=dev_sha(s.intent_raw)||window.package!=s.intent.package||window.expires!=s.intent.expires)throw std::runtime_error("ingress operator window binding");c.window_bound=true;
  const auto ticket_name=ticket_leaf(s.intent.stage,s.intent.run);const auto ticket_path=root/std::filesystem::path(std::wstring(ticket_name.begin(),ticket_name.end()));
  if(ingress_file_exists(ticket_path)){
   open_dev_file(s.ticket,ticket_path,true);s.ticket_raw=ingress_read(out.inventory,s.ticket,4096);s.ticket_sha=dev_sha(s.ticket_raw);c.ticket_present=true;
   if(s.ticket_sha!=s.intent.ticket_sha)throw std::runtime_error("ingress ticket SHA mismatch");
   const auto pseudo_now=s.intent.expires-1;
   if(s.intent.stage==Stage::identity){auto x=gpu_identity::parse_versioned(s.ticket_raw,pseudo_now,true);if(x.id!=s.intent.run||x.selection!=s.intent.selection||x.expires!=s.intent.expires||!s.intent.config_sha.empty())throw std::runtime_error("ingress identity ticket mismatch");}
   else if(s.intent.stage==Stage::proof){auto x=gpu_run::ticket_versioned(s.ticket_raw,pseudo_now,true);if(x.id!=s.intent.run||x.selection!=s.intent.selection||x.expires!=s.intent.expires||x.config_sha!=s.intent.config_sha)throw std::runtime_error("ingress proof ticket mismatch");}
   else {auto x=dev_run::parse_ticket_versioned(s.ticket_raw,pseudo_now,true);if(x.id!=s.intent.run||x.selection!=s.intent.selection||x.expires!=s.intent.expires||x.config_sha!=s.intent.config_sha)throw std::runtime_error("ingress CPU ticket mismatch");}
  }
  const auto result_name=result_prefix(s.intent.stage,s.intent.run);const auto base=root/std::filesystem::path(std::wstring(result_name.begin(),result_name.end()));
  const auto attempt=std::filesystem::path(base.wstring()+L"attempt-v2.json"),terminal=std::filesystem::path(base.wstring()+L"terminal-v2.json"),output=std::filesystem::path(base.wstring()+L"output.json"),claim=std::filesystem::path(base.wstring()+L"claim");
  const bool attempted=ingress_file_exists(attempt),terminated=ingress_file_exists(terminal);c.claim_present=ingress_file_exists(claim);c.output_present=ingress_file_exists(output);c.consume_known=attempted&&!c.ticket_present;
  if(c.claim_present){DevHandle h;open_dev_file(h,claim,false);const auto bytes=ingress_read(out.inventory,h,128);if(bytes!=s.intent.ticket_sha+"\n")throw std::runtime_error("ingress claim binding");}
  bool attempt_ok=false;if(attempted){DevHandle h;open_dev_file(h,attempt,false);attempt_ok=ingress_same(parse_attempt(ingress_read(out.inventory,h,768)),s.intent);if(!attempt_ok)throw std::runtime_error("ingress attempt binding");}
  if(terminated){DevHandle h;open_dev_file(h,terminal,false);const auto t=parse_terminal(ingress_read(out.inventory,h,1024));if(!attempt_ok||!ingress_same(t,s.intent)||!c.output_present)throw std::runtime_error("ingress terminal binding");DevHandle oh;open_dev_file(oh,output,false);const auto bytes=ingress_read(out.inventory,oh,s.intent.stage==Stage::identity?gpu_identity::result_cap:262144);if(dev_sha(bytes)!=t.output_sha)throw std::runtime_error("ingress terminal output SHA");c.terminal_verified=true;c.lifecycle=Lifecycle::closed_spent;}
  else c.lifecycle=attempted||c.claim_present||c.output_present||!c.ticket_present?Lifecycle::orphan:Lifecycle::fresh;
  if(c.lifecycle==Lifecycle::fresh&&s.intent.stage!=Stage::identity){const auto config_leaf=result_prefix(s.intent.stage,s.intent.run)+"config.json";const auto cp=root/std::filesystem::path(std::wstring(config_leaf.begin(),config_leaf.end()));open_dev_file(s.config,cp,false);s.config_raw=ingress_read(out.inventory,s.config,16384);s.config_sha=dev_sha(s.config_raw);if(s.config_sha!=s.intent.config_sha)throw std::runtime_error("ingress config SHA mismatch");}
  out.inventory.candidates.push_back(std::move(c));out.entries.push_back(std::move(s));
 }
 if(ec)throw std::runtime_error("ingress enumeration final error");
 for(const auto& name:ticket_names){
  bool matched=false;
  for(const auto& candidate:out.inventory.candidates)if(ticket_leaf(candidate.intent.stage,candidate.intent.run)==name&&candidate.ticket_present){matched=true;break;}
  if(!matched)throw std::runtime_error("unbound v2 ticket");
 }
 for(const auto& name:window_names){bool matched=false;for(const auto& candidate:out.inventory.candidates)if(window_leaf(candidate.intent.run)==name){matched=true;break;}if(!matched)throw std::runtime_error("unbound operator window");}
 for(const auto& name:identity_result_names){bool matched=false;for(const auto& candidate:out.inventory.candidates)if(candidate.intent.stage==Stage::identity&&name.rfind(result_prefix(Stage::identity,candidate.intent.run),0)==0){matched=true;break;}if(!matched)throw std::runtime_error("unbound identity result");}
 out.inventory.elapsed_ms=elapsed();out.decision=decide(out.inventory,ui,dev_utc_ms(),package);return out;
}
}
