#pragma once
#include "dev_run_config.h"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace xllama::ingress_v2 {
// Dispatch is deliberately separate from the three stage parsers.  A ticket is
// staged data; only an exact, independently selected intent can activate it.
inline constexpr size_t intent_cap=4096, max_candidates=32, max_scan_bytes=10u<<20;
inline constexpr size_t max_directory_entries=1024;
inline constexpr uint64_t max_scan_ms=2000;
inline constexpr size_t ui_intent_cap=2048, ui_marker_cap=1024, ui_review_cap=8192;
inline constexpr size_t ui_max_intents=32, ui_scan_bytes_cap=384u<<10;
static_assert(ui_max_intents*(ui_intent_cap+2*ui_marker_cap+ui_review_cap)<=ui_scan_bytes_cap);
// Conservative reservation for the scanner's retained strings/handles,
// transient read and JSON copies, directory names, SHA scratch and descriptors.
// A native quota bracket still has to admit this reservation before routing.
inline constexpr size_t route_scratch_and_descriptors=4u<<20;
inline constexpr size_t route_parser_and_held_per_candidate=128u<<10;
inline constexpr size_t route_directory_per_entry=2u<<10;
inline constexpr size_t route_output_and_serializer=2u<<20;
inline constexpr size_t route_memory_charge=2*max_scan_bytes+
 max_candidates*route_parser_and_held_per_candidate+
 max_directory_entries*route_directory_per_entry+
 route_scratch_and_descriptors+route_output_and_serializer;
inline constexpr size_t ui_memory_charge=2*ui_scan_bytes_cap+
 ui_max_intents*(16u<<10)+max_directory_entries*(2u<<10)+(1u<<20);
