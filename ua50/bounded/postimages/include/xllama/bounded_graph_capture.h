#pragma once
#include "bounded_plan.h"
#include "bounded_index.h"
#include "bounded_state_capture.h"
#include "bounded_edge_capture.h"
#include "bounded_expert_capture.h"
#include "bounded_arithmetic_capture.h"
#include "bounded_worker_capture.h"
#include "bounded_observer_capture.h"
#include "bounded_parameter_capture.h"
#include <memory>
#include <variant>
#include "ggml-backend.h"
#include <mutex>
namespace xllama::capture {
/* Context-owned event adapter. Registration is semantic, not a tensor-name
 * lookup at execution time. Raw pointers are used only during their context. */
struct IndexGroup {
 std::variant<ResidualGroup,StateGroup,GraphGroup,ExpertGroup,LifecycleGroup,ArithmeticGroup,CacheEvidence,WorkerGroup,ObserverGroup,GpuEvidenceGroup,ParameterGroup> value;
 template<class T>IndexGroup(T g):value(std::move(g)){}
 std::string json()const{return std::visit([](const auto&g){return g.json();},value);}
};
class GraphCapture {
 struct Node {ggml_tensor*t;uint32_t id;};
 Plan plan_;Owner owner_;std::unique_ptr<WorkerCapture>worker_;std::unique_ptr<ObserverCapture>observer_;std::unique_ptr<ParameterCapture>parameter_;std::unique_ptr<StateCapture> state_;std::unique_ptr<EdgeCapture> edge_;std::shared_ptr<ExpertCapture> expert_;std::unique_ptr<ArithmeticCapture> arithmetic_;mutable std::vector<Metadata> merged_;std::vector<Node> nodes_;std::vector<Metadata> meta_;std::vector<ResidualGroup> groups_;
 uint32_t phase_=0,position_=0,token_=0,next_record_=1,occurrence_seen_=0;uint64_t context_=0,graph_=0,sequence_=0;
 ggml_tensor*add_=nullptr,*out_=nullptr,*a_=nullptr,*b_=nullptr,*direction_=nullptr;
 const void*state_owner_=nullptr;ggml_capture_state_reader state_reader_=nullptr;
 ggml_capture_cvec_state cvec_start_{};ResidualGroup group_;bool entered_=false,copied_=false,after_=false,unavailable_=false;
 std::mutex mutex_;std::vector<LifecycleGroup> lifecycle_;LifecycleGroup reset_;
 uint64_t next_sequence(){if(expert_)return expert_->next_sequence();if(edge_)return edge_->next_sequence();if(state_)return state_->next_sequence();return sequence_=owner_.next_sequence();}
 bool selected()const{return plan_.profile=="R0"&&phase_>=1&&phase_<=2&&position_==plan_.selections[0].position;}
 uint32_t node(ggml_tensor*t){if(!t)return 0;for(const auto&n:nodes_)if(n.t==t)return n.id;need(nodes_.size()<256,"capture registry node cap");const uint32_t id=owner_.register_node(t,context_,graph_);nodes_.push_back({t,id});return id;}
 Source source(ggml_tensor*t){Source s;if(!t)return s;s.node=node(t);s.op=uint32_t(t->op);s.type=uint32_t(t->type);for(size_t i=0;i<4;++i){need(t->ne[i]>=0,"negative capture extent");s.ne[i]=uint64_t(t->ne[i]);s.nb[i]=t->nb[i];}s.view=t->view_offs;s.context=context_;s.graph=graph_;return s;}
 Metadata metadata(ggml_tensor*t,const char*semantic,const char*event,uint32_t consumer){Metadata m;m.semantic=semantic;m.arm=phase_==1?"CPU":"HYBRID";m.position=position_;m.token=token_;m.layer=plan_.selections[0].layer;m.occurrence=plan_.selections[0].occurrence;m.source=source(t);m.event.kind=event;m.event.sequence=sequence_=owner_.next_sequence();m.event.producer=m.source.node;m.event.consumer=consumer;if(t)for(auto*src:t->src)if(src){need(m.event.deps.size()<16,"capture dependency cap");m.event.deps.push_back(node(src));}return m;}
 uint32_t snapshot(ggml_tensor*t,const char*semantic,const char*event,uint32_t consumer){
  const uint32_t id=owner_.next_id();auto m=metadata(t,semantic,event,consumer);m.record_id=id;
  if(!t||t->type!=GGML_TYPE_F32||t->ne[0]!=2048||t->ne[1]!=1||t->ne[2]!=1||t->ne[3]!=1||!ggml_is_contiguous(t)||t->nb[0]!=4||ggml_nbytes(t)!=8192||!t->data||!(t->view_src?t->view_src->buffer:t->buffer))owner_.gap(id,Status::layout);
  else{m.shape={2048};owner_.copy(id,Type::f32,2048,[t](void*dst,size_t n){ggml_backend_tensor_get(t,dst,0,n);return true;});}
  if(owner_.records().back().type==Type::none)m.shape.clear();meta_.push_back(std::move(m));return id;
 }
 bool state(ggml_capture_cvec_state&out)const{return state_reader_&&state_owner_&&state_reader_(state_owner_,int32_t(plan_.selections[0].layer),nullptr,0,&out);}
 void before(ggml_tensor*t){
  if(entered_){unavailable_=true;return;}entered_=true;group_={};group_.id=uint32_t(groups_.size()+1);group_.arm=phase_==1?"CPU":"HYBRID";group_.position=position_;group_.token=token_;group_.layer=plan_.selections[0].layer;group_.occurrence=plan_.selections[0].occurrence;group_.state_epoch=context_;
  need(token_==plan_.selections[0].token,"capture selected token mismatch");
  // Reject unregistered scheduler replacement rather than guessing from shape.
  need(t==add_&&t->op==GGML_OP_ADD&&t->src[0]==a_&&t->src[1]==b_,"capture actual ADD binding/backend mapping");
  group_.add=node(t);group_.src0=node(a_);group_.src1=node(b_);
  group_.A=snapshot(a_,"residual.A","READY_BEFORE_WRITE",group_.add);group_.B=snapshot(b_,"residual.B","READY_BEFORE_WRITE",group_.add);
  if(state(cvec_start_)){
   group_.branch=cvec_start_.applied?"APPLIED":"NOOP";group_.range_start=cvec_start_.range_start;group_.range_end=cvec_start_.range_end;direction_=cvec_start_.direction;group_.direction=node(direction_);
   if(cvec_start_.applied)need(out_&&out_->op==GGML_OP_ADD&&out_->src[0]==add_&&out_->src[1]==direction_,"capture actual cvec ADD dependencies");else need(out_==add_&&!direction_,"capture actual cvec NOOP alias");
   if(cvec_start_.applied){const uint32_t id=owner_.next_id();auto m=metadata(direction_,"cvec.V","READY_BEFORE_WRITE",node(out_));m.record_id=id;m.shape={2048};ggml_capture_cvec_state copied{};
    owner_.copy(id,Type::f32,2048,[&](void*dst,size_t n){return state_reader_(state_owner_,int32_t(group_.layer),dst,n,&copied)&&copied.revision==cvec_start_.revision&&copied.direction==direction_;});if(owner_.records().back().type==Type::none)m.shape.clear();meta_.push_back(std::move(m));group_.V=id;}
  }
  copied_=true;
 }
 void complete(ggml_tensor*t){
  if(!copied_)return;
  if(t==add_&&!after_){after_=true;group_.R=snapshot(t,"residual.R","AFTER_COMPUTE",0);
   if(out_==add_&&group_.branch=="NOOP"){
    group_.O=owner_.next_id();auto m=metadata(t,"residual.O","AFTER_COMPUTE",0);m.record_id=group_.O;m.shape={2048};owner_.alias(group_.O,group_.R);if(owner_.records().back().type==Type::none)m.shape.clear();meta_.push_back(std::move(m));finish_group();
   }
  }
  if(t==out_&&out_!=add_&&after_&&!group_.O){group_.O=snapshot(t,"residual.O","AFTER_COMPUTE",0);finish_group();}
 }
 void finish_group(){ggml_capture_cvec_state end{};group_.stable=state(end)&&end.revision==cvec_start_.revision&&end.direction==cvec_start_.direction&&end.applied==cvec_start_.applied&&end.range_start==cvec_start_.range_start&&end.range_end==cvec_start_.range_end;groups_.push_back(group_);}
public:
 GraphCapture(Plan p,Hash hash,std::string model={},std::string packer={}):plan_(std::move(p)),owner_(plan_.limits,std::move(hash)){
  need(!plan_.selections.empty(),"capture selection missing");if(plan_.observer_control)observer_=std::make_unique<ObserverCapture>(plan_,owner_);if(plan_.worker_fp)worker_=std::make_unique<WorkerCapture>(plan_,owner_);nodes_.reserve(256);meta_.reserve(plan_.limits.records);groups_.reserve(16);if(plan_.profile=="S0")state_=std::make_unique<StateCapture>(plan_,owner_);merged_.reserve(plan_.limits.records);bool have_edges=false;for(const auto&s:plan_.selections)have_edges|=s.kind=="GRAPH_EDGE";if(have_edges)edge_=std::make_unique<EdgeCapture>(plan_,owner_);if(plan_.profile=="G0")arithmetic_=std::make_unique<ArithmeticCapture>(plan_,owner_);bool have_parameters=false;for(const auto&s:plan_.selections)have_parameters|=s.kind=="PARAMETER_ROWS";if(have_parameters)parameter_=std::make_unique<ParameterCapture>(plan_,owner_,model);if(plan_.profile=="E0")expert_=std::make_shared<ExpertCapture>(plan_,owner_,std::move(model),std::move(packer));
 }
 void context(uint32_t phase){std::lock_guard<std::mutex> lock(mutex_);if(parameter_)parameter_->context(phase);if(worker_)worker_->context(phase);if(observer_)observer_->context(phase);if(state_)state_->context(phase);if(edge_)edge_->context(phase);if(arithmetic_)arithmetic_->context(phase);need(phase==1||phase==2,"capture phase");phase_=phase;++context_;++graph_;nodes_.clear();add_=out_=a_=b_=direction_=nullptr;state_owner_=nullptr;state_reader_=nullptr;entered_=copied_=after_=false;occurrence_seen_=0;group_={};}
 void frame(uint32_t phase,uint32_t position,int32_t token){std::lock_guard<std::mutex> lock(mutex_);need(phase==phase_&&token>=0,"capture frame");position_=position;token_=uint32_t(token);graph_=(context_<<32)|(uint64_t(position_)+1);nodes_.clear();if(parameter_)parameter_->frame(phase,position_,token_);if(worker_)worker_->frame(position_,token_);if(observer_)observer_->frame(position_,token_);if(state_)state_->frame(phase,position,uint32_t(token));if(edge_)edge_->frame(phase,position,uint32_t(token));if(arithmetic_)arithmetic_->frame(phase,position,uint32_t(token));}
 bool event(const ggml_capture_event&e)noexcept{
  try{std::lock_guard<std::mutex> lock(mutex_);auto measurement=owner_.instrument();if(observer_)observer_->event(e);if(worker_)worker_->registration(e);
   if(e.kind==GGML_CAPTURE_RESET_BEGIN){need(!reset_.request,"duplicate lifecycle reset");reset_={};reset_.id=uint32_t(lifecycle_.size()+1);reset_.arm=phase_==1?"CPU":"HYBRID";reset_.context=context_;reset_.previous=context_?context_-1:0;reset_.request=next_sequence();reset_.requested=e.values[0]==1;return false;}
   if(e.kind==GGML_CAPTURE_RESET_COMPLETE){need(reset_.request&&reset_.context==context_,"unmatched lifecycle reset");reset_.completion=next_sequence();reset_.observed=false;need(lifecycle_.size()<32,"lifecycle group cap");lifecycle_.push_back(reset_);reset_={};return false;}
   bool want=false;if(parameter_)want|=parameter_->event(e);if(state_)want|=state_->event(e);if(edge_)want|=edge_->event(e);if(arithmetic_)want|=arithmetic_->event(e);if(plan_.profile!="R0")return want;
   const auto&sel=plan_.selections[0];
   if(e.kind==GGML_CAPTURE_REGISTER&&e.layer==int32_t(sel.layer)&&e.semantic){
    if(group_.O)return false;const std::string_view label(e.semantic);if(label=="model.layers.{}.post_ffn_residual"){if(add_!=e.tensor)nodes_.clear();add_=e.tensor;a_=add_->src[0];b_=add_->src[1];node(add_);node(a_);node(b_);}if(label=="l_out")out_=e.tensor;
   }
   if(e.kind==GGML_CAPTURE_CVEC&&!group_.O&&e.layer==int32_t(sel.layer)){need(e.tensor==add_,"capture cvec input binding");out_=e.related[0];state_owner_=e.state_owner;state_reader_=e.state_reader;}
   if(!selected()||unavailable_)return want;
   if(e.kind==GGML_CAPTURE_WANT_BEFORE)return e.tensor==add_||want;
   if(e.kind==GGML_CAPTURE_WANT_AFTER)return e.tensor==add_||e.tensor==out_||want;
   if(e.kind==GGML_CAPTURE_READY_BEFORE_WRITE&&e.tensor==add_){if(occurrence_seen_++==sel.occurrence)before(e.tensor);}
   if(e.kind==GGML_CAPTURE_COMPLETE)complete(e.tensor);return want;
  }catch(...){unavailable_=true;}return false;
 }
 static bool worker_callback(const ggml_capture_worker_event*e,void*u)noexcept{if(!e||!u)return false;auto*self=static_cast<GraphCapture*>(u);try{std::lock_guard<std::mutex>lock(self->mutex_);auto measured=self->owner_.instrument();return self->worker_&&self->worker_->event(*e);}catch(...){self->unavailable_=true;return false;}}
 static bool callback(const ggml_capture_event*e,void*u)noexcept{return e&&u?static_cast<GraphCapture*>(u)->event(*e):false;}
 std::vector<IndexGroup>all_groups()const{std::vector<IndexGroup> out;
  auto append=[&](const auto&rows){for(auto row:rows){row.id=uint32_t(out.size()+1);out.emplace_back(std::move(row));}};
  if(expert_){append(expert_->groups());append(expert_->cache_groups());append(expert_->gpu_groups());}if(parameter_)append(parameter_->groups());if(edge_)append(edge_->groups());if(arithmetic_)append(arithmetic_->groups());if(state_)append(state_->groups());if(plan_.profile=="R0")append(groups_);if(worker_)append(worker_->groups());if(observer_)append(observer_->groups());
  for(auto g:lifecycle_){g.id=uint32_t(out.size()+1);const auto&m=metadata();const auto&r=owner_.records();for(size_t i=0;i<m.size();++i)if(m[i].source.context==g.context&&m[i].event.sequence>=g.completion)g.records.push_back(r[i].id);out.emplace_back(std::move(g));}need(out.size()<=64,"combined group cap");return out;}
 bool worker_enabled()const{return plan_.worker_fp;}
 Owner&owner(){return owner_;}const std::vector<Metadata>&metadata()const{merged_.clear();auto append=[&](const auto&rows){merged_.insert(merged_.end(),rows.begin(),rows.end());};append(meta_);if(parameter_)append(parameter_->metadata());if(state_)append(state_->metadata());if(edge_)append(edge_->metadata());if(expert_)append(expert_->metadata());if(arithmetic_)append(arithmetic_->metadata());std::sort(merged_.begin(),merged_.end(),[](const auto&a,const auto&b){return a.record_id<b.record_id;});return merged_;}std::shared_ptr<ua50::DispatchObserver>dispatch_observer()const{return expert_;}const auto&expert_groups()const{need(bool(expert_),"expert adapter absent");return expert_->groups();}const auto&edge_groups()const{need(bool(edge_),"edge adapter absent");return edge_->groups();}const auto&state_groups()const{need(bool(state_),"state adapter absent");return state_->groups();}const auto&groups()const{return groups_;}
 bool unavailable()const{return unavailable_||(worker_&&worker_->failed())||(parameter_&&parameter_->failed())||(state_&&state_->unavailable())||(edge_&&edge_->unavailable())||(expert_&&expert_->failed())||(arithmetic_&&arithmetic_->unavailable());}uint64_t sequence()const{return owner_.sequence();}
 size_t owned_bytes()const{return sizeof(*this)+(parameter_?parameter_->owned_bytes():0)+(worker_?worker_->owned_bytes():0)+(observer_?observer_->owned_bytes():0)+(state_?state_->owned_bytes():0)+(edge_?edge_->owned_bytes():0)+(expert_?expert_->owned_bytes():0)+(arithmetic_?arithmetic_->owned_bytes():0)+merged_.capacity()*sizeof(Metadata)+owner_.owned_bytes()+nodes_.capacity()*sizeof(Node)+meta_.capacity()*sizeof(Metadata)+groups_.capacity()*sizeof(ResidualGroup);}
};
}
