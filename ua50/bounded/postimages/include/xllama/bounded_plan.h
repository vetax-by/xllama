#pragma once
#include "bounded_capture.h"
#include "bounded_catalog.h"
#include "dev_run_config.h"
namespace xllama::capture {
struct Selection {
 std::string kind,point;uint32_t layer=0,position=0,token=0,occurrence=0,role=0,expert=0,input_row=0,slot=4;
 uint32_t weight_row=0,weight_rows=1,block=0,blocks=1,output_row=0,output_rows=1;
 uint32_t cell=0,cells=1,element=0,elements=1;
};
struct Plan {
 bool worker_fp=false,observer_control=false,gpu_input_echo=false,io_cache=false,shader_trace=false;std::string profile;Limits limits;std::vector<Selection> selections;
 uint64_t gpu_memory()const{return (gpu_input_echo?131072ull:0)+(shader_trace?(8ull<<20):0);}
 size_t owned_bytes()const{return sizeof(*this)+profile.capacity()+selections.capacity()*sizeof(Selection);}
};
inline Plan parse_plan(const dev_run::Json&j){
 j.keys({"schema","profile","limits","selectors","extensions"});j.at("schema").eq("xllama-bounded-diagnostics-plan-v2");
 Plan p;p.profile=j.at("profile").text;need(p.profile=="R0"||p.profile=="E0"||p.profile=="S0"||p.profile=="G0","bounded profile");
 const auto&ex=j.at("extensions");ex.keys({"worker_fp","observer_control","gpu_input_echo","io_cache","shader_trace"});
 auto flag=[&](const char*key){const auto&v=ex.at(key);need(v.type==dev_run::Json::Type::boolean,"bounded extension boolean");return v.flag;};p.worker_fp=flag("worker_fp");p.observer_control=flag("observer_control");p.gpu_input_echo=flag("gpu_input_echo");p.io_cache=flag("io_cache");p.shader_trace=flag("shader_trace");need((!p.gpu_input_echo&&!p.io_cache&&!p.shader_trace)||p.profile=="E0","expert-only extension");
 const auto&l=j.at("limits");l.keys({"records","raw_bytes","bundle_bytes","index_bytes","owner_bytes","reference_work","time_ms"});
 p.limits.records=uint32_t(l.at("records").range(1,64));p.limits.raw=l.at("raw_bytes").range(1,1048576);p.limits.bundle=l.at("bundle_bytes").range(248,1081344);p.limits.index=l.at("index_bytes").range(4096,262144);p.limits.owner=l.at("owner_bytes").range(1048576,16777216);p.limits.work=l.at("reference_work").range(1,120000000);p.limits.time_ms=uint32_t(l.at("time_ms").range(1,120000));
 const auto&s=j.at("selectors");need(s.type==dev_run::Json::Type::array&&!s.array.empty()&&s.array.size()<=16,"bounded selector cap");
 uint64_t worst_raw=0,records=0,weight_bytes=0,weight_rows=0,weight_spans=0,state_scalars=0;size_t primary=0,side=0;
 for(const auto&v:s.array){Selection x;x.kind=v.at("kind").text;
  if(x.kind=="RESIDUAL_CVEC")v.keys({"kind","layer","position","token","occurrence"});
  else if(x.kind=="ARITHMETIC")v.keys({"kind","point","layer","position","token","occurrence"});
  else if(x.kind=="GRAPH_EDGE")v.keys({"kind","point","layer","position","token","occurrence","element_start","element_count"});
  else if(x.kind=="PARAMETER_ROWS")v.keys({"kind","point","layer","position","token","occurrence","weight_row_start","weight_row_count","block_start","block_count"});
  else if(x.kind=="EXPERT_DISPATCH")v.keys({"kind","layer","position","token","occurrence","role","expert","input_row","slot","weight_row_start","weight_row_count","block_start","block_count","output_row_start","output_row_count"});
  else if(x.kind=="KV_K"||x.kind=="KV_V_TRANSPOSED"||x.kind=="SHORTCONV")v.keys({"kind","layer","position","token","occurrence","cell_start","cell_count","element_start","element_count"});
  else throw std::invalid_argument("unsupported bounded adapter");
  x.layer=uint32_t(v.at("layer").range(0,39));x.position=uint32_t(v.at("position").range(0,128));x.token=uint32_t(v.at("token").range(0,127999));x.occurrence=uint32_t(v.at("occurrence").range(0,65535));
  if(x.kind=="RESIDUAL_CVEC"){need(p.profile=="R0"&&primary++==0&&p.selections.empty(),"R0 first and only residual group");worst_raw+=81920;records+=10;}
  else if(x.kind=="ARITHMETIC"){
   need(p.profile=="G0","arithmetic graph profile");x.point=v.at("point").text;const auto*point=graph_point(x.point);need(point&&(point->op==2||point->op==7),"arithmetic ADD/MUL catalog");
   const uint64_t n=uint64_t(point->n0)*point->n1;need(n<=16384,"arithmetic full tensor cap");worst_raw+=x.point=="moe_aggregate"?81952:n*24;records+=6;
  }
  else if(x.kind=="GRAPH_EDGE"){
   if(p.profile!="G0")need(++side<=4,"bounded common graph side-probe cap");x.point=v.at("point").text;const auto*point=graph_point(x.point);need(point,"unregistered graph point");
   x.element=uint32_t(v.at("element_start").range(0,uint64_t(point->n0)*point->n1-1));x.elements=uint32_t(v.at("element_count").range(1,16384));need(x.element+x.elements<=uint64_t(point->n0)*point->n1,"graph selected window");
   worst_raw+=uint64_t(x.elements)*8;records+=2;
  }
  else if(x.kind=="PARAMETER_ROWS"){
   need(p.profile=="G0","parameter graph profile");x.point=v.at("point").text;need(x.point=="result_output"||x.point=="conv_kernel","unregistered resident parameter endpoint");const uint32_t rows=x.point=="result_output"?128000:2048,columns=x.point=="result_output"?2048:64;
   x.weight_row=uint32_t(v.at("weight_row_start").range(0,rows-1));x.weight_rows=uint32_t(v.at("weight_row_count").range(1,16));x.block=uint32_t(v.at("block_start").range(0,columns-1));x.blocks=uint32_t(v.at("block_count").range(1,columns));need(x.weight_row+x.weight_rows<=rows&&x.block+x.blocks<=columns,"parameter bounded ranges");const uint64_t bytes=uint64_t(x.weight_rows)*x.blocks*210;need(bytes<=65536,"parameter worst quant/scalar record cap");weight_bytes+=bytes*2;weight_rows+=x.weight_rows*2;weight_spans+=2;worst_raw+=bytes*2;records+=2;
  }
  else if(x.kind=="EXPERT_DISPATCH"){
   need(p.profile=="E0","expert profile");++primary;x.role=uint32_t(v.at("role").range(0,113));need(x.layer>=2&&x.role/3==x.layer-2,"expert role layer");x.expert=uint32_t(v.at("expert").range(0,63));x.input_row=uint32_t(v.at("input_row").range(0,0));x.slot=v.at("slot").type==dev_run::Json::Type::null?4:uint32_t(v.at("slot").range(0,3));
   const uint32_t k=x.role%3==1?1536:2048,n=x.role%3==1?2048:1536;
   x.weight_row=uint32_t(v.at("weight_row_start").range(0,n-1));x.weight_rows=uint32_t(v.at("weight_row_count").range(1,16));x.block=uint32_t(v.at("block_start").range(0,k/256-1));x.blocks=uint32_t(v.at("block_count").range(1,k/256));x.output_row=uint32_t(v.at("output_row_start").range(0,n-1));x.output_rows=uint32_t(v.at("output_row_count").range(1,n));
   need(x.weight_row+x.weight_rows<=n&&x.block+x.blocks<=k/256&&x.output_row+x.output_rows<=n,"expert selected ranges");
   const uint64_t weights=uint64_t(x.weight_rows)*x.blocks*(x.role%3==1?210:144);need(weights<=262144,"selected weight cap");
   weight_bytes+=weights*2;weight_rows+=x.weight_rows*2;weight_spans+=2;
   worst_raw+=2*(k*4+k/256*292+weights+x.output_rows*4*3);records+=12;if(p.gpu_input_echo){worst_raw+=weights+k/256*292;records+=2;weight_bytes+=weights;weight_rows+=x.weight_rows;++weight_spans;}if(p.shader_trace){need(x.output_rows<=64,"shader trace selected rows cap");worst_raw+=uint64_t(x.output_rows)*64*8;records+=2;}
  }else{need(p.profile=="S0","state profile");++primary;x.cell=uint32_t(v.at("cell_start").range(0,511));x.cells=uint32_t(v.at("cell_count").range(1,32));x.element=uint32_t(v.at("element_start").range(0,65535));x.elements=uint32_t(v.at("element_count").range(1,16384));need(x.cell+x.cells<=512,"state cell range");const size_t bytes=mul(mul(x.cells,x.elements),x.kind=="SHORTCONV"?4:2);need(mul(mul(x.cells,x.elements),4)<=65536,"state source record window");worst_raw+=bytes*(x.kind=="SHORTCONV"?8:10);records+=8;state_scalars+=2*x.cells*(x.kind=="KV_V_TRANSPOSED"?3+2*x.elements:5);}
  for(const auto&old:p.selections)need(!(old.kind==x.kind&&old.point==x.point&&(x.kind!="GRAPH_EDGE"||old.element==x.element)&&old.layer==x.layer&&old.position==x.position&&old.role==x.role&&old.expert==x.expert&&old.occurrence==x.occurrence),"duplicate bounded selector");p.selections.push_back(x);
 }
 need((p.profile=="G0"||primary>0)&&(p.profile!="R0"||primary==1),"primary profile selection missing");
 size_t group_bound=2*p.selections.size()+2,fp_bound=0;for(const auto&x:p.selections){if(p.worker_fp){group_bound+=x.kind=="RESIDUAL_CVEC"?4:x.kind=="ARITHMETIC"&&x.point=="moe_aggregate"?6:2;fp_bound+=(x.kind=="EXPERT_DISPATCH"||x.kind=="PARAMETER_ROWS"||(graph_point(x.point)&&graph_point(x.point)->op==29))?12:x.kind=="RESIDUAL_CVEC"?8:x.kind=="ARITHMETIC"&&x.point=="moe_aggregate"?12:4;}if(p.io_cache&&x.kind=="EXPERT_DISPATCH")group_bound+=2;if((p.gpu_input_echo||p.shader_trace)&&x.kind=="EXPERT_DISPATCH")++group_bound;}if(p.observer_control)group_bound+=2*p.selections.size();need(group_bound<=64&&fp_bound<=64,"bounded auxiliary group/worker receipt preflight");
 need(weight_bytes<=262144&&weight_rows<=16&&weight_spans<=8&&state_scalars<=1024,"bounded shared mapping caps");
 need(worst_raw<=p.limits.raw&&records<=p.limits.records,"bounded raw/record preflight");
 need(96+records*96+worst_raw+56<=p.limits.bundle,"bounded binary preflight");
 need(owner_static_reserve_bytes+metadata_reserve_bytes+p.limits.raw+p.limits.records*record_reserve_bytes+p.limits.bundle+p.limits.index<=p.limits.owner,"bounded owner preflight");return p;
}
}
