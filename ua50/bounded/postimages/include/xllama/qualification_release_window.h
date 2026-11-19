#pragma once
#include "qualification_release.h"
#include "qualification_receipts.h"
namespace xllama::qualification {
inline constexpr size_t release_query_cap=4096,release_window_cap=4096;
struct ReleaseQuery {
 std::string run,selection,package,instance,artifact,command,control,completion,challenge;
};
inline ReleaseQuery release_query(const std::string&raw){
 const auto j=dev_run::Parser(raw).parse(release_query_cap);
 j.keys({"schema","run_id","selection_id","full_package_name","instance_id","signed_artifact_sha256","command_sha256","control_receipt_sha256","completion_nonce_sha256","host_challenge_sha256"});
 j.at("schema").eq("xllama-qualification-release-window-query-v1");ReleaseQuery q;
 q.run=text(j,"run_id",32);q.selection=text(j,"selection_id",64);q.package=text(j,"full_package_name",160);q.instance=text(j,"instance_id",32);
 q.artifact=hash(j,"signed_artifact_sha256");q.command=hash(j,"command_sha256");q.control=hash(j,"control_receipt_sha256");q.completion=hash(j,"completion_nonce_sha256");q.challenge=hash(j,"host_challenge_sha256");
 dev_run::need(dev_run::run_id(q.run)&&dev_run::run_id(q.instance)&&selection_id(q.selection),"release query identity");return q;
}
inline void validate_release_query(const ReleaseQuery&q,const Session&s,const std::string&command,const std::string&control,const std::string&completion){
 dev_run::need(s.state==Phase::returned&&s.claim_attempted&&s.dispatches==1,"window only after actual adapter return");
 dev_run::need(q.run==s.selected.run&&q.selection==s.selected.selection&&q.package==s.actual_package&&q.instance==s.instance&&q.artifact==s.selected.artifact_sha&&q.command==command&&q.control==control&&q.completion==completion&&dev_run::hex(completion,64),"window current completed instance");
}
enum ReleaseMask:uint32_t { release_not_started=1u,release_aborted=2u,release_clock_unavailable=4u,release_clock_rollback=8u,release_clock_drift=16u,release_time_exhausted=32u,release_polls_exhausted=64u };
struct ReleaseObservation {uint32_t mask=0;Clock sample{};uint64_t elapsed=0,polls=0;bool elapsed_available=false,started=false,abort=false;};
struct ReleaseStop:std::runtime_error {ReleaseObservation observation;explicit ReleaseStop(ReleaseObservation o):std::runtime_error("release stopped; see bounded release-window-refusal.json"),observation(o){}};
inline std::string release_stop_body(const Session&s,uint32_t pid,const ReleaseObservation&o,const std::string&command,const std::string&completion){
 return "{\"schema\":\"xllama-qualification-release-stop-v1\",\"status\":\"RELEASE_STOPPED_NO_ACCEPTANCE\",\"run_id\":"+quoted(s.selected.run)+",\"selection_id\":"+quoted(s.selected.selection)+",\"instance_id\":"+quoted(s.instance)+",\"full_package_name\":"+quoted(s.actual_package)+",\"signed_artifact_sha256\":"+quoted(s.selected.artifact_sha)+",\"stage\":"+quoted(name(s.selected.work))+",\"pid\":"+std::to_string(pid)+",\"command_sha256\":"+quoted(command)+",\"completion_nonce_sha256\":"+quoted(completion)+",\"release_mask\":"+std::to_string(o.mask)+",\"elapsed_ms\":"+(o.elapsed_available?std::to_string(o.elapsed):"null")+",\"polls_consumed\":"+std::to_string(o.polls)+",\"utc_available\":"+(o.sample.utc?"true":"false")+",\"steady_available\":"+(o.sample.steady?"true":"false")+",\"release_started\":"+(o.started?"true":"false")+",\"abort_requested\":"+(o.abort?"true":"false")+",\"native_resources_closed\":null,\"scientific_acceptance\":\"NOT_GRANTED\"}";
}
inline ReleaseObservation parse_release_stop(const std::string&raw){
 const auto j=dev_run::Parser(raw).parse(release_window_cap);j.keys({"schema","status","run_id","selection_id","instance_id","full_package_name","signed_artifact_sha256","stage","pid","command_sha256","completion_nonce_sha256","release_mask","elapsed_ms","polls_consumed","utc_available","steady_available","release_started","abort_requested","native_resources_closed","scientific_acceptance"});
 j.at("schema").eq("xllama-qualification-release-stop-v1");j.at("status").eq("RELEASE_STOPPED_NO_ACCEPTANCE");j.at("scientific_acceptance").eq("NOT_GRANTED");dev_run::need(j.at("native_resources_closed").type==dev_run::Json::Type::null,"stop not resource evidence");dev_run::need(dev_run::run_id(text(j,"run_id",32))&&dev_run::run_id(text(j,"instance_id",32))&&selection_id(text(j,"selection_id",64)),"stop identity");(void)text(j,"full_package_name",160);(void)stage(text(j,"stage",16));(void)hash(j,"signed_artifact_sha256");(void)hash(j,"command_sha256");(void)hash(j,"completion_nonce_sha256");(void)j.at("pid").range(1,UINT32_MAX);
 ReleaseObservation o;o.mask=uint32_t(j.at("release_mask").range(1,127));o.polls=j.at("polls_consumed").range(0,release_polls);o.elapsed_available=j.at("elapsed_ms").type!=dev_run::Json::Type::null;if(o.elapsed_available)o.elapsed=j.at("elapsed_ms").range(0,INT64_MAX);
 for(const char*k:{"utc_available","steady_available","release_started","abort_requested"})dev_run::need(j.at(k).type==dev_run::Json::Type::boolean,"stop boolean");o.started=j.at("release_started").flag;o.abort=j.at("abort_requested").flag;o.sample.utc=j.at("utc_available").flag?1:0;o.sample.steady=j.at("steady_available").flag?1:0;
 dev_run::need(bool(o.mask&release_not_started)==!o.started&&bool(o.mask&release_aborted)==o.abort&&bool(o.mask&release_clock_unavailable)==(!o.sample.utc||!o.sample.steady)&&bool(o.mask&release_time_exhausted)==(o.elapsed_available&&o.elapsed>=release_wait_ms)&&bool(o.mask&release_polls_exhausted)==(o.polls>=release_polls),"stop predicate snapshot mismatch");return o;
}
// Timing-only owner. Work UTC expiry is NOT a release predicate. This fresh
// clock anchor detects unknown/rollback/drift independently of work expiry.
// Poll count is separately bounded; no assertion that one poll == one second.
struct ReleaseWindow {
 Clock first{},last{};uint64_t polls=0;bool started=false,query_seen=false;std::string response_sha,challenge;
 void start(Clock n){dev_run::need(!started,"release window cannot restart");if(!n.utc||!n.steady)throw ReleaseStop(observation(n));first=last=n;started=true;}
 ReleaseObservation observation(Clock n,bool abort=false)const {
  ReleaseObservation o;o.sample=n;o.polls=polls;o.started=started;o.abort=abort;
  o.elapsed_available=started&&n.steady&&n.steady>=first.steady;if(o.elapsed_available)o.elapsed=n.steady-first.steady;
  if(!started)o.mask|=release_not_started;if(abort)o.mask|=release_aborted;if(!n.utc||!n.steady)o.mask|=release_clock_unavailable;
  const bool rollback=started&&((n.steady&&n.steady<first.steady)||(n.utc&&n.utc<first.utc)||(n.steady&&n.steady<last.steady)||(n.utc&&n.utc<last.utc));if(rollback)o.mask|=release_clock_rollback;
  Anchor clock;clock.first=first;if(started&&n.utc&&n.steady&&!rollback&&clock.uncertain(n))o.mask|=release_clock_drift;
  if(o.elapsed_available&&o.elapsed>=release_wait_ms)o.mask|=release_time_exhausted;if(polls>=release_polls)o.mask|=release_polls_exhausted;return o;
 }
 void check(Clock n,bool abort=false){const auto o=observation(n,abort);if(o.mask)throw ReleaseStop(o);last=n;}
 void tick(Clock n,bool abort=false){check(n,abort);++polls;}
 void acquire_query(){dev_run::need(started&&!query_seen,"release query one use");query_seen=true;}
 std::string response(const ReleaseQuery&q,const Session&s,uint32_t pid,const std::string&query_sha,Clock n,bool abort=false){
  check(n,abort);dev_run::need(query_seen&&response_sha.empty(),"release response one use");
  dev_run::need(s.clock.first.steady&&n.steady>=s.clock.first.steady,"window instance clock unavailable");
  const auto left=release_wait_ms-(n.steady-first.steady),count=release_polls-polls;
  return "{\"schema\":\"xllama-qualification-release-window-v1\",\"status\":\"TIMING_ONLY_NOT_RESOURCE_AUTHORITY\",\"run_id\":"+quoted(q.run)+",\"selection_id\":"+quoted(q.selection)+",\"full_package_name\":"+quoted(s.actual_package)+",\"instance_id\":"+quoted(s.instance)+",\"signed_artifact_sha256\":"+quoted(s.selected.artifact_sha)+",\"stage\":"+quoted(name(s.selected.work))+",\"pid\":"+std::to_string(pid)+",\"command_sha256\":"+quoted(q.command)+",\"control_receipt_sha256\":"+quoted(q.control)+",\"completion_nonce_sha256\":"+quoted(q.completion)+",\"query_sha256\":"+quoted(query_sha)+",\"host_challenge_sha256\":"+quoted(q.challenge)+",\"remaining_steady_ms\":"+std::to_string(left)+",\"remaining_polls\":"+std::to_string(count)+",\"sample_instance_steady_elapsed_ms\":"+std::to_string(n.steady-s.clock.first.steady)+",\"timer_reset\":false,\"resources_closed\":null,\"scientific_acceptance\":\"NOT_GRANTED\"}";
 }
 void published(const std::string&sha,const std::string&host_challenge){dev_run::need(query_seen&&response_sha.empty()&&dev_run::hex(sha,64)&&dev_run::hex(host_challenge,64),"release publication binding");response_sha=sha;challenge=host_challenge;}
};
// Portable receiver parser for the exact native wire; external code still
// authenticates acquisition/context and computes conservative age from POST.
struct WindowReply {ReleaseQuery query;Stage work=Stage::none;uint32_t pid=0;std::string query_sha;uint64_t remaining_ms=0,remaining_polls=0,instance_elapsed_ms=0;};
inline WindowReply window_reply(const std::string&raw){
 const auto j=dev_run::Parser(raw).parse(release_window_cap);
 j.keys({"schema","status","run_id","selection_id","full_package_name","instance_id","signed_artifact_sha256","stage","pid","command_sha256","control_receipt_sha256","completion_nonce_sha256","query_sha256","host_challenge_sha256","remaining_steady_ms","remaining_polls","sample_instance_steady_elapsed_ms","timer_reset","resources_closed","scientific_acceptance"});
 j.at("schema").eq("xllama-qualification-release-window-v1");j.at("status").eq("TIMING_ONLY_NOT_RESOURCE_AUTHORITY");j.at("timer_reset").eq(false);j.at("scientific_acceptance").eq("NOT_GRANTED");dev_run::need(j.at("resources_closed").type==dev_run::Json::Type::null,"window is not resource evidence");WindowReply r;auto&q=r.query;
 q.run=text(j,"run_id",32);q.selection=text(j,"selection_id",64);q.package=text(j,"full_package_name",160);q.instance=text(j,"instance_id",32);q.artifact=hash(j,"signed_artifact_sha256");q.command=hash(j,"command_sha256");q.control=hash(j,"control_receipt_sha256");q.completion=hash(j,"completion_nonce_sha256");q.challenge=hash(j,"host_challenge_sha256");
 dev_run::need(dev_run::run_id(q.run)&&dev_run::run_id(q.instance)&&selection_id(q.selection),"window reply identity");r.work=stage(text(j,"stage",16));dev_run::need(r.work!=Stage::none,"window reply stage");r.pid=uint32_t(j.at("pid").range(1,UINT32_MAX));r.query_sha=hash(j,"query_sha256");r.remaining_ms=j.at("remaining_steady_ms").range(1,release_wait_ms);r.remaining_polls=j.at("remaining_polls").range(1,release_polls);r.instance_elapsed_ms=j.at("sample_instance_steady_elapsed_ms").range(1,INT64_MAX);return r;
}
inline void validate_window_reply(const WindowReply&r,const ReleaseQuery&q,Stage work,uint32_t pid,const std::string&query_sha){
 const auto&a=r.query;dev_run::need(a.run==q.run&&a.selection==q.selection&&a.package==q.package&&a.instance==q.instance&&a.artifact==q.artifact&&a.command==q.command&&a.control==q.control&&a.completion==q.completion&&a.challenge==q.challenge&&r.work==work&&r.pid==pid&&r.query_sha==query_sha,"current one-use window reply");
}
struct TimedRelease {Release value;std::string window_sha,challenge;};
inline TimedRelease timed_release(const std::string&raw){
 const auto j=dev_run::Parser(raw).parse(command_cap);
 j.keys({"schema","run_id","selection_id","full_package_name","instance_id","receipt_sha256","closure_sha256","completion_nonce_sha256","phase","action","release_window_sha256","host_challenge_sha256"});
 j.at("schema").eq("xllama-qualification-release-v2");j.at("phase").eq(int64_t(2));j.at("action").eq("exit_after_independent_native_closure");TimedRelease t;auto&r=t.value;
 r.run=text(j,"run_id",32);r.selection=text(j,"selection_id",64);r.package=text(j,"full_package_name",160);r.instance=text(j,"instance_id",32);r.receipt_sha=hash(j,"receipt_sha256");r.closure_sha=hash(j,"closure_sha256");r.completion_nonce=hash(j,"completion_nonce_sha256");t.window_sha=hash(j,"release_window_sha256");t.challenge=hash(j,"host_challenge_sha256");
 dev_run::need(dev_run::run_id(r.run)&&dev_run::run_id(r.instance)&&selection_id(r.selection),"timed release identity");return t;
}
inline void validate_timed_release(const TimedRelease&t,ReleaseWindow&w,Clock n,bool abort){w.check(n,abort);dev_run::need(!w.response_sha.empty()&&t.window_sha==w.response_sha&&t.challenge==w.challenge,"release fresh window binding");}
}
