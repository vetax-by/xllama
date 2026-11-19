#pragma once
#include "bounded_plan.h"
#include "bounded_index.h"
#include "ggml-backend.h"
namespace xllama::capture {
struct ArithmeticNode {uint32_t id=0,op=0,view_source=0;uint64_t view_offset=0;std::vector<uint32_t>src;std::array<uint64_t,4>ne{};std::string json()const{return "{\"id\":"+std::to_string(id)+",\"op\":"+std::to_string(op)+",\"src_ids\":"+numbers(src)+",\"view_source\":"+std::to_string(view_source)+",\"view_offset\":"+std::to_string(view_offset)+",\"ne\":"+numbers(ne)+"}";}};
struct ArithmeticGroup {uint32_t id=0,position=0,token=0,layer=0,occurrence=0,output=0,slots=0;std::string arm,point,op;std::vector<uint32_t>inputs;std::vector<ArithmeticNode>nodes;uint64_t first_write=0;
 std::string json()const{std::string chain="[";for(const auto&n:nodes){if(chain.size()>1)chain+=',';chain+=n.json();}chain+=']';return "{\"id\":"+std::to_string(id)+",\"kind\":\"ARITHMETIC\",\"point\":"+quote(point)+",\"op\":"+quote(op)+",\"arm\":"+quote(arm)+",\"position\":"+std::to_string(position)+",\"token\":"+std::to_string(token)+",\"layer\":"+std::to_string(layer)+",\"occurrence\":"+std::to_string(occurrence)+",\"inputs\":"+numbers(inputs)+",\"output\":"+std::to_string(output)+",\"nodes\":"+chain+",\"slot_ids\":"+(slots?std::to_string(slots):"null")+",\"first_write_sequence\":"+std::to_string(first_write)+"}";}
};
class ArithmeticCapture {
 struct Row {Selection selected;ggml_tensor*out=nullptr,*first=nullptr,*weighted=nullptr,*ids=nullptr;std::vector<ggml_tensor*>views,adds;bool entered=false,done=false;uint32_t occurrences=0;ArithmeticGroup group;};
 struct Router {uint32_t layer;ggml_tensor*t;};
 Owner&owner_;std::vector<Row>rows_;std::vector<Router>routers_;std::vector<Metadata>meta_;std::vector<ArithmeticGroup>groups_;uint32_t phase_=0,position_=0,token_=0;uint64_t context_=0,graph_=0;bool unavailable_=false;
 uint32_t node(ggml_tensor*t){return t?owner_.register_node(t,context_,graph_):0;}
 Source source(ggml_tensor*t){need(t,"arithmetic actual source");Source s;s.node=node(t);s.op=t->op;s.type=t->type;s.view=t->view_offs;s.context=context_;s.graph=graph_;for(int i=0;i<4;++i){need(t->ne[i]>0,"arithmetic extent");s.ne[i]=t->ne[i];s.nb[i]=t->nb[i];}return s;}
 ArithmeticNode description(ggml_tensor*t){ArithmeticNode n;n.id=node(t);n.op=t->op;n.view_source=node(t->view_src);n.view_offset=t->view_offs;for(int i=0;i<4;++i)n.ne[i]=t->ne[i];for(auto*s:t->src)if(s)n.src.push_back(node(s));need(n.src.size()<=2,"arithmetic node arity");return n;}
 uint32_t snapshot(Row&r,ggml_tensor*t,const char*semantic,const char*event,uint32_t consumer){need(t&&(t->type==GGML_TYPE_F32||t->type==GGML_TYPE_I32)&&ggml_is_contiguous(t)&&t->nb[0]==4&&ggml_nelements(t)>0&&ggml_nelements(t)<=16384,"arithmetic full contiguous typed operand");const uint32_t id=owner_.next_id();Metadata m;m.record_id=id;m.semantic=semantic;m.arm=phase_==1?"CPU":"HYBRID";m.position=position_;m.token=token_;m.layer=r.selected.layer;m.occurrence=r.selected.occurrence;m.shape={uint64_t(ggml_nelements(t))};m.source=source(t);m.event.kind=event;m.event.sequence=owner_.next_sequence();m.event.producer=node(t);m.event.consumer=consumer;for(auto*s:t->src)if(s)m.event.deps.push_back(node(s));owner_.copy(id,t->type==GGML_TYPE_F32?Type::f32:Type::i32,ggml_nelements(t),[&](void*d,size_t n){ggml_backend_tensor_get(t,d,0,n);return true;});if(owner_.records().back().type==Type::none)m.shape.clear();meta_.push_back(std::move(m));return id;}
 bool ancestor(ggml_tensor*t,ggml_tensor*target){std::vector<ggml_tensor*>todo{t},seen;while(!todo.empty()){auto*cur=todo.back();todo.pop_back();if(cur==target)return true;if(!cur||std::find(seen.begin(),seen.end(),cur)!=seen.end())continue;need(seen.size()<256,"slot dependency traversal cap");seen.push_back(cur);for(auto*s:cur->src)if(s){need(todo.size()<256,"slot dependency frontier cap");todo.push_back(s);}}return false;}