inline constexpr size_t route_quota_reserve=512u<<20;
inline bool ascii_prefix_ci(const std::string& name,const std::string& prefix){
 if(name.size()<prefix.size())return false;
 for(size_t n=0;n<prefix.size();++n){unsigned char a=name[n],b=prefix[n];if(a>='A'&&a<='Z')a=static_cast<unsigned char>(a+32);if(b>='A'&&b<='Z')b=static_cast<unsigned char>(b+32);if(a!=b)return false;}
 return true;
}
inline bool ui_time_admitted(uint64_t now,uint64_t expires){return now&&expires>now&&expires-now<=300000;}
inline bool preclaim_refusal_admitted(bool claim,bool output,bool terminal,bool ticket,
                                      bool ticket_sha_equal,bool package_equal){
 return !claim&&!output&&!terminal&&ticket&&ticket_sha_equal&&package_equal;
}
static_assert(route_memory_charge<=32u<<20);
static_assert(ui_memory_charge<=5u<<20);
inline void charge_scan_bytes(size_t& total,size_t add,size_t cap){
 if(total>cap||add>cap-total)throw std::runtime_error("ingress scan byte cap");
 total+=add;
}
inline void charge_scan_entry(size_t& total){
 if(total>=max_directory_entries)throw std::runtime_error("ingress directory entry cap");
 ++total;
}
inline bool route_quota_admitted(uint64_t first,uint64_t second,uint64_t used,
                                 bool low_or_medium,uint64_t charge){
 if(!first||first!=second||used>first||!low_or_medium||
    charge>UINT64_MAX-route_quota_reserve)return false;
 return first-used>=charge+route_quota_reserve;
}
inline constexpr char schema[]="xllama-headless-intent-v2";
inline constexpr char spent_identity_v1_sha[]="8e262186e384974614a18e3733eb29608a6330120d3af5368047080fb0b61c0a";
enum class Stage { identity, proof, cpu_dev };
inline const char* stage_name(Stage s){switch(s){case Stage::identity:return "identity";case Stage::proof:return "proof";case Stage::cpu_dev:return "cpu_dev";}return "unknown";}
inline const char* ticket_prefix(Stage s){switch(s){case Stage::identity:return "ua68-gpu-identity-v2.";case Stage::proof:return "ua68-gpu-proof-v2.";case Stage::cpu_dev:return "ua50-dev-run-v2.";}return "";}
inline std::string ticket_leaf(Stage s,const std::string& id){dev_run::need(dev_run::run_id(id),"ingress run id");return std::string(ticket_prefix(s))+id+".ticket.json";}
inline std::string intent_leaf(const std::string& id){dev_run::need(dev_run::run_id(id),"ingress run id");return "ua68-headless-intent-v2."+id+".json";}
inline std::string window_leaf(const std::string& id){dev_run::need(dev_run::run_id(id),"window run id");return "ua68-headless-window-v2."+id+".json";}
struct OperatorWindow {std::string run,selection,intent_sha,selection_receipt_sha,package;uint64_t expires=0;};
inline OperatorWindow parse_operator_window(const std::string& raw){
 auto j=dev_run::Parser(raw).parse(4096);j.keys({"schema","run_id","selection_id","intent_sha256","selection_receipt_sha256","full_package_name","expires_at_epoch_ms"});j.at("schema").eq("xllama-headless-operator-window-v2");OperatorWindow w;
 const auto& id=j.at("run_id");dev_run::need(id.type==dev_run::Json::Type::string&&dev_run::run_id(id.text),"window run");w.run=id.text;
 const auto& sel=j.at("selection_id");dev_run::need(sel.type==dev_run::Json::Type::string&&!sel.text.empty()&&sel.text.size()<=64&&std::all_of(sel.text.begin(),sel.text.end(),[](char c){return(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-';}),"window selection");w.selection=sel.text;
 const auto& h=j.at("intent_sha256");const auto& r=j.at("selection_receipt_sha256");dev_run::need(h.type==dev_run::Json::Type::string&&dev_run::hex(h.text,64)&&r.type==dev_run::Json::Type::string&&dev_run::hex(r.text,64),"window hashes");w.intent_sha=h.text;w.selection_receipt_sha=r.text;
 const auto& p=j.at("full_package_name");dev_run::need(p.type==dev_run::Json::Type::string&&p.text.size()>=32&&p.text.size()<=160,"window package");w.package=p.text;
 w.expires=j.at("expires_at_epoch_ms").range(1,INT64_MAX);return w;
}
inline std::string ui_leaf(const std::string& id){dev_run::need(dev_run::run_id(id),"UI run id");return "ua68-protected-ui-v2."+id+".intent.json";}
inline std::string ui_claim_leaf(const std::string& id){dev_run::need(dev_run::run_id(id),"UI run id");return "ua68-protected-ui-v2."+id+".claim.json";}
inline std::string ui_close_leaf(const std::string& id){dev_run::need(dev_run::run_id(id),"UI run id");return "ua68-protected-ui-v2."+id+".close.json";}
inline std::string ui_review_leaf(const std::string& id){dev_run::need(dev_run::run_id(id),"UI run id");return "ua68-protected-ui-v2."+id+".review.json";}
struct UiIntent {std::string run,selection,package,nonce;uint64_t expires=0;};
inline UiIntent parse_ui_intent(const std::string& raw){
 auto j=dev_run::Parser(raw).parse(ui_intent_cap);
 j.keys({"schema","run_id","selection_id","full_package_name","nonce_sha256","expires_at_epoch_ms"});
 j.at("schema").eq("xllama-protected-ui-intent-v2");UiIntent u;
 const auto& id=j.at("run_id");dev_run::need(id.type==dev_run::Json::Type::string&&dev_run::run_id(id.text),"UI run");u.run=id.text;
 const auto& sel=j.at("selection_id");dev_run::need(sel.type==dev_run::Json::Type::string&&!sel.text.empty()&&sel.text.size()<=64&&std::all_of(sel.text.begin(),sel.text.end(),[](char c){return(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-';}),"UI selection");u.selection=sel.text;
 const auto& p=j.at("full_package_name");dev_run::need(p.type==dev_run::Json::Type::string&&p.text.size()>=32&&p.text.size()<=160&&p.text.find("GianlucaMazza.xllama_")==0&&p.text.find("_x64__pj67f1fcj4n14")!=std::string::npos,"UI package");u.package=p.text;
 const auto& n=j.at("nonce_sha256");dev_run::need(n.type==dev_run::Json::Type::string&&dev_run::hex(n.text,64),"UI nonce");u.nonce=n.text;
 u.expires=j.at("expires_at_epoch_ms").range(1,INT64_MAX);return u;
}
struct UiMarker {std::string run,intent_sha,package;};
inline UiMarker parse_ui_marker(const std::string& raw,const char* schema){
 auto j=dev_run::Parser(raw).parse(1024);j.keys({"schema","run_id","intent_sha256","full_package_name"});j.at("schema").eq(schema);
 UiMarker m;const auto& id=j.at("run_id");dev_run::need(id.type==dev_run::Json::Type::string&&dev_run::run_id(id.text),"UI marker run");m.run=id.text;
 const auto& h=j.at("intent_sha256");dev_run::need(h.type==dev_run::Json::Type::string&&dev_run::hex(h.text,64),"UI marker SHA");m.intent_sha=h.text;
 const auto& p=j.at("full_package_name");dev_run::need(p.type==dev_run::Json::Type::string&&p.text.size()>=32&&p.text.size()<=160,"UI marker package");m.package=p.text;return m;
}
struct UiClose {UiMarker marker;std::string selection,claim_sha,post_ui_review_sha;};
inline UiClose parse_ui_close(const std::string& raw){
 auto j=dev_run::Parser(raw).parse(ui_marker_cap);j.keys({"schema","run_id","selection_id","intent_sha256","claim_sha256","full_package_name","post_ui_review_sha256"});j.at("schema").eq("xllama-protected-ui-close-v2");
 UiClose c;const auto& id=j.at("run_id");dev_run::need(id.type==dev_run::Json::Type::string&&dev_run::run_id(id.text),"UI close run");c.marker.run=id.text;
 const auto& sel=j.at("selection_id");dev_run::need(sel.type==dev_run::Json::Type::string&&!sel.text.empty()&&sel.text.size()<=64,"UI close selection");c.selection=sel.text;
 const auto& h=j.at("intent_sha256");dev_run::need(h.type==dev_run::Json::Type::string&&dev_run::hex(h.text,64),"UI close intent SHA");c.marker.intent_sha=h.text;
 const auto& ch=j.at("claim_sha256");dev_run::need(ch.type==dev_run::Json::Type::string&&dev_run::hex(ch.text,64),"UI close claim SHA");c.claim_sha=ch.text;
 const auto& p=j.at("full_package_name");dev_run::need(p.type==dev_run::Json::Type::string&&p.text.size()>=32&&p.text.size()<=160,"UI close package");c.marker.package=p.text;
 const auto& r=j.at("post_ui_review_sha256");dev_run::need(r.type==dev_run::Json::Type::string&&dev_run::hex(r.text,64),"UI close review SHA");c.post_ui_review_sha=r.text;return c;
}
// The reviewed receipt is staged as private data only after independent review.
// Syntax and exact byte binding here do not authenticate its external issuer;
// the selecting Ops adapter must verify the machine receipt before upload.
inline void check_ui_review_receipt(const std::string& raw,const UiIntent& ui,const std::string& claim_sha){
 auto j=dev_run::Parser(raw).parse(ui_review_cap);
 j.at("schema").eq("xllama-independent-post-ui-selected-readback-v2");
 j.at("selected_ui_run_id").eq(ui.run.c_str());
 j.at("selected_ui_selection_id").eq(ui.selection.c_str());
 j.at("native_ui_claim_sha256").eq(claim_sha.c_str());
 const auto version_begin=ui.package.find("_1.7.0.");dev_run::need(version_begin!=std::string::npos,"UI package version");
 const auto build_begin=version_begin+7,build_end=ui.package.find('_',build_begin);
 dev_run::need(build_end!=std::string::npos&&build_end>build_begin&&build_end-build_begin<=8,"UI package build");
 const auto expected_status="ACCEPTED_POST_UI_"+ui.package.substr(build_begin,build_end-build_begin)+"_OWNER_METADATA_ONLY";
 j.at("status").eq(expected_status.c_str());
 j.at("package_full_name").eq(ui.package.c_str());
 j.at("xllama_process_count").eq(int64_t(0));
 j.at("write_count").eq(int64_t(0));
 j.at("retry_count_reported").eq(int64_t(0));
 j.at("all_requests_closed_reported").eq(true);
 j.at("user_game_manual_ui_report").eq(("EXTERNAL_STAN_REPORT_AFTER_EXACT_"+ui.package.substr(build_begin,build_end-build_begin)+"_READBACK").c_str());
}
inline std::string result_prefix(Stage s,const std::string& id){dev_run::need(dev_run::run_id(id),"ingress run id");switch(s){case Stage::identity:return "ua68-gpu-identity-result-v2."+id+".";case Stage::proof:return "ua68-gpu-proof-v1."+id+".";case Stage::cpu_dev:return "ua50-dev-run-v1."+id+".";}throw std::invalid_argument("ingress result stage");}
enum class IdentityNameKind { other, ticket, result, invalid };
inline IdentityNameKind classify_identity_name(const std::string& name){
 const std::string ticket="ua68-gpu-identity-v2.",result="ua68-gpu-identity-result-v2.";
 const bool is_result=ascii_prefix_ci(name,result),is_ticket=ascii_prefix_ci(name,ticket);
 if(!is_result&&!is_ticket)return IdentityNameKind::other;
 const auto& prefix=is_result?result:ticket;
 if(name.rfind(prefix,0)!=0||name.size()<prefix.size()+32+1)return IdentityNameKind::invalid;
 const auto run=name.substr(prefix.size(),32),suffix=name.substr(prefix.size()+32);
 if(!dev_run::run_id(run))return IdentityNameKind::invalid;
 if(is_result){
  for(const char* allowed:{".attempt-v2.json",".terminal-v2.json",".output.json",".claim",".reason-v2.json",".stop"})if(suffix==allowed)return IdentityNameKind::result;
  return IdentityNameKind::invalid;
 }
 return suffix==".ticket.json"?IdentityNameKind::ticket:IdentityNameKind::invalid;
}
struct Attempt {Stage stage=Stage::identity;std::string run,ticket_sha,package;};
inline Attempt parse_attempt(const std::string& raw){auto j=dev_run::Parser(raw).parse(768);j.keys({"schema","stage","run_id","ticket_sha256","full_package_name"});j.at("schema").eq("xllama-headless-attempt-v2");Attempt a;const auto& st=j.at("stage");dev_run::need(st.type==dev_run::Json::Type::string,"attempt stage");if(st.text=="identity")a.stage=Stage::identity;else if(st.text=="proof")a.stage=Stage::proof;else if(st.text=="cpu_dev")a.stage=Stage::cpu_dev;else throw std::invalid_argument("attempt stage unsupported");const auto& id=j.at("run_id");dev_run::need(id.type==dev_run::Json::Type::string&&dev_run::run_id(id.text),"attempt run");a.run=id.text;const auto& th=j.at("ticket_sha256");dev_run::need(th.type==dev_run::Json::Type::string&&dev_run::hex(th.text,64),"attempt ticket SHA");a.ticket_sha=th.text;const auto& p=j.at("full_package_name");dev_run::need(p.type==dev_run::Json::Type::string&&!p.text.empty()&&p.text.size()<=160,"attempt package");a.package=p.text;return a;}
struct Terminal {Stage stage=Stage::identity;std::string run,ticket_sha,output_sha,package;bool normal=false;};
inline Terminal parse_terminal(const std::string& raw){
 auto j=dev_run::Parser(raw).parse(1024);j.keys({"schema","stage","run_id","ticket_sha256","output_sha256","full_package_name","normal"});j.at("schema").eq("xllama-headless-terminal-v2");
 Terminal t;const auto& st=j.at("stage");dev_run::need(st.type==dev_run::Json::Type::string,"terminal stage");if(st.text=="identity")t.stage=Stage::identity;else if(st.text=="proof")t.stage=Stage::proof;else if(st.text=="cpu_dev")t.stage=Stage::cpu_dev;else throw std::invalid_argument("terminal stage unsupported");
 const auto& id=j.at("run_id");dev_run::need(id.type==dev_run::Json::Type::string&&dev_run::run_id(id.text),"terminal run");t.run=id.text;
 const auto& th=j.at("ticket_sha256");const auto& oh=j.at("output_sha256");dev_run::need(th.type==dev_run::Json::Type::string&&dev_run::hex(th.text,64)&&oh.type==dev_run::Json::Type::string&&dev_run::hex(oh.text,64),"terminal hashes");t.ticket_sha=th.text;t.output_sha=oh.text;
 const auto& p=j.at("full_package_name");dev_run::need(p.type==dev_run::Json::Type::string&&!p.text.empty()&&p.text.size()<=160,"terminal package");t.package=p.text;
 const auto& n=j.at("normal");dev_run::need(n.type==dev_run::Json::Type::boolean,"terminal normal");t.normal=n.flag;return t;
}
struct Intent {
 Stage stage=Stage::identity;std::string run,selection,ticket_sha,config_sha,package;uint64_t expires=0;size_t parser_bytes=0;
 size_t owned_bytes()const{return sizeof(*this)+run.capacity()+selection.capacity()+ticket_sha.capacity()+config_sha.capacity()+package.capacity()+parser_bytes;}
};
inline Intent parse_intent(const std::string& raw){
 auto j=dev_run::Parser(raw).parse(intent_cap);
 j.keys({"schema","stage","run_id","selection_id","ticket_sha256","config_sha256","full_package_name","expires_at_epoch_ms"});
 j.at("schema").eq(schema);Intent i;i.parser_bytes=j.owned();
 const auto& st=j.at("stage");dev_run::need(st.type==dev_run::Json::Type::string,"ingress stage");
 if(st.text=="identity")i.stage=Stage::identity;else if(st.text=="proof")i.stage=Stage::proof;else if(st.text=="cpu_dev")i.stage=Stage::cpu_dev;else throw std::invalid_argument("ingress stage unsupported");
 const auto& id=j.at("run_id");dev_run::need(id.type==dev_run::Json::Type::string&&dev_run::run_id(id.text),"ingress run id");i.run=id.text;
 const auto& sel=j.at("selection_id");dev_run::need(sel.type==dev_run::Json::Type::string&&!sel.text.empty()&&sel.text.size()<=64&&std::all_of(sel.text.begin(),sel.text.end(),[](char c){return(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-';}),"ingress selection");i.selection=sel.text;
 const auto& th=j.at("ticket_sha256");dev_run::need(th.type==dev_run::Json::Type::string&&dev_run::hex(th.text,64),"ingress ticket SHA");i.ticket_sha=th.text;
 const auto& ch=j.at("config_sha256");dev_run::need(ch.type==dev_run::Json::Type::string,"ingress config SHA type");dev_run::need(i.stage==Stage::identity?ch.text.empty():dev_run::hex(ch.text,64),"ingress config SHA");i.config_sha=ch.text;
 const auto& pkg=j.at("full_package_name");dev_run::need(pkg.type==dev_run::Json::Type::string&&pkg.text.size()>=32&&pkg.text.size()<=160&&pkg.text.find("GianlucaMazza.xllama_")==0&&pkg.text.find("_x64__pj67f1fcj4n14")!=std::string::npos,"ingress package");i.package=pkg.text;
 i.expires=j.at("expires_at_epoch_ms").range(1,INT64_MAX);return i;
}
enum class State { absent, inert_spent_v1, selected, conflict, unavailable, spent_or_orphan };
enum class Lifecycle { fresh, closed_spent, orphan, unknown };
struct Candidate {Intent intent;bool ticket_present=false,claim_present=false,output_present=false,consume_known=false;bool exact_binding=false,window_bound=false,terminal_verified=false;Lifecycle lifecycle=Lifecycle::unknown;};
struct Inventory {std::vector<Candidate> candidates;bool old_identity_present=false,old_identity_exact_spent=false,old_proof_present=false,old_cpu_present=false,legacy_flag_present=false,scan_error=false;size_t bytes=0;uint64_t elapsed_ms=0;};
struct Decision {State state=State::unavailable;size_t selected=0;const char* reason="INGRESS_UNAVAILABLE";};
// Retained claims/refusals are history. They may coexist with one fresh run,
// but an unbound or ambiguous item is never silently treated as inert.
inline Decision decide(const Inventory& v,bool ui,uint64_t now,const std::string& full_package_name){
 if(v.scan_error||v.candidates.size()>max_candidates||v.bytes>max_scan_bytes||v.elapsed_ms>max_scan_ms)return {State::unavailable,0,"SCAN_CAP_OR_IO"};
 if((v.old_identity_present&&!v.old_identity_exact_spent)||v.old_proof_present||v.old_cpu_present||v.legacy_flag_present)return {State::conflict,0,"LEGACY_UNTRUSTED_OR_CONFLICT"};
 size_t active=0,selected=0;
 for(size_t n=0;n<v.candidates.size();++n){const auto& c=v.candidates[n];
  if(!c.exact_binding)return {State::unavailable,0,"SCHEMA_OR_BINDING"};
  if(c.lifecycle==Lifecycle::orphan||c.lifecycle==Lifecycle::unknown)return {State::spent_or_orphan,0,"ORPHAN_OR_UNKNOWN"};
  if(c.lifecycle==Lifecycle::closed_spent){if(!c.output_present||!c.terminal_verified)return {State::spent_or_orphan,0,"SPENT_TERMINAL_UNVERIFIED"};continue;}
  if(!c.window_bound)return {State::unavailable,0,"OPERATOR_WINDOW_UNBOUND"};
  if(c.claim_present||c.output_present||c.consume_known)return {State::spent_or_orphan,0,"FRESH_STATE_CONFLICT"};
  if(!c.ticket_present)return {State::unavailable,0,"INTENT_WITHOUT_TICKET"};
  if(c.intent.package!=full_package_name)return {State::unavailable,0,"PACKAGE_MISMATCH"};
  const uint64_t window=c.intent.stage==Stage::identity?300000:1800000;
  if(c.intent.expires<=now||c.intent.expires-now>window)return {State::unavailable,0,"EXPIRY_UNKNOWN_OR_INVALID"};
  ++active;selected=n;
 }
 if(active>1)return {State::conflict,0,"INGRESS_CONFLICT"};
 if(ui)return active?Decision{State::conflict,0,"UI_HEADLESS_ACTIVE"}:Decision{State::absent,0,"UI_CLEAR"};
 if(!active)return {State::absent,0,"NO_SELECTED_INTENT"};
 return {State::selected,selected,"SELECTED"};
}
}
