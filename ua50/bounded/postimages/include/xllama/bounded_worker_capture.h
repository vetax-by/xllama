#pragma once
#include "bounded_plan.h"
#include "bounded_index.h"
#include "ggml-cpu.h"
namespace xllama::capture {
struct WorkerGroup {
 struct Sample{ggml_capture_worker_event value;uint64_t sequence=0;};
 uint32_t id=0,position=0,token=0,layer=0,occurrence=0,selector=0;std::string arm,point,endpoint,status="INCOMPLETE_STAGE_COVERAGE",stop_reason="NONE";uint32_t sample_limit=0;bool complete=false,capture_failed=false;Source source;std::vector<Sample>samples;
 std::string json()const{static const char*stages[]={"QUERY","NODE_BEFORE","NODE_AFTER","PACK_BEFORE","PACK_AFTER","DOT_BEFORE","DOT_AFTER","GPU_HOST_BEFORE","GPU_HOST_AFTER"};std::string rows="[";for(const auto&sample:samples){const auto&e=sample.value;if(rows.size()>1)rows+=',';rows+="{\"sequence\":"+std::to_string(sample.sequence)+",\"stage\":"+quote(stages[e.stage])+",\"worker_index\":"+std::to_string(e.worker_index)+",\"worker_count\":"+std::to_string(e.worker_count)+",\"valid_mask\":"+std::to_string(e.valid_mask)+",\"rounding_mode\":"+std::to_string(e.rounding_mode)+",\"exception_flags\":"+std::to_string(e.exception_flags)+",\"mxcsr\":"+std::to_string(e.mxcsr)+",\"x87_control\":"+std::to_string(e.x87_control)+",\"fused_following\":"+std::to_string(e.fused_following)+",\"expert\":"+std::to_string(e.expert)+"}";}rows+=']';std::string out="{\"id\":"+std::to_string(id)+",\"kind\":\"WORKER_FP\",\"arm\":"+quote(arm)+",\"position\":"+std::to_string(position)+",\"token\":"+std::to_string(token)+",\"layer\":"+std::to_string(layer)+",\"occurrence\":"+std::to_string(occurrence)+",\"point\":"+quote(point)+",\"selector_index\":"+std::to_string(selector)+",\"endpoint\":"+quote(endpoint)+",\"status\":"+quote(status)+",\"source\":"+source_json(source)+",\"sample_limit\":"+std::to_string(sample_limit)+",\"complete\":"+(complete?"true":"false")+",\"capture_failed\":"+(capture_failed?"true":"false")+",\"stop_reason\":"+quote(stop_reason)+",\"samples\":"+rows+"}";need(out.size()<=worker_index_group_bound+samples.size()*worker_index_sample_bound,"worker declared JSON bound");return out;}
};
class WorkerCapture {
 struct Binding{ggml_tensor*t;uint32_t index;const char*point;const char*endpoint;};
 struct Row{Selection selected;uint32_t index=0;std::string point,endpoint;ggml_tensor*t=nullptr;std::array<uint32_t,64>seen{};std::array<bool,64>active{};size_t group=SIZE_MAX;bool pack=false;uint32_t dot_experts=0;};
 static constexpr size_t binding_cap=64;
 Plan plan_;Owner&owner_;std::vector<Row>rows_;std::vector<WorkerGroup>groups_;uint32_t phase_=0,position_=0,token_=0;uint64_t context_=0,graph_=0;size_t samples_=0;bool failed_=false;
 std::vector<Binding> building_,built_;
 const ggml_cgraph* built_graph_=nullptr;
 bool building_overflow_=false;
 Source source(ggml_tensor*t){Source s;s.node=owner_.register_node(t,context_,graph_);s.op=t->op;s.type=t->type;s.view=t->view_offs;s.context=context_;s.graph=graph_;for(int i=0;i<4;++i){need(t->ne[i]>0,"worker source extent");s.ne[i]=t->ne[i];s.nb[i]=t->nb[i];}return s;}
 void bind(uint32_t index,const Selection&sel,ggml_tensor*t,const std::string&point,const std::string&endpoint){if(!t)return;for(const auto&r:rows_)if(r.index==index&&r.endpoint==endpoint&&r.t==t)return;need(rows_.size()<64,"worker registered endpoint cap");Row row;row.index=index;row.selected=sel;row.t=t;row.point=point;row.endpoint=endpoint;rows_.push_back(std::move(row));}
 bool selected(const Selection&sel)const{return position_==sel.position&&token_==sel.token;}
 void register_endpoint(uint32_t index,ggml_tensor*t,const char*point,const char*endpoint){
  if(!t)return;
  const auto&sel=plan_.selections[index];
  // Keep existing registration for a freshly built selected frame.
  if(selected(sel))bind(index,sel,t,point,endpoint);
  for(const auto&b:building_)if(b.index==index&&b.t==t&&std::string_view(b.endpoint)==endpoint)return;
  if(building_.size()==binding_cap){building_overflow_=true;return;}
  building_.push_back({t,index,point,endpoint});
 }
 void logical_graph(const ggml_capture_event&e){
  if(e.kind==GGML_CAPTURE_LOGICAL_GRAPH){
   // Emitted after the build, including rebuilds that reuse a graph address.
   built_.clear();
   built_graph_=nullptr;
   if(!e.graph||building_overflow_)return;
   built_.swap(building_);
   built_graph_=e.graph;
   return;
  }
  if(!e.graph||e.graph!=built_graph_){
   built_.clear();
   built_graph_=nullptr;
   rows_.clear();
   return;
  }
  // LOGICAL_REFERENCE also runs on reuse. Receipt epochs and stage counters
  // remain frame-local. The cached pointers only identify tensors in this graph.
  for(const auto&b:built_){
   const auto&sel=plan_.selections[b.index];
   if(selected(sel))bind(b.index,sel,b.t,b.point,b.endpoint);
  }
 }
 void stop(const char*reason){
  failed_=true;for(auto&g:groups_){g.capture_failed=true;g.stop_reason=reason;if(!g.complete)g.status=std::string(reason)=="SAMPLE_LIMIT"||std::string(reason)=="OWNER_BUDGET"?"BUDGET_EXHAUSTED":"INCOMPLETE_STAGE_COVERAGE";}
 }
 bool complete(const Row&r,const WorkerGroup&g)const{
  if(g.samples.size()<2||g.samples.front().value.stage!=GGML_CAPTURE_WORKER_NODE_BEFORE||g.samples.back().value.stage!=GGML_CAPTURE_WORKER_NODE_AFTER)return false;
  uint32_t node_before=0,node_after=0,pack_before=0,pack_after=0,experts=0;
  std::array<uint8_t,65>dot{},gpu{};bool packed=!r.pack;
  for(size_t i=0;i<g.samples.size();++i){const auto&e=g.samples[i].value;
   if(e.worker_count!=1||e.worker_index||e.stage<1||e.stage>8)return false;
   if(e.stage==GGML_CAPTURE_WORKER_NODE_BEFORE){if(i||++node_before!=1)return false;}
   else if(e.stage==GGML_CAPTURE_WORKER_NODE_AFTER){if(i+1!=g.samples.size()||++node_after!=1)return false;}
   else if(e.stage==GGML_CAPTURE_WORKER_PACK_BEFORE){if(!r.pack||pack_before++||pack_after||experts)return false;}
   else if(e.stage==GGML_CAPTURE_WORKER_PACK_AFTER){if(!r.pack||pack_before!=1||pack_after++)return false;packed=true;}
   else{
    if(!packed||!r.dot_experts)return false;
    const uint32_t key=e.expert==UINT32_MAX?64:e.expert;if(key>64)return false;
    if(r.selected.kind=="EXPERT_DISPATCH"?key!=r.selected.expert:(r.t->op==GGML_OP_MUL_MAT?key!=64:key==64))return false;
    const bool gpu_stage=e.stage==GGML_CAPTURE_WORKER_GPU_HOST_BEFORE||e.stage==GGML_CAPTURE_WORKER_GPU_HOST_AFTER;
    auto&state=gpu_stage?gpu[key]:dot[key];auto other=gpu_stage?dot[key]:gpu[key];if(other)return false;
    const bool before=e.stage==GGML_CAPTURE_WORKER_DOT_BEFORE||e.stage==GGML_CAPTURE_WORKER_GPU_HOST_BEFORE;
    if(before){if(state)return false;state=1;}else{if(state!=1)return false;state=2;++experts;}
   }
  }
  for(size_t k=0;k<65;++k)if(dot[k]==1||gpu[k]==1)return false;
  return node_before==1&&node_after==1&&pack_before==uint32_t(r.pack)&&pack_after==uint32_t(r.pack)&&experts==r.dot_experts;
 }

public:
 WorkerCapture(const Plan&p,Owner&o):plan_(p),owner_(o){need(p.worker_events<=256&&(!p.worker_fp||p.worker_events),"worker selected event budget");rows_.reserve(binding_cap);groups_.reserve(64);building_.reserve(binding_cap);built_.reserve(binding_cap);}
 void context(uint32_t phase){phase_=phase;++context_;++graph_;rows_.clear();building_.clear();built_.clear();built_graph_=nullptr;building_overflow_=false;}
 void frame(uint32_t pos,uint32_t token){position_=pos;token_=token;graph_=(context_<<32)|(uint64_t(position_)+1);rows_.clear();building_.clear();building_overflow_=false;}
 void registration(const ggml_capture_event&e){
  if(!plan_.worker_fp||!phase_)return;
  if(e.kind==GGML_CAPTURE_LOGICAL_GRAPH||e.kind==GGML_CAPTURE_LOGICAL_REFERENCE){logical_graph(e);return;}
  if(e.kind!=GGML_CAPTURE_REGISTER&&e.kind!=GGML_CAPTURE_CVEC&&e.kind!=GGML_CAPTURE_STATE_WRITE)return;
  for(uint32_t i=0;i<plan_.selections.size();++i){const auto&s=plan_.selections[i];
  if(s.kind=="RESIDUAL_CVEC"){if(e.layer!=int32_t(s.layer))continue;if(e.kind==GGML_CAPTURE_REGISTER&&e.semantic&&std::string_view(e.semantic)=="model.layers.{}.post_ffn_residual")register_endpoint(i,e.tensor,"post_ffn_residual","NODE");if(e.kind==GGML_CAPTURE_CVEC&&e.related[0]!=e.tensor)register_endpoint(i,e.related[0],"post_ffn_residual","CVEC_ADD");continue;}
  if(s.kind=="KV_K"||s.kind=="KV_V_TRANSPOSED"||s.kind=="SHORTCONV"){if(e.layer!=int32_t(s.layer))continue;if(e.kind==GGML_CAPTURE_STATE_WRITE&&e.semantic){const std::string_view label(e.semantic);if((s.kind=="KV_K"&&label=="@capture.kv.K.write")||(s.kind=="KV_V_TRANSPOSED"&&label=="@capture.kv.V.write")||(s.kind=="SHORTCONV"&&label=="@capture.shortconv.write"))register_endpoint(i,e.tensor,s.kind.c_str(),"NODE");}continue;}
  const std::string_view point=s.kind=="PARAMETER_ROWS"?(s.point=="conv_kernel"?"conv_inner_out":"result_output"):s.kind=="EXPERT_DISPATCH"?(s.role%3==0?"moe_gate":s.role%3==1?"moe_down":"moe_up"):std::string_view(s.point);const auto*p=graph_point(point);if(!p||(p->layer?e.layer!=int32_t(s.layer):e.layer!=-1)||e.kind!=GGML_CAPTURE_REGISTER||!e.semantic||std::string_view(e.semantic)!=p->semantic)continue;
  if(s.kind=="ARITHMETIC"&&point=="moe_aggregate"){auto*last=e.tensor;auto*middle=last?last->src[0]:nullptr;auto*first=middle?middle->src[0]:nullptr;if(!first||first->op!=GGML_OP_ADD||middle->op!=GGML_OP_ADD||last->op!=GGML_OP_ADD){need(!selected(s),"worker slot ADD chain");continue;}register_endpoint(i,first,p->point,"SLOT_ADD_0");register_endpoint(i,middle,p->point,"SLOT_ADD_1");register_endpoint(i,last,p->point,"SLOT_ADD_2");}else register_endpoint(i,e.tensor,p->point,"NODE");
 }}

