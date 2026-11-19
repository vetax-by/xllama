#pragma once
#include "dev_run_config.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace xllama::gpu_diagnostic {
// This plan controls observation only. It has no access to proof thresholds or GPU execution.
struct Axis {
 enum class Mode {all,workload,range,set}; Mode mode=Mode::all;
 uint32_t first=0,last=0,stride=1;
 std::vector<uint32_t> values;
 bool contains(uint32_t v,uint32_t limit)const noexcept{
  if(v>=limit)return false;
  if(mode==Mode::all)return true;
  if(mode==Mode::workload)return v>=first&&(v-first)%stride==0;
  if(mode==Mode::range)return v>=first&&v<last&&(v-first)%stride==0;
  return std::binary_search(values.begin(),values.end(),v);
 }
 uint32_t count(uint32_t limit)const noexcept{
  uint32_t n=0;for(uint32_t v=0;v<limit;++v)if(contains(v,limit))++n;return n;
 }
};
inline Axis axis(const dev_run::Json&j,uint32_t limit,bool allow_workload=false){
 using J=dev_run::Json; j.keys({"mode","first","last_exclusive","stride","values"});
 Axis a;const auto&m=j.at("mode");dev_run::need(m.type==J::Type::string,"diagnostic axis mode type");
 const auto&f=j.at("first"),&l=j.at("last_exclusive"),&s=j.at("stride"),&v=j.at("values");
 dev_run::need(v.type==J::Type::array,"diagnostic axis values type");
 if(m.text=="all"){
  dev_run::need(f.type==J::Type::null&&l.type==J::Type::null&&s.type==J::Type::null&&v.array.empty(),"diagnostic all axis shape");
  a.mode=Axis::Mode::all;
 }else if(m.text=="workload"){
  dev_run::need(allow_workload&&l.type==J::Type::null&&v.array.empty(),"diagnostic workload axis shape");
  a.mode=Axis::Mode::workload;a.first=uint32_t(f.range(0,limit-1));a.stride=uint32_t(s.range(1,limit));
 }else if(m.text=="range"){
  dev_run::need(v.array.empty(),"diagnostic range values");a.mode=Axis::Mode::range;
  a.first=uint32_t(f.range(0,limit-1));a.last=uint32_t(l.range(a.first+1,limit));a.stride=uint32_t(s.range(1,limit));
 }else if(m.text=="set"){
  dev_run::need(f.type==J::Type::null&&l.type==J::Type::null&&s.type==J::Type::null&&!v.array.empty()&&v.array.size()<=limit,"diagnostic set shape");
  a.mode=Axis::Mode::set;for(const auto&e:v.array){const auto x=uint32_t(e.range(0,limit-1));dev_run::need(a.values.empty()||x>a.values.back(),"diagnostic duplicate/unsorted set");a.values.push_back(x);}
 }else throw std::invalid_argument("diagnostic axis unsupported");
 return a;
}
struct VectorKey {uint32_t position=0,role=0,expert=0;};
struct Plan {
 enum class Arms {paired,cpu_only,hybrid_only};Arms arms=Arms::paired;
 static constexpr uint32_t max_positions=129,max_layers=40,max_vectors=16;
 static constexpr uint64_t memory_ceiling=64ull<<20,output_ceiling=2ull<<20,work_ceiling=120000000;
 static constexpr uint32_t time_ceiling_ms=120000;
 Axis positions,layers;
 std::vector<VectorKey> vectors;
 uint64_t memory_budget=0,output_budget=0,reference_work_budget=0;
 uint32_t time_budget_ms=0,max_records=0,element_start=0,element_stride=1,element_count=2048,top_k=0;
 uint32_t analysis_abs_ppm=1000,analysis_rel_ppm=1000;
 bool exact=true,stop_on_first=false;
 uint64_t selected_pairs_upper()const noexcept{return uint64_t(positions.count(max_positions))*layers.count(max_layers);}
 uint64_t sweep_work_upper()const noexcept{return selected_pairs_upper()*(4096u+(exact?element_count:0));}
 uint64_t vector_work_upper()const noexcept{return uint64_t(vectors.size())*(2048u*2048u+2048u);}
 uint64_t required_reference_bytes()const noexcept{return exact?selected_pairs_upper()*2048*4:0;}
};
inline Plan parse(const dev_run::Json&j){
 using J=dev_run::Json;
 j.keys({"schema","overview","vectors","sampling","analysis","output","budgets"});
 j.at("schema").eq("ua68-model2-diagnostics-v1");Plan p;
 const auto&o=j.at("overview");o.keys({"positions","layers","arms","point","compare","collection"});
 p.positions=axis(o.at("positions"),Plan::max_positions,true);p.layers=axis(o.at("layers"),Plan::max_layers);
 const auto&arms=o.at("arms");dev_run::need(arms.type==J::Type::string,"diagnostic arms type");
 if(arms.text=="paired")p.arms=Plan::Arms::paired;else if(arms.text=="cpu_only")p.arms=Plan::Arms::cpu_only;else if(arms.text=="hybrid_only")p.arms=Plan::Arms::hybrid_only;else throw std::invalid_argument("diagnostic arms unsupported");
 o.at("point").eq("l_out");const auto&cmp=o.at("compare");dev_run::need(cmp.type==J::Type::string,"diagnostic compare type");
 if(cmp.text=="exact_f32")p.exact=true;else if(cmp.text=="hash_finite")p.exact=false;else throw std::invalid_argument("diagnostic compare unsupported");
 dev_run::need(!p.exact||p.arms==Plan::Arms::paired,"exact compare needs paired arms");
 const auto&coll=o.at("collection");dev_run::need(coll.type==J::Type::string,"diagnostic collection type");
 if(coll.text=="collect_selected")p.stop_on_first=false;else if(coll.text=="stop_diagnostic_on_first_violation")p.stop_on_first=true;else throw std::invalid_argument("diagnostic collection unsupported");
 dev_run::need(!p.stop_on_first||p.exact,"diagnostic stop requires numeric compare");
 const auto&vectors=j.at("vectors");dev_run::need(vectors.type==J::Type::array&&vectors.array.size()<=Plan::max_vectors,"diagnostic vector count");
 for(const auto&x:vectors.array){x.keys({"phase","position","role","expert"});x.at("phase").eq(int64_t(2));VectorKey k;
  k.position=uint32_t(x.at("position").range(0,128));k.role=uint32_t(x.at("role").range(0,113));k.expert=uint32_t(x.at("expert").range(0,63));
  for(const auto&prior:p.vectors)dev_run::need(prior.position!=k.position||prior.role!=k.role||prior.expert!=k.expert,"diagnostic vector duplicate");p.vectors.push_back(k);
 }
 const auto&sample=j.at("sampling");sample.keys({"element_start","element_stride","element_count"});
 p.element_start=uint32_t(sample.at("element_start").range(0,2047));p.element_stride=uint32_t(sample.at("element_stride").range(1,2048));p.element_count=uint32_t(sample.at("element_count").range(1,2048));
 dev_run::need(uint64_t(p.element_start)+uint64_t(p.element_count-1)*p.element_stride<2048,"diagnostic sample extent");
 const auto&analysis=j.at("analysis");analysis.keys({"absolute_ppm","relative_ppm"});p.analysis_abs_ppm=uint32_t(analysis.at("absolute_ppm").range(0,1000000));p.analysis_rel_ppm=uint32_t(analysis.at("relative_ppm").range(0,1000000));
 const auto&out=j.at("output");out.keys({"top_k"});p.top_k=uint32_t(out.at("top_k").range(0,16));
 const auto&b=j.at("budgets");b.keys({"max_records","max_output_bytes","max_extra_memory_bytes","max_reference_work","max_time_ms"});
 p.max_records=uint32_t(b.at("max_records").range(1,Plan::max_positions*Plan::max_layers+Plan::max_vectors));
 p.output_budget=b.at("max_output_bytes").range(4096,Plan::output_ceiling);p.memory_budget=b.at("max_extra_memory_bytes").range(1u<<20,Plan::memory_ceiling);
 p.reference_work_budget=b.at("max_reference_work").range(1,Plan::work_ceiling);p.time_budget_ms=uint32_t(b.at("max_time_ms").range(1000,Plan::time_ceiling_ms));
 const uint64_t pair_count=p.selected_pairs_upper();
 dev_run::need(pair_count+p.vectors.size()<=p.max_records,"diagnostic records budget preflight");
 dev_run::need(p.required_reference_bytes()+2u*(2048u*4u)+4u*1024u*1024u<=p.memory_budget,"diagnostic memory budget preflight");
 dev_run::need(p.sweep_work_upper()+p.vector_work_upper()<=p.reference_work_budget,"diagnostic work budget preflight");
 // A 320-byte row upper estimate includes schema, IDs, hashes and summary values.
 dev_run::need(pair_count*320+p.vectors.size()*512+4096<=p.output_budget,"diagnostic output budget preflight");
 return p;
}
}