 void bind(Row&r){auto*t=r.out;const auto*p=graph_point(r.selected.point);need(t&&p&&uint32_t(t->op)==p->op&&t->type==GGML_TYPE_F32&&graph_shape(*p,t)&&!t->view_src,"arithmetic registered endpoint");r.views.clear();r.adds.clear();
  if(r.selected.point!="moe_aggregate"){need((t->op==GGML_OP_ADD||t->op==GGML_OP_MUL)&&t->src[0]&&t->src[1]&&t->src[0]->type==GGML_TYPE_F32&&t->src[1]->type==GGML_TYPE_F32&&ggml_can_repeat(t->src[1],t->src[0]),"arithmetic source operation");r.first=t;return;}
  auto*last=t;auto*middle=last->src[0];auto*first=middle?middle->src[0]:nullptr;need(first&&middle&&first->op==GGML_OP_ADD&&middle->op==GGML_OP_ADD&&last->op==GGML_OP_ADD,"slot actual three ADD chain");r.adds={first,middle,last};r.views={first->src[0],first->src[1],middle->src[1],last->src[1]};need(r.views[0]&&r.views[0]->src[0],"slot first source");r.weighted=r.views[0]->src[0];need(r.weighted->op==GGML_OP_MUL&&r.weighted->type==GGML_TYPE_F32&&r.weighted->ne[0]==2048&&r.weighted->ne[1]==4&&r.weighted->ne[2]==1&&r.weighted->ne[3]==1&&ggml_is_contiguous(r.weighted),"slot actual weighted matrix");
  for(size_t i=0;i<4;++i){auto*v=r.views[i];need(v&&v->op==GGML_OP_VIEW&&v->src[0]==r.weighted&&v->view_src==r.weighted&&v->view_offs==i*8192&&v->ne[0]==2048&&v->ne[1]==1&&v->ne[2]==1&&v->ne[3]==1,"slot source order/view lineage");}
  r.ids=nullptr;for(auto route:routers_)if(route.layer==r.selected.layer)r.ids=route.t;need(r.ids&&r.ids->type==GGML_TYPE_I32&&ggml_nelements(r.ids)==4&&ancestor(r.weighted->src[1],r.ids),"slot actual router dependency");r.first=first;
 }
 void before(Row&r){need(!r.entered&&token_==r.selected.token,"arithmetic selected occurrence/token");r.entered=true;bind(r);auto&g=r.group;g={};g.arm=phase_==1?"CPU":"HYBRID";g.point=r.selected.point;g.position=position_;g.token=token_;g.layer=r.selected.layer;g.occurrence=r.selected.occurrence;
  if(r.selected.point=="moe_aggregate"){g.op="SLOT_SUM";g.inputs={snapshot(r,r.weighted,"slot.weighted","READY_BEFORE_WRITE",node(r.first))};g.slots=snapshot(r,r.ids,"slot.actual_router_ids","READY_BEFORE_WRITE",node(r.first));for(auto*v:r.views)g.nodes.push_back(description(v));for(auto*a:r.adds)g.nodes.push_back(description(a));}
  else{g.op=r.out->op==GGML_OP_ADD?"ADD":"MUL";g.inputs={snapshot(r,r.out->src[0],"arithmetic.A","READY_BEFORE_WRITE",node(r.out)),snapshot(r,r.out->src[1],"arithmetic.B","READY_BEFORE_WRITE",node(r.out))};g.nodes={description(r.out)};}
  g.first_write=owner_.next_sequence();
 }
public:
 ArithmeticCapture(const Plan&p,Owner&o):owner_(o){for(const auto&s:p.selections)if(s.kind=="ARITHMETIC")rows_.push_back({s});rows_.reserve(16);meta_.reserve(p.limits.records);groups_.reserve(32);routers_.reserve(40);}
 void context(uint32_t phase){phase_=phase;++context_;++graph_;routers_.clear();for(auto&r:rows_){auto s=r.selected;r={};r.selected=s;}}
 void frame(uint32_t phase,uint32_t position,uint32_t token){need(phase==phase_,"arithmetic phase");position_=position;token_=token;graph_=(context_<<32)|(uint64_t(position_)+1);}
 bool event(const ggml_capture_event&e)noexcept{try{auto measure=owner_.instrument();
  if(e.kind==GGML_CAPTURE_REGISTER&&e.semantic&&std::string_view(e.semantic)=="ffn_moe_topk"){bool found=false;for(auto&r:routers_)if(r.layer==uint32_t(e.layer)){r.t=e.tensor;found=true;}if(!found){need(routers_.size()<40,"arithmetic router registry cap");routers_.push_back({uint32_t(e.layer),e.tensor});}}
  for(auto&r:rows_){const auto*p=graph_point(r.selected.point);if(e.kind==GGML_CAPTURE_REGISTER&&!r.done&&e.semantic&&p&&std::string_view(e.semantic)==p->semantic&&(!p->layer||e.layer==int32_t(r.selected.layer))){r.out=e.tensor;bind(r);}
   if(!phase_||r.done||position_!=r.selected.position)continue;
   if(e.kind==GGML_CAPTURE_WANT_BEFORE&&e.tensor==r.first)return true;
   if(e.kind==GGML_CAPTURE_WANT_AFTER&&e.tensor==r.out)return true;
   if(e.kind==GGML_CAPTURE_READY_BEFORE_WRITE&&e.tensor==r.first){if(r.occurrences++==r.selected.occurrence)before(r);}
   if(e.kind==GGML_CAPTURE_COMPLETE&&e.tensor==r.out&&r.entered){r.group.output=snapshot(r,r.out,"arithmetic.actual_output","AFTER_COMPUTE",0);r.group.id=uint32_t(groups_.size()+1);groups_.push_back(r.group);r.done=true;}
  }
 }catch(...){unavailable_=true;}return false;}
 const auto&metadata()const{return meta_;}const auto&groups()const{return groups_;}bool unavailable()const{return unavailable_;}
 size_t owned_bytes()const{return sizeof(*this)+rows_.capacity()*sizeof(Row)+routers_.capacity()*sizeof(Router)+meta_.capacity()*sizeof(Metadata)+groups_.capacity()*sizeof(ArithmeticGroup);}
};
}
