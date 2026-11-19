#pragma once
#include "gpu_diagnostic_plan.h"
#include "gpu_graph_witness.h"
#include "gpu_cross_arm_witness.h"
#include <memory>
#include <string>
#include <vector>

namespace xllama::gpu_diagnostic {
// Versioned diagnostic selection. V3 is observational; the fixed proof
// contract and immutable execution envelope remain in gpu_run_config.h.
struct PlanV3 {
 Plan base;
 std::vector<gpu_proof::GraphWitness::Selection> graph;
 std::vector<gpu_proof::CrossArmWitness::Key> cross_arm;
 std::vector<gpu_proof::CrossArmWitness::HeldSelection> held_weight;
 bool graph_workload_axis=false,raw_private=false,held_raw_private=false;
 uint32_t graph_abs_ppm=1000,graph_rel_ppm=1000,cross_abs_ppm=1000,cross_rel_ppm=1000;
 uint64_t graph_memory=0,graph_output=0,graph_work=0,cross_memory=0,cross_output=0,cross_work=0;
 uint32_t graph_time_ms=0,cross_time_ms=0;
 uint64_t held_memory=0,held_output=0,held_work=0,held_output_upper=0;
 uint32_t held_time_ms=0;
 uint64_t graph_memory_upper=0,graph_output_upper=0,graph_work_upper=0,cross_output_upper=0;
 uint64_t serializer_charge=0;
 size_t owned_bytes()const noexcept{
  size_t n=sizeof(*this)+graph.capacity()*sizeof(gpu_proof::GraphWitness::Selection)+
   cross_arm.capacity()*sizeof(gpu_proof::CrossArmWitness::Key)+
   held_weight.capacity()*sizeof(gpu_proof::CrossArmWitness::HeldSelection);
  for(const auto&entry:graph)n+=entry.point.capacity();
  return n;
 }
};
inline PlanV3 parse_v3(const dev_run::Json&j){
 using J=dev_run::Json;
 const bool held_schema=j.at("schema").type==J::Type::string&&j.at("schema").text=="ua68-model2-diagnostics-v3";
 if(held_schema)j.keys({"schema","overview","vectors","sampling","analysis","output","budgets","graph","cross_arm","held_weight"});
 else{j.keys({"schema","overview","vectors","sampling","analysis","output","budgets","graph","cross_arm"});j.at("schema").eq("ua68-model2-diagnostics-v2");}
 J old;old.type=J::Type::object;
 J version;version.type=J::Type::string;version.text="ua68-model2-diagnostics-v1";
 old.object.emplace_back("schema",std::move(version));
 for(const char*name:{"overview","vectors","sampling","analysis","output","budgets"})old.object.emplace_back(name,j.at(name));
 PlanV3 p;p.base=gpu_diagnostic::parse(old);
 const auto&graph=j.at("graph");
 if(graph.type!=J::Type::null){
  graph.keys({"positions","layers","points","analysis","budgets"});
  const auto pos=axis(graph.at("positions"),Plan::max_positions,true);
  const auto layers=axis(graph.at("layers"),Plan::max_layers);
  p.graph_workload_axis=pos.mode==Axis::Mode::all||pos.mode==Axis::Mode::workload;
  const auto&points=graph.at("points");
  dev_run::need(points.type==J::Type::array&&!points.array.empty()&&points.array.size()<=std::size(gpu_proof::GraphWitness::catalog),"graph point list bound");
  std::vector<const gpu_proof::GraphWitness::Point*> chosen;
  for(const auto&entry:points.array){
   dev_run::need(entry.type==J::Type::string,"graph point type");
   const gpu_proof::GraphWitness::Point*found=nullptr;
   for(const auto&catalog:gpu_proof::GraphWitness::catalog)if(entry.text==catalog.key){found=&catalog;break;}
   dev_run::need(found!=nullptr,"unsupported graph point");
   for(const auto*prior:chosen)dev_run::need(prior!=found,"duplicate graph point");
   chosen.push_back(found);
  }
  const auto&analysis=graph.at("analysis");analysis.keys({"absolute_ppm","relative_ppm"});
  p.graph_abs_ppm=uint32_t(analysis.at("absolute_ppm").range(0,1000000));
  p.graph_rel_ppm=uint32_t(analysis.at("relative_ppm").range(0,1000000));
  const auto&b=graph.at("budgets");b.keys({"max_output_bytes","max_extra_memory_bytes","max_reference_work","max_time_ms"});
  p.graph_output=b.at("max_output_bytes").range(4096,Plan::output_ceiling);
  p.graph_memory=b.at("max_extra_memory_bytes").range(1u<<20,Plan::memory_ceiling);
  p.graph_work=b.at("max_reference_work").range(1,Plan::work_ceiling);
  p.graph_time_ms=uint32_t(b.at("max_time_ms").range(1000,Plan::time_ceiling_ms));
  p.graph.reserve(8192);
  p.graph_memory_upper=1048576;
  p.graph_output_upper=4096;
  for(uint32_t position=0;position<Plan::max_positions;++position)if(pos.contains(position,Plan::max_positions))
   for(uint32_t layer=0;layer<Plan::max_layers;++layer)if(layers.contains(layer,Plan::max_layers))
    for(const auto*point:chosen){
     if(!point->layer&&layer!=0)continue;
     dev_run::need(p.graph.size()<8192,"graph expanded bound before allocation");
     p.graph.push_back({position,layer,point->key});
     p.graph_memory_upper+=sizeof(gpu_proof::GraphWitness::Row)+sizeof(size_t)+uint64_t(point->max_elements)*8;
     p.graph_output_upper+=gpu_proof::GraphWitness::max_encoded_row_bytes;
     p.graph_work_upper+=uint64_t(point->max_elements)*2;
    }
  dev_run::need(!p.graph.empty()&&p.graph.size()<=8192&&p.graph_memory_upper<=p.graph_memory&&
   p.graph_output_upper<=p.graph_output&&p.graph_work_upper<=p.graph_work,"graph expanded bounds");
 }
 const auto&cross=j.at("cross_arm");
 if(cross.type!=J::Type::null){
  cross.keys({"rows","analysis","raw_private","budgets"});
  const auto&rows=cross.at("rows");dev_run::need(rows.type==J::Type::array&&!rows.array.empty()&&rows.array.size()<=16,"cross-arm rows bound");
  for(const auto&entry:rows.array){
   entry.keys({"position","role","expert","slot","input_row","occurrence"});
   gpu_proof::CrossArmWitness::Key key{
    uint32_t(entry.at("position").range(0,128)),uint32_t(entry.at("role").range(0,113)),
    uint32_t(entry.at("expert").range(0,63)),
    entry.at("slot").type==J::Type::null?gpu_proof::CrossArmWitness::any_slot:uint32_t(entry.at("slot").range(0,3)),
    uint32_t(entry.at("input_row").range(0,128)),uint32_t(entry.at("occurrence").range(0,3))};
   for(const auto&prior:p.cross_arm)dev_run::need(
    prior.position!=key.position||prior.role!=key.role||prior.expert!=key.expert||
    (prior.slot!=key.slot&&prior.slot!=gpu_proof::CrossArmWitness::any_slot&&key.slot!=gpu_proof::CrossArmWitness::any_slot)||
    prior.input_row!=key.input_row||prior.occurrence!=key.occurrence,
    "duplicate cross-arm semantic key");
   p.cross_arm.push_back(key);
  }
  dev_run::need(cross.at("raw_private").type==J::Type::boolean,"raw private type");
  p.raw_private=cross.at("raw_private").flag;
  dev_run::need(!p.raw_private||p.cross_arm.size()<=8,"raw private mapping cap");
  const auto&a=cross.at("analysis");a.keys({"absolute_ppm","relative_ppm"});
  p.cross_abs_ppm=uint32_t(a.at("absolute_ppm").range(0,1000000));
  p.cross_rel_ppm=uint32_t(a.at("relative_ppm").range(0,1000000));
  const auto&b=cross.at("budgets");b.keys({"max_output_bytes","max_extra_memory_bytes","max_reference_work","max_time_ms"});
  p.cross_output=b.at("max_output_bytes").range(4096,Plan::output_ceiling);
  p.cross_memory=b.at("max_extra_memory_bytes").range(1u<<20,Plan::memory_ceiling);
  p.cross_work=b.at("max_reference_work").range(1,Plan::work_ceiling);
  p.cross_time_ms=uint32_t(b.at("max_time_ms").range(1000,Plan::time_ceiling_ms));
  const uint64_t logical_raw=uint64_t(p.cross_arm.size())*2*(2048*4+2336+2048*4);
  dev_run::need(!p.raw_private||logical_raw<=512u*1024,"raw private logical cap");
  p.cross_output_upper=4096+uint64_t(p.cross_arm.size())*1024+(p.raw_private?2*logical_raw:0);
  dev_run::need(p.cross_output_upper<=p.cross_output,"cross-arm encoded output bound");
  dev_run::need(sizeof(gpu_proof::CrossArmWitness)+uint64_t(p.cross_arm.size())*sizeof(gpu_proof::CrossArmWitness::Record)+1048576<=p.cross_memory,"cross-arm memory bound");
  dev_run::need(uint64_t(p.cross_arm.size())*2*(2*2048+2336+2048)<=p.cross_work,"cross-arm work bound");
 }
 if(held_schema){
  const auto&held=j.at("held_weight");
  dev_run::need(held.type!=J::Type::null,"v3 held-weight selection required");
  if(held.type!=J::Type::null){
   dev_run::need(!p.cross_arm.empty(),"held-weight requires paired cross rows");
   held.keys({"rows","raw_private","budgets"});const auto&rows=held.at("rows");
   dev_run::need(rows.type==J::Type::array&&!rows.array.empty()&&rows.array.size()<=8,"held-weight rows bound");
   for(const auto&entry:rows.array){
    entry.keys({"phase","position","layer","token","role","expert","slot","input_row","occurrence"});
    dev_run::need(entry.at("phase").range(2,2)==2,"held-weight hybrid phase only");
    gpu_proof::CrossArmWitness::HeldSelection s{};
    s.key={uint32_t(entry.at("position").range(0,128)),uint32_t(entry.at("role").range(0,113)),
      uint32_t(entry.at("expert").range(0,63)),
      entry.at("slot").type==J::Type::null?gpu_proof::CrossArmWitness::any_slot:uint32_t(entry.at("slot").range(0,3)),
      uint32_t(entry.at("input_row").range(0,128)),uint32_t(entry.at("occurrence").range(0,3))};
    s.layer=uint32_t(entry.at("layer").range(2,39));s.token=int32_t(entry.at("token").range(0,127999));
    dev_run::need(s.key.role/3==s.layer-2,"held-weight registered role/layer");
    size_t matches=0;for(const auto&c:p.cross_arm)matches+=
      c.position==s.key.position&&c.role==s.key.role&&c.expert==s.key.expert&&
      c.slot==s.key.slot&&c.input_row==s.key.input_row&&c.occurrence==s.key.occurrence;
    dev_run::need(matches==1,"held-weight exact cross selection");
    for(const auto&prior:p.held_weight)dev_run::need(!(prior.key.position==s.key.position&&
      prior.key.role==s.key.role&&prior.key.expert==s.key.expert&&prior.key.slot==s.key.slot&&
      prior.key.input_row==s.key.input_row&&prior.key.occurrence==s.key.occurrence),"held-weight duplicate");
    p.held_weight.push_back(s);
   }
   const auto&b=held.at("budgets");b.keys({"max_output_bytes","max_extra_memory_bytes","max_reference_work","max_time_ms"});
   dev_run::need(held.at("raw_private").type==J::Type::boolean,"held raw type");
   p.held_raw_private=held.at("raw_private").flag;
   dev_run::need(!p.held_raw_private||(p.raw_private&&p.held_weight.size()==1),"held raw requires one private paired row");
   p.held_output=b.at("max_output_bytes").range(4096,Plan::output_ceiling);
   p.held_memory=b.at("max_extra_memory_bytes").range(1u<<20,Plan::memory_ceiling);
   p.held_work=b.at("max_reference_work").range(1,Plan::work_ceiling);
   p.held_time_ms=uint32_t(b.at("max_time_ms").range(1000,Plan::time_ceiling_ms));
   p.held_output_upper=4096+1600*p.held_weight.size()+(p.held_raw_private?32768:0);
   dev_run::need(p.held_output_upper<=p.held_output&&
      uint64_t(p.held_weight.size())*2*2048*1536<=p.held_work&&
      1048576+uint64_t(p.held_weight.size())*sizeof(gpu_proof::CrossArmWitness::HeldRow)+32768<=p.held_memory,
      "held-weight preflight bounds");
  }
 }
 const uint64_t output_charge=p.base.output_budget+p.graph_output+p.cross_output+p.held_output;
 p.serializer_charge=8*output_charge+(1u<<20);
 // Summary strings, concatenation temporaries, sidecar and capacity coexist.
 // Charge eight full output envelopes plus fixed serializer scratch.
 dev_run::need(p.base.memory_budget+p.graph_memory+p.cross_memory+p.held_memory+
  p.serializer_charge<=Plan::memory_ceiling,"joint diagnostic memory ceiling");
 dev_run::need(output_charge<=Plan::output_ceiling,"joint diagnostic output ceiling");
 dev_run::need(p.base.reference_work_budget+p.graph_work+p.cross_work+p.held_work<=Plan::work_ceiling,"joint diagnostic work ceiling");
 dev_run::need(uint64_t(p.base.time_budget_ms)+p.graph_time_ms+p.cross_time_ms+p.held_time_ms<=Plan::time_ceiling_ms,"joint diagnostic time ceiling");
 return p;
}
}