 bool event(const ggml_capture_worker_event&e){
  auto measured=owner_.instrument();
  if(!plan_.worker_fp||failed_)return false;bool selected=false;
  for(auto&r:rows_){
   if(e.tensor!=r.t)continue;
   if(e.worker_count!=1||e.worker_index){stop("UNSUPPORTED_WORKER_COUNT");return false;}
   const auto stage=e.stage?e.stage:e.requested_stage;
   if(stage<1||stage>8){stop("INVALID_STAGE_SEQUENCE");return false;}
   const bool before=stage==GGML_CAPTURE_WORKER_NODE_BEFORE;
   if(!e.stage&&before)r.active[0]=r.seen[0]++==r.selected.occurrence;
   if(!r.active[0])continue;
   if(stage>=GGML_CAPTURE_WORKER_DOT_BEFORE&&r.selected.kind=="EXPERT_DISPATCH"&&e.expert!=r.selected.expert)continue;
   selected=true;if(!e.stage)continue;
   if(r.group==SIZE_MAX){
    WorkerGroup g;g.id=uint32_t(groups_.size()+1);g.arm=phase_==1?"CPU":"HYBRID";g.position=position_;g.token=token_;g.layer=r.selected.layer;g.occurrence=r.selected.occurrence;g.selector=r.index;g.point=r.point;g.endpoint=r.endpoint;g.source=source(r.t);g.sample_limit=worker_samples_per_endpoint(r.selected);
    const bool mm=r.t->op==GGML_OP_MUL_MAT||r.t->op==GGML_OP_MUL_MAT_ID;
    r.pack=mm&&r.t->src[0]&&r.t->src[1]&&r.t->src[0]->type!=GGML_TYPE_F32&&r.t->src[1]->type==GGML_TYPE_F32;
    r.dot_experts=mm?(r.t->op==GGML_OP_MUL_MAT_ID&&r.selected.kind!="EXPERT_DISPATCH"?4:1):0;
    need(groups_.size()<64,"worker group cap");g.samples.reserve(g.sample_limit);groups_.push_back(std::move(g));r.group=groups_.size()-1;
   }
   auto&g=groups_[r.group];
   if(samples_>=plan_.worker_events||g.samples.size()>=g.sample_limit){stop("SAMPLE_LIMIT");return false;}
   // Charge callback query+sample cost conservatively once per retained event;
   // owner instrumentation also charges cumulative measured elapsed, never idle.
   if(!owner_.reserve_work(2*sizeof(e))){stop("OWNER_BUDGET");return false;}
   auto retained=e;retained.tensor=nullptr;g.samples.push_back({retained,owner_.next_sequence()});++samples_;
   if(stage==GGML_CAPTURE_WORKER_NODE_AFTER){
    r.active[0]=false;g.complete=complete(r,g);g.status=g.complete?"CAPTURED":"INCOMPLETE_STAGE_COVERAGE";
    if(!g.complete){stop("INVALID_STAGE_SEQUENCE");return false;}
   }
  }
  return selected;
 }
 bool failed()const{if(failed_)return true;for(const auto&g:groups_)if(!g.complete)return true;return false;}
 size_t sample_count()const{return samples_;}

 size_t owned_bytes()const{size_t n=sizeof(*this)+rows_.capacity()*sizeof(Row)+groups_.capacity()*sizeof(WorkerGroup)+(building_.capacity()+built_.capacity())*sizeof(Binding);for(const auto&g:groups_)n+=g.samples.capacity()*sizeof(WorkerGroup::Sample);return n;}
 const auto&groups()const{return groups_;}
};
}
