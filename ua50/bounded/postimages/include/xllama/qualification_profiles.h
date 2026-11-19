#pragma once
#include "qualification_mailbox.h"
#include "gpu_identity.h"
#include "gpu_run_config.h"
#include "generation_profile.h"
namespace xllama::qualification {
inline bool same_generation_profile(const generation::Request&a,const generation::Request&b){const auto&x=a.profile;const auto&y=b.profile;return a.id==b.id&&a.prompt==b.prompt&&x.hybrid==y.hybrid&&x.workers==y.workers&&x.context==y.context&&x.prompt_cap==y.prompt_cap&&x.max_new==y.max_new&&x.slots==y.slots&&x.ceiling==y.ceiling;}
inline void joint_diagnostic_envelope(const gpu_run::Config&c){
 if(c.diagnostics_v3){const auto&p=*c.diagnostics_v3;
  dev_run::need(p.base.memory_budget+p.graph_memory+p.cross_memory+p.held_memory+p.serializer_charge+retained_charge<=gpu_diagnostic::Plan::memory_ceiling,"mailbox plus optional diagnostic joint64MiB");
  dev_run::need(p.base.output_budget+p.graph_output+p.cross_output+p.held_output+control_output_charge<=gpu_diagnostic::Plan::output_ceiling,"mailbox plus optional encoded output joint2MiB");
 }else if(c.diagnostics){dev_run::need(c.diagnostics->memory_budget+retained_charge<=gpu_diagnostic::Plan::memory_ceiling&&c.diagnostics->output_budget+control_output_charge<=gpu_diagnostic::Plan::output_ceiling,"mailbox plus legacy diagnostic joint envelope");}
}
inline void validate_profile(Stage stage,const std::string&raw,const std::string&run){
 if(stage==Stage::identity){const auto j=dev_run::Parser(raw).parse(profile_cap);j.keys({"schema","run_id","contract_sha256"});j.at("schema").eq("xllama-identity-profile-v1");j.at("run_id").eq(run.c_str());j.at("contract_sha256").eq(gpu_identity::contract_sha);}
 else if(stage==Stage::proof){const auto c=gpu_run::parse(raw);dev_run::need(c.id==run,"proof profile run");joint_diagnostic_envelope(c);}
 else if(stage==Stage::cpu_dev)dev_run::need(dev_run::parse_config(raw).id==run,"CPU profile run");
 else if(stage==Stage::generation){const auto request=generation::parse(raw);dev_run::need(request.id==run&&request.permit_sha==std::string(64,'0'),"generation immutable profile placeholder");}
 else throw std::runtime_error("no diagnostic stage");
}
inline std::string issued_ticket(const Command&c){
 const auto q=first_answer::quote;
 if(c.work==Stage::identity)return "{\"schema\":\"ua68-gpu-identity-request-v2\",\"contract_sha256\":"+q(gpu_identity::contract_sha)+",\"run_id\":"+q(c.run)+",\"selection_id\":"+q(c.selection)+",\"expires_at_epoch_ms\":"+std::to_string(c.expires)+"}";
 dev_run::need(c.work==Stage::proof||c.work==Stage::cpu_dev,"native ticket stage");
 return "{\"schema\":"+q(c.work==Stage::proof?"ua68-gpu-proof-ticket-v2":"xllama-dev-run-ticket-v2")+",\"selection_id\":"+q(c.selection)+",\"run_id\":"+q(c.run)+",\"config_sha256\":"+q(c.config_sha)+",\"engine_contract_sha256\":"+q(c.work==Stage::proof?gpu_run::engine_sha:dev_run::engine_digest)+",\"expires_at_epoch_ms\":"+std::to_string(c.expires)+"}";
}
inline void validate_final(const Prepare&p,const Command&c,const std::string&profile,const std::string&config,const std::string&permit,uint64_t now){
 dev_run::need(c.work==p.work&&c.run==p.run,"final stage/run");
 if(c.work==Stage::generation){
  const auto a=generation::parse(profile),b=generation::parse(config);
  dev_run::need(same_generation_profile(a,b)&&b.permit_sha==c.permit_sha,"generation final/profile permit binding");
  const auto v=generation::permit(permit,now,p.package);
  dev_run::need(v.expires==c.expires&&v.artifact==p.artifact_sha&&v.model2==p.prerequisite_sha,"generation issued expiry/artifact/prerequisite");
 }else{dev_run::need(profile==config&&c.permit_sha==std::string(64,'0'),"exact static profile/config");validate_profile(c.work,config,c.run);
  if(c.work==Stage::proof){const auto v=gpu_run::parse(config);if(v.stage==gpu_proof::Stage::Model2)dev_run::need(v.primitive_acceptance_sha==p.prerequisite_sha,"model2 prerequisite");}
 }
}
}
