#pragma once
#include "bounded_plan.h"
#include "bounded_index.h"
#include "ggml-cpu.h"
namespace xllama::capture {
struct WorkerGroup {
 struct Sample{ggml_capture_worker_event value;uint64_t sequence=0;};
 uint32_t id=0,position=0,token=0,layer=0,occurrence=0,selector=0;std::string arm,point,endpoint,status="CAPTURED";Source source;std::vector<Sample>samples;
 std::string json()const{static const char*stages[]={"QUERY","NODE_BEFORE","NODE_AFTER","PACK_BEFORE","PACK_AFTER","DOT_BEFORE","DOT_AFTER","GPU_HOST_BEFORE","GPU_HOST_AFTER"};std::string rows="[";for(const auto&sample:samples){const auto&e=sample.value;if(rows.size()>1)rows+=',';rows+="{\"sequence\":"+std::to_string(sample.sequence)+",\"stage\":"+quote(stages[e.stage])+",\"worker_index\":"+std::to_string(e.worker_index)+",\"worker_count\":"+std::to_string(e.worker_count)+",\"valid_mask\":"+std::to_string(e.valid_mask)+",\"rounding_mode\":"+std::to_string(e.rounding_mode)+",\"exception_flags\":"+std::to_string(e.exception_flags)+",\"mxcsr\":"+std::to_string(e.mxcsr)+",\"x87_control\":"+std::to_string(e.x87_control)+",\"fused_following\":"+std::to_string(e.fused_following)+",\"expert\":"+std::to_string(e.expert)+"}";}rows+=']';return "{\"id\":"+std::to_string(id)+",\"kind\":\"WORKER_FP\",\"arm\":"+quote(arm)+",\"position\":"+std::to_string(position)+",\"token\":"+std::to_string(token)+",\"layer\":"+std::to_string(layer)+",\"occurrence\":"+std::to_string(occurrence)+",\"point\":"+quote(point)+",\"selector_index\":"+std::to_string(selector)+",\"endpoint\":"+quote(endpoint)+",\"status\":"+quote(status)+",\"source\":"+source_json(source)+",\"samples\":"+rows+"}";}
};
class WorkerCapture {
 struct Row{Selection selected;uint32_t index=0;std::string point,endpoint;ggml_tensor*t=nullptr;std::array<uint32_t,64>seen{};std::array<bool,64>active{};size_t group=SIZE_MAX;};
 Plan plan_;Owner&owner_;std::vector<Row>rows_;std::vector<WorkerGroup>groups_;uint32_t phase_=0,position_=0,token_=0;uint64_t context_=0,graph_=0;size_t samples_=0;bool failed_=false;
 Source source(ggml_tensor*t){Source s;s.node=owner_.register_node(t,context_,graph_);s.op=t->op;s.type=t->type;s.view=t->view_offs;s.context=context_;s.graph=graph_;for(int i=0;i<4;++i){need(t->ne[i]>0,"worker source extent");s.ne[i]=t->ne[i];s.nb[i]=t->nb[i];}return s;}
 void bind(uint32_t index,const Selection&sel,ggml_tensor*t,const std::string&point,const std::string&endpoint){if(!t)return;for(const auto&r:rows_)if(r.index==index&&r.endpoint==endpoint&&r.t==t)return;need(rows_.size()<64,"worker registered endpoint cap");Row row;row.index=index;row.selected=sel;row.t=t;row.point=point;row.endpoint=endpoint;rows_.push_back(std::move(row));}
public:
 WorkerCapture(const Plan&p,Owner&o):plan_(p),owner_(o){rows_.reserve(64);groups_.reserve(64);}
 void context(uint32_t phase){phase_=phase;++context_;++graph_;rows_.clear();}
 void frame(uint32_t pos,uint32_t token){position_=pos;token_=token;graph_=(context_<<32)|(uint64_t(position_)+1);rows_.clear();}
 void registration(const ggml_capture_event&e){if(!plan_.worker_fp||!phase_)return;for(size_t i=0;i<plan_.selections.size();++i){const auto&s=plan_.selections[i];if(position_!=s.position||token_!=s.token)continue;
  if(s.kind=="RESIDUAL_CVEC"){if(e.layer!=int32_t(s.layer))continue;if(e.kind==GGML_CAPTURE_REGISTER&&e.semantic&&std::string_view(e.semantic)=="model.layers.{}.post_ffn_residual")bind(i,s,e.tensor,"post_ffn_residual","NODE");if(e.kind==GGML_CAPTURE_CVEC&&e.related[0]!=e.tensor)bind(i,s,e.related[0],"post_ffn_residual","CVEC_ADD");continue;}
  if(s.kind=="KV_K"||s.kind=="KV_V_TRANSPOSED"||s.kind=="SHORTCONV"){if(e.layer!=int32_t(s.layer))continue;if(e.kind==GGML_CAPTURE_STATE_WRITE&&e.semantic){const std::string_view label(e.semantic);if((s.kind=="KV_K"&&label=="@capture.kv.K.write")||(s.kind=="KV_V_TRANSPOSED"&&label=="@capture.kv.V.write")||(s.kind=="SHORTCONV"&&label=="@capture.shortconv.write"))bind(i,s,e.tensor,s.kind,"NODE");}continue;}
  const std::string point=s.kind=="PARAMETER_ROWS"?(s.point=="conv_kernel"?"conv_inner_out":"result_output"):s.kind=="EXPERT_DISPATCH"?(s.role%3==0?"moe_gate":s.role%3==1?"moe_down":"moe_up"):s.point;const auto*p=graph_point(point);if(!p||(p->layer?e.layer!=int32_t(s.layer):e.layer!=-1)||e.kind!=GGML_CAPTURE_REGISTER||!e.semantic||std::string_view(e.semantic)!=p->semantic)continue;
  if(s.kind=="ARITHMETIC"&&point=="moe_aggregate"){auto*last=e.tensor;auto*middle=last?last->src[0]:nullptr;auto*first=middle?middle->src[0]:nullptr;need(first&&first->op==GGML_OP_ADD&&middle->op==GGML_OP_ADD&&last->op==GGML_OP_ADD,"worker slot ADD chain");bind(i,s,first,point,"SLOT_ADD_0");bind(i,s,middle,point,"SLOT_ADD_1");bind(i,s,last,point,"SLOT_ADD_2");}else bind(i,s,e.tensor,point,"NODE");
 }}
 bool event(const ggml_capture_worker_event&e){if(!plan_.worker_fp||failed_)return false;need(e.worker_count&&e.worker_count<=64&&e.worker_index<e.worker_count,"actual worker tuple");bool selected=false;for(auto&r:rows_){if(e.tensor!=r.t)continue;const auto stage=e.stage?e.stage:e.requested_stage;const bool before=stage==GGML_CAPTURE_WORKER_NODE_BEFORE;if(!e.stage&&before){r.active[e.worker_index]=r.seen[e.worker_index]++==r.selected.occurrence;}const bool active=r.active[e.worker_index];if(!active)continue;if(stage>=GGML_CAPTURE_WORKER_DOT_BEFORE&&r.selected.kind=="EXPERT_DISPATCH"&&e.expert!=r.selected.expert)continue;selected=true;if(!e.stage)continue;
  if(r.group==SIZE_MAX){WorkerGroup g;g.id=uint32_t(groups_.size()+1);g.arm=phase_==1?"CPU":"HYBRID";g.position=position_;g.token=token_;g.layer=r.selected.layer;g.occurrence=r.selected.occurrence;g.selector=r.index;g.point=r.point;g.endpoint=r.endpoint;g.source=source(r.t);need(groups_.size()<64,"worker group cap");groups_.push_back(std::move(g));r.group=groups_.size()-1;}
  auto&g=groups_[r.group];if(samples_>=64||!owner_.reserve_work(sizeof(e))){g.status="BUDGET_EXHAUSTED";failed_=true;return false;}auto retained=e;retained.tensor=nullptr;g.samples.push_back({retained,owner_.next_sequence()});++samples_;if(stage==GGML_CAPTURE_WORKER_NODE_AFTER)r.active[e.worker_index]=false;
 }return selected;}
 size_t owned_bytes()const{size_t n=sizeof(*this)+rows_.capacity()*sizeof(Row)+groups_.capacity()*sizeof(WorkerGroup);for(const auto&g:groups_)n+=g.samples.capacity()*sizeof(WorkerGroup::Sample);return n;}
 const auto&groups()const{return groups_;}
};
}
