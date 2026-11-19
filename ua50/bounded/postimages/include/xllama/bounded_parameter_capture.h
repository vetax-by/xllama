#pragma once
#include "bounded_plan.h"
#include "bounded_index.h"
#include "ggml-backend.h"
namespace xllama::capture {
struct ParameterGroup {
 uint32_t id=0,position=0,token=0,layer=0,occurrence=0,record=0,row=0,rows=0,block=0,blocks=0,block_elements=0,block_bytes=0,source_index=0;std::string arm,point,model,status="CAPTURED";Source consumer;
 std::string json()const{return "{\"id\":"+std::to_string(id)+",\"kind\":\"PARAMETER_ROWS\",\"arm\":"+quote(arm)+",\"position\":"+std::to_string(position)+",\"token\":"+std::to_string(token)+",\"layer\":"+std::to_string(layer)+",\"occurrence\":"+std::to_string(occurrence)+",\"point\":"+quote(point)+",\"status\":"+quote(status)+",\"record\":"+std::to_string(record)+",\"model_sha256\":"+quote(model)+",\"lifetime\":\"MODEL_RESIDENT_READ_ONLY\",\"consumer\":"+source_json(consumer)+",\"source_index\":"+std::to_string(source_index)+",\"weight_row_start\":"+std::to_string(row)+",\"weight_row_count\":"+std::to_string(rows)+",\"block_start\":"+std::to_string(block)+",\"block_count\":"+std::to_string(blocks)+",\"block_elements\":"+std::to_string(block_elements)+",\"block_bytes\":"+std::to_string(block_bytes)+"}";}
};
class ParameterCapture {
 struct Row {Selection s;ggml_tensor*t=nullptr;uint32_t seen=0;bool done=false;};Plan plan_;Owner&owner_;std::string model_;std::vector<Row>rows_;std::vector<Metadata>meta_;std::vector<ParameterGroup>groups_;uint32_t phase_=0,position_=0,token_=0;uint64_t context_=0,graph_=0;bool failed_=false;
 uint32_t node(const ggml_tensor*t){return t?owner_.register_node(t,context_,graph_):0;}
 Source source(const ggml_tensor*t){need(t,"parameter actual tensor");Source s;s.node=node(t);s.op=t->op;s.type=t->type;s.view=t->view_offs;s.context=context_;s.graph=graph_;for(int d=0;d<4;++d){need(t->ne[d]>0,"parameter source extent");s.ne[d]=t->ne[d];s.nb[d]=t->nb[d];}return s;}
 void capture(Row&r){auto*t=r.t;const bool output=r.s.point=="result_output";const uint32_t src=output?0:1;need(t&&t->op==(output?GGML_OP_MUL_MAT:GGML_OP_SSM_CONV)&&t->src[src]&&token_==r.s.token,"resident parameter actual consumer/source");auto*w=t->src[src];ParameterGroup g;g.id=uint32_t(groups_.size()+1);g.arm=phase_==1?"CPU":"HYBRID";g.position=position_;g.token=token_;g.layer=r.s.layer;g.occurrence=r.s.occurrence;g.point=r.s.point;g.row=r.s.weight_row;g.rows=r.s.weight_rows;g.block=r.s.block;g.blocks=r.s.blocks;g.model=model_;g.source_index=src;g.consumer=source(t);g.record=owner_.next_id();Metadata m;m.record_id=g.record;m.semantic="parameter."+g.point;m.arm=g.arm;m.position=position_;m.token=token_;m.layer=g.layer;m.occurrence=g.occurrence;m.source=source(w);m.event.kind="READY_BEFORE_WRITE";m.event.sequence=owner_.next_sequence();m.event.producer=node(w);m.event.consumer=node(t);Type type=Type::none;
 switch(w->type){case GGML_TYPE_Q4_K:type=Type::q4;break;case GGML_TYPE_Q6_K:type=Type::q6;break;case GGML_TYPE_F32:type=Type::f32;break;case GGML_TYPE_F16:type=Type::f16;break;default:break;}
 g.block_elements=type==Type::q4||type==Type::q6?256:1;g.block_bytes=type==Type::none?0:uint32_t(width(type));
 const bool shape=output?w->ne[0]==2048&&w->ne[1]==128000:w->ne[0]>=2&&w->ne[0]<=64&&w->ne[1]==2048;
 if(type==Type::none)owner_.gap(g.record,Status::type);else if(!shape||w->ne[2]!=1||w->ne[3]!=1||!w->data||!w->buffer||g.row+g.rows>uint64_t(w->ne[1])||g.block+g.blocks>uint64_t(w->ne[0])/g.block_elements||w->nb[0]!=g.block_bytes||w->nb[1]<uint64_t(w->ne[0])/g.block_elements*g.block_bytes)owner_.gap(g.record,Status::layout);
 else{m.shape={uint64_t(g.rows)*g.blocks};owner_.copy(g.record,type,uint64_t(g.rows)*g.blocks,[&](void*out,size_t bytes){const size_t row_bytes=mul(g.blocks,g.block_bytes);need(bytes==mul(g.rows,row_bytes),"parameter packed extent");for(size_t row=0;row<g.rows;++row){const size_t at=add(mul(g.row+row,w->nb[1]),mul(g.block,g.block_bytes));need(at<=ggml_nbytes(w)&&row_bytes<=ggml_nbytes(w)-at,"parameter strided source extent");ggml_backend_tensor_get(w,static_cast<unsigned char*>(out)+row*row_bytes,at,row_bytes);}return true;});if(owner_.records().back().type==Type::none)m.shape.clear();}
 if(owner_.records().back().status!=Status::captured&&owner_.records().back().status!=Status::nonfinite)g.status="PARAMETER_MAPPING_UNAVAILABLE";meta_.push_back(std::move(m));groups_.push_back(std::move(g));r.done=true;
 }
public:
 ParameterCapture(const Plan&p,Owner&o,std::string model):plan_(p),owner_(o),model_(std::move(model)){need(sha_text(model_),"parameter model association");for(const auto&s:p.selections)if(s.kind=="PARAMETER_ROWS")rows_.push_back({s});meta_.reserve(p.limits.records);groups_.reserve(32);}
 void context(uint32_t phase){phase_=phase;++context_;for(auto&r:rows_){r.t=nullptr;r.done=false;r.seen=0;}}
 void frame(uint32_t phase,uint32_t position,uint32_t token){need(phase==phase_,"parameter phase");position_=position;token_=token;graph_=(context_<<32)|(uint64_t(position)+1);}
 bool event(const ggml_capture_event&e)noexcept{try{auto measure=owner_.instrument();for(auto&r:rows_){if(e.kind==GGML_CAPTURE_REGISTER&&e.semantic&&!r.done&&std::string_view(e.semantic)==(r.s.point=="result_output"?"result_output":"model.layers.{}.conv.conv")&&(r.s.point=="result_output"||e.layer==int32_t(r.s.layer)))r.t=e.tensor;if(!phase_||r.done||position_!=r.s.position||e.tensor!=r.t)continue;if(e.kind==GGML_CAPTURE_WANT_BEFORE)return true;if(e.kind==GGML_CAPTURE_READY_BEFORE_WRITE&&r.seen++==r.s.occurrence)capture(r);}}catch(...){failed_=true;}return false;}
 const auto&metadata()const{return meta_;}const auto&groups()const{return groups_;}bool failed()const{return failed_;}size_t owned_bytes()const{return sizeof(*this)+rows_.capacity()*sizeof(Row)+meta_.capacity()*sizeof(Metadata)+groups_.capacity()*sizeof(ParameterGroup);}
};
}
