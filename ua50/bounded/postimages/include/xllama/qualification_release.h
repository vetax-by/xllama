#pragma once
#include "qualification_mailbox.h"
namespace xllama::qualification {
inline constexpr size_t closure_cap=16384, review_cap=65536;
inline constexpr uint64_t release_wait_ms=600000,release_polls=600;
struct Release {std::string run,selection,package,instance,receipt_sha,closure_sha,completion_nonce;};
struct Closure {std::string run,selection,package,instance,command_sha,receipt_sha,output_sha,review_sha,artifact_sha,completion_nonce;Stage work=Stage::none;};
inline Release release(const std::string&raw){
 const auto j=dev_run::Parser(raw).parse(command_cap);
 j.keys({"schema","run_id","selection_id","full_package_name","instance_id","receipt_sha256","closure_sha256","completion_nonce_sha256","phase","action"});
 j.at("schema").eq("xllama-qualification-release-v1");j.at("phase").eq(int64_t(2));j.at("action").eq("exit_after_independent_native_closure");Release r;
 r.run=text(j,"run_id",32);r.selection=text(j,"selection_id",64);r.package=text(j,"full_package_name",160);r.instance=text(j,"instance_id",32);r.completion_nonce=hash(j,"completion_nonce_sha256");r.receipt_sha=hash(j,"receipt_sha256");r.closure_sha=hash(j,"closure_sha256");dev_run::need(dev_run::run_id(r.run)&&dev_run::run_id(r.instance)&&selection_id(r.selection),"release identity");return r;
}
inline Closure closure(const std::string&raw){
 const auto j=dev_run::Parser(raw).parse(closure_cap);
 j.keys({"schema","run_id","selection_id","full_package_name","instance_id","stage","signed_artifact_sha256","command_sha256","receipt_sha256","native_output_sha256","completion_nonce_sha256","independent_review_sha256","native_resources_closed","issuer","post_work_review"});
 j.at("schema").eq("xllama-qualification-closure-attestation-v1");j.at("issuer").eq("nominated-ops-external-authority");j.at("native_resources_closed").eq(true);j.at("post_work_review").eq(true);Closure c;
 c.run=text(j,"run_id",32);c.selection=text(j,"selection_id",64);c.package=text(j,"full_package_name",160);c.instance=text(j,"instance_id",32);c.work=stage(text(j,"stage",16));c.artifact_sha=hash(j,"signed_artifact_sha256");c.command_sha=hash(j,"command_sha256");c.receipt_sha=hash(j,"receipt_sha256");c.completion_nonce=hash(j,"completion_nonce_sha256");c.output_sha=hash(j,"native_output_sha256");c.review_sha=hash(j,"independent_review_sha256");dev_run::need(dev_run::run_id(c.run)&&dev_run::run_id(c.instance)&&selection_id(c.selection),"closure identity");return c;
}
// Closed PRE-EXIT phase binding, NOT authentication of the external reviewer
// or replacement for its stage-specific scientific-resource evidence check.
inline void validate_resource_review(const std::string&raw,const Session&s,const Closure&c,uint32_t pid){
 const auto j=dev_run::Parser(raw).parse(review_cap);
 j.keys({"schema","phase","status","issuer","run_id","selection_id","instance_id","full_package_name","signed_artifact_sha256","stage","pid","instance_nonce_sha256","completion_nonce_sha256","command_sha256","control_receipt_sha256","native_output_sha256","native_terminal_sha256","trace_binding_sha256","trace_sha256","process_reply_sha256","evidence_manifest_sha256","resource_scope","resource_state","native_normal_reported","scientific_acceptance","process_state","process_closure","http_caller_closure","post_work_review"});
 j.at("schema").eq("xllama-independent-pre-exit-resource-review-v1");j.at("phase").eq("PRE_EXIT_RESOURCE_ONLY");j.at("status").eq("STAGE_RESOURCES_CLOSED_PROCESS_PENDING");
 j.at("resource_scope").eq("STAGE_OWNED_RESOURCES");j.at("resource_state").eq("CLOSED");j.at("scientific_acceptance").eq("NOT_GRANTED");j.at("process_state").eq("CURRENT_INSTANCE_ALIVE");j.at("process_closure").eq("PENDING");j.at("http_caller_closure").eq("PENDING");j.at("post_work_review").eq(true);
 dev_run::need(!text(j,"issuer",256).empty(),"external reviewer identity unavailable");
 j.at("run_id").eq(s.selected.run.c_str());j.at("selection_id").eq(s.selected.selection.c_str());j.at("instance_id").eq(s.instance.c_str());j.at("full_package_name").eq(s.actual_package.c_str());j.at("signed_artifact_sha256").eq(s.selected.artifact_sha.c_str());j.at("stage").eq(name(s.selected.work));j.at("pid").eq(int64_t(pid));j.at("instance_nonce_sha256").eq(s.nonce.c_str());
 j.at("completion_nonce_sha256").eq(c.completion_nonce.c_str());j.at("command_sha256").eq(c.command_sha.c_str());j.at("control_receipt_sha256").eq(c.receipt_sha.c_str());j.at("native_output_sha256").eq(c.output_sha.c_str());
 (void)hash(j,"process_reply_sha256");(void)hash(j,"evidence_manifest_sha256");dev_run::need(j.at("native_normal_reported").type==dev_run::Json::Type::boolean,"resource normal report type");
 for(const char*k:{"native_terminal_sha256","trace_binding_sha256","trace_sha256"})if(j.at(k).type!=dev_run::Json::Type::null)(void)hash(j,k);
 dev_run::need(s.selected.work==Stage::generation?j.at("native_terminal_sha256").type==dev_run::Json::Type::null:j.at("native_terminal_sha256").type==dev_run::Json::Type::string,"stage terminal evidence kind");
 if(s.selected.work==Stage::generation){(void)hash(j,"trace_binding_sha256");(void)hash(j,"trace_sha256");}
}
inline void validate_release(const Session&s,const Release&r,const Closure&c,const std::string&receipt_sha,const std::string&output_sha,const std::string&command_sha,const std::string&completion_nonce){
 dev_run::need(dev_run::hex(completion_nonce,64)&&r.completion_nonce==completion_nonce&&c.completion_nonce==completion_nonce,"release post-work nonce");
 dev_run::need(s.state==Phase::returned&&s.claim_attempted&&s.dispatches==1,"release only after actual adapter return");
 dev_run::need(r.run==s.selected.run&&r.selection==s.selected.selection&&r.package==s.actual_package&&r.instance==s.instance&&r.receipt_sha==receipt_sha,"release current instance");
 dev_run::need(c.run==r.run&&c.selection==r.selection&&c.package==r.package&&c.instance==r.instance&&c.work==s.selected.work&&c.artifact_sha==s.selected.artifact_sha&&c.command_sha==command_sha&&c.receipt_sha==receipt_sha&&c.output_sha==output_sha,"closure current native binding");
 // Issuer, independent review authenticity and post-work chronology MUST be
 // verified externally before either upload. SHA matching is not proof of
 // issuer or chronology and does not grant native/numerical acceptance.
}
}
