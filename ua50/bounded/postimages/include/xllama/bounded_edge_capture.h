#pragma once
#include "bounded_plan.h"
#include "bounded_index.h"
#include "ggml-backend.h"
namespace xllama::capture {
struct GraphInputContext {Source source;std::vector<uint32_t>params;std::string json()const{return "{\"source\":"+source_json(source)+",\"op_params\":"+numbers(params)+"}";}};
struct GraphGroup {
 uint32_t id=0,position=0,token=0,layer=0,occurrence=0,record=0,element_start=0,element_count=0;std::string arm,point;std::vector<uint32_t>op_params;std::vector<GraphInputContext>input_contexts;
 std::string json()const{std::string inputs="[";for(const auto&i:input_contexts){if(inputs.size()>1)inputs+=',';inputs+=i.json();}inputs+=']';return "{\"id\":"+std::to_string(id)+",\"kind\":\"GRAPH_EDGE\",\"arm\":"+quote(arm)+",\"position\":"+std::to_string(position)+",\"token\":"+std::to_string(token)+",\"layer\":"+std::to_string(layer)+",\"occurrence\":"+std::to_string(occurrence)+",\"point\":"+quote(point)+",\"record\":"+std::to_string(record)+",\"element_start\":"+std::to_string(element_start)+",\"element_count\":"+std::to_string(element_count)+",\"op_params\":"+numbers(op_params)+",\"input_contexts\":"+inputs+"}";}
};
class EdgeCapture {
 struct Registered {Selection selection;ggml_tensor*t=nullptr;bool done=false;uint32_t seen=0;};
 struct Node {ggml_tensor*t;uint32_t id;};
 Owner&owner_;std::vector<Registered> rows_;std::vector<Node>nodes_;std::vector<Metadata>meta_;std::vector<GraphGroup>groups_;
 uint32_t phase_=0,position_=0,token_=0,next_=1;uint64_t context_=0,graph_=0,sequence_=0;bool unavailable_=false;
 uint32_t node(ggml_tensor*t){if(!t)return 0;for(auto&n:nodes_)if(n.t==t)return n.id;need(nodes_.size()<256,"edge node cap");nodes_.push_back({t,owner_.register_node(t,context_,graph_)});return nodes_.back().id;}
 void capture(Registered&r){const auto&s=r.selection;const auto*p=graph_point(s.point);auto*t=r.t;need(p&&t,"registered edge required");
  GraphGroup g;g.id=uint32_t(groups_.size()+1);g.arm=phase_==1?"CPU":"HYBRID";g.point=s.point;g.position=position_;g.token=token_;g.layer=s.layer;g.occurrence=s.occurrence;g.element_start=s.element;g.element_count=s.elements;g.record=owner_.next_id();for(size_t i=0;i<sizeof(t->op_params);++i)g.op_params.push_back(reinterpret_cast<const uint8_t*>(t->op_params)[i]);
  Metadata m;m.record_id=g.record;m.arm=g.arm;m.semantic=s.point;m.layer=s.layer;m.position=position_;m.token=token_;m.occurrence=s.occurrence;m.source.node=node(t);m.source.op=t->op;m.source.type=t->type;m.source.context=context_;m.source.graph=graph_;m.source.view=t->view_offs;for(int i=0;i<4;++i){need(t->ne[i]>0,"edge extent");m.source.ne[i]=t->ne[i];m.source.nb[i]=t->nb[i];}m.event.kind="COMPLETED_PRODUCER";m.event.sequence=sequence_=owner_.next_sequence();m.event.producer=node(t);for(auto*src:t->src)if(src)m.event.deps.push_back(node(src));
  for(auto*input:t->src)if(input){GraphInputContext context;context.source.node=node(input);context.source.op=input->op;context.source.type=input->type;context.source.view=input->view_offs;context.source.context=context_;context.source.graph=graph_;for(int d=0;d<4;++d){need(input->ne[d]>0,"graph input extent");context.source.ne[d]=input->ne[d];context.source.nb[d]=input->nb[d];}for(size_t i=0;i<sizeof(input->op_params);++i)context.params.push_back(reinterpret_cast<const uint8_t*>(input->op_params)[i]);g.input_contexts.push_back(std::move(context));}
  const bool matched=token_==s.token&&uint32_t(t->op)==p->op&&t->type==(p->i32?GGML_TYPE_I32:GGML_TYPE_F32)&&graph_shape(*p,t)&&t->data&&(t->view_src?t->view_src->buffer:t->buffer)&&t->nb[0]==4&&uint64_t(s.element)+s.elements<=uint64_t(ggml_nelements(t));
  if(!matched)owner_.gap(g.record,token_==s.token?Status::layout:Status::token);
  else {m.shape={s.elements};owner_.copy(g.record,p->i32?Type::i32:Type::f32,s.elements,[&](void*d,size_t n){if(ggml_is_contiguous(t)){ggml_backend_tensor_get(t,d,size_t(s.element)*4,n);return true;}need(n==size_t(s.elements)*4,"edge logical window bytes");for(size_t i=0;i<s.elements;++i){size_t logical=s.element+i,offset=0;for(int dim=0;dim<4;++dim){offset=add(offset,mul(logical%size_t(t->ne[dim]),t->nb[dim]));logical/=size_t(t->ne[dim]);}need(offset<=ggml_nbytes(t)&&4<=ggml_nbytes(t)-offset,"edge logical stride extent");ggml_backend_tensor_get(t,static_cast<unsigned char*>(d)+i*4,offset,4);}return true;});if(owner_.records().back().type==Type::none)m.shape.clear();}
  meta_.push_back(std::move(m));groups_.push_back(std::move(g));r.done=true;
 }
public:
 EdgeCapture(const Plan&p,Owner&o):owner_(o){for(const auto&s:p.selections)if(s.kind=="GRAPH_EDGE")rows_.push_back({s});nodes_.reserve(256);meta_.reserve(p.limits.records);groups_.reserve(32);}
 void context(uint32_t phase){phase_=phase;++context_;++graph_;nodes_.clear();for(auto&r:rows_){r.t=nullptr;r.done=false;r.seen=0;}}
 void frame(uint32_t phase,uint32_t pos,uint32_t token){need(phase==phase_,"edge phase");position_=pos;token_=token;graph_=(context_<<32)|(uint64_t(position_)+1);}
 bool event(const ggml_capture_event&e)noexcept{try{auto measurement=owner_.instrument();
  for(auto&r:rows_){const auto*p=graph_point(r.selection.point);need(p,"unknown edge point");
   if(e.kind==GGML_CAPTURE_REGISTER&&!r.done&&e.semantic&&std::string_view(e.semantic)==p->semantic&&(!p->layer||e.layer==int32_t(r.selection.layer)))r.t=e.tensor;
   if(!phase_||r.done||position_!=r.selection.position||e.tensor!=r.t)continue;
   if(e.kind==GGML_CAPTURE_WANT_AFTER)return true;
   if(e.kind==GGML_CAPTURE_COMPLETE){if(r.seen++==r.selection.occurrence)capture(r);}
  }
 }catch(...){unavailable_=true;}return false;}
 const auto&metadata()const{return meta_;}const auto&groups()const{return groups_;}uint64_t sequence()const{return owner_.sequence();}uint64_t next_sequence(){return sequence_=owner_.next_sequence();}bool unavailable()const{return unavailable_;}
 size_t owned_bytes()const{size_t n=sizeof(*this)+rows_.capacity()*sizeof(Registered)+nodes_.capacity()*sizeof(Node)+meta_.capacity()*sizeof(Metadata)+groups_.capacity()*sizeof(GraphGroup);for(const auto&g:groups_){n+=g.op_params.capacity()*sizeof(uint32_t)+g.input_contexts.capacity()*sizeof(GraphInputContext);for(const auto&i:g.input_contexts)n+=i.params.capacity()*sizeof(uint32_t);}return n;}
};
}
