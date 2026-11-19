#pragma once
#include "bounded_plan.h"
#include "bounded_index.h"
#include "ggml-backend.h"
namespace xllama::capture {
struct StateGroup {
 uint32_t id=0,position=0,token=0,layer=0,occurrence=0,write_source=0,before=0,after=0,read_window=0,read_consumer=0;
 std::string arm,mapping,kind="F16_KV_WINDOW",cast;bool supported=false,transposed=false;
 uint64_t state_epoch=0,context_epoch=0,graph_epoch=0,ready_sequence=0,write_sequence=0,completed_sequence=0,read_sequence=0;
 uint64_t head=0,size=512;std::vector<uint64_t>read_cells,read_indices,read_shape,read_strides,read_origin,read_window_shape;std::vector<uint64_t> cells,positions,indices,shape,strides,origin,window;
 std::string json()const{
  auto ref=[](uint32_t v){return v?std::to_string(v):"null";};
  return "{\"id\":"+std::to_string(id)+",\"kind\":"+quote(kind)+",\"arm\":"+quote(arm)+",\"position\":"+std::to_string(position)+",\"token\":"+std::to_string(token)+",\"layer\":"+std::to_string(layer)+",\"occurrence\":"+std::to_string(occurrence)+",\"mapping\":"+quote(mapping)+",\"supported\":"+(supported?"true":"false")+",\"state_epoch\":"+std::to_string(state_epoch)+",\"context_epoch\":"+std::to_string(context_epoch)+",\"graph_epoch\":"+std::to_string(graph_epoch)+",\"sequence_id\":0,\"stream_id\":0,\"cell_ids\":"+numbers(cells)+",\"positions\":"+numbers(positions)+",\"indices\":"+numbers(indices)+",\"write_source\":"+ref(write_source)+",\"before\":"+ref(before)+",\"after\":"+ref(after)+",\"read_window\":"+ref(read_window)+",\"read_sequence\":"+std::to_string(read_sequence)+",\"read_consumer_id\":"+std::to_string(read_consumer)+",\"ready_sequence\":"+std::to_string(ready_sequence)+",\"write_sequence\":"+std::to_string(write_sequence)+",\"completed_sequence\":"+std::to_string(completed_sequence)+",\"read_layout\":{\"cell_ids\":"+numbers(read_cells)+",\"indices\":"+numbers(read_indices)+",\"source_shape\":"+numbers(read_shape)+",\"source_strides\":"+numbers(read_strides)+",\"window_origin\":"+numbers(read_origin)+",\"window_shape\":"+numbers(read_window_shape)+"},\"cast\":"+quote(cast)+",\"layout\":{\"source_shape\":"+numbers(shape)+",\"source_strides\":"+numbers(strides)+",\"window_origin\":"+numbers(origin)+",\"window_shape\":"+numbers(window)+",\"head\":"+std::to_string(head)+",\"size\":"+std::to_string(size)+",\"transposed\":"+(transposed?"true":"false")+",\"duplicate_scatter\":false,\"shifted\":false,\"defrag\":false,\"multistream\":false}}";
 }
};
/* This first mapping allowlist observes one CPU sequence/current-token write.
 * It never treats an arbitrary F16 tensor as a cache. Registration must name
 * the actual SET_ROWS and attention read consumer from the model graph. */
class StateCapture {
 struct Mapping {Selection sel;ggml_tensor*write=nullptr,*read=nullptr,*cache=nullptr;StateGroup group;bool entered=false,done=false,read_done=false;uint64_t head=0,size=0,slot=0,sequences=0;std::vector<size_t> offsets,source_offsets,read_offsets;};
 struct Node {ggml_tensor*t;uint32_t id;};
 Owner&owner_;std::vector<Mapping> maps_;std::vector<Node>nodes_;std::vector<Metadata>meta_;std::vector<StateGroup>groups_;
 uint32_t phase_=0,position_=0,token_=0,next_=1;uint64_t context_=0,graph_=0,sequence_=0;bool unavailable_=false;
 uint32_t node(ggml_tensor*t){if(!t)return 0;for(auto&n:nodes_)if(n.t==t)return n.id;need(nodes_.size()<256,"state node cap");nodes_.push_back({t,owner_.register_node(t,context_,graph_)});return nodes_.back().id;}
 Source source(ggml_tensor*t){Source s;s.node=node(t);s.op=t->op;s.type=t->type;s.context=context_;s.graph=graph_;s.view=t->view_offs;for(int i=0;i<4;++i){need(t->ne[i]>0,"state extent");s.ne[i]=t->ne[i];s.nb[i]=t->nb[i];}return s;}
 static ggml_tensor*root(ggml_tensor*t){for(size_t n=0;t&&t->view_src;++n){need(n<8,"state view depth");t=t->view_src;}return t;}
 bool active(const Mapping&m)const{return phase_&&position_==m.sel.position&&!m.done;}
 uint32_t snapshot(Mapping&m,ggml_tensor*t,const std::vector<size_t>&offsets,Type type,const char*label,const char*event,uint32_t consumer){
  const uint32_t id=owner_.next_id();Metadata md;md.record_id=id;md.semantic=label;md.arm=phase_==1?"CPU":"HYBRID";md.position=position_;md.token=token_;md.layer=m.sel.layer;md.occurrence=m.sel.occurrence;md.source=source(t);md.shape={offsets.size()};md.event.kind=event;md.event.sequence=sequence_=owner_.next_sequence();md.event.producer=md.source.node;md.event.consumer=consumer;
  for(auto*s:t->src)if(s)md.event.deps.push_back(node(s));
  owner_.copy(id,type,offsets.size(),[&](void*dst,size_t n){const size_t w=width(type);need(n==offsets.size()*w,"state snapshot extent");for(size_t i=0;i<offsets.size();++i){need(offsets[i]<=ggml_nbytes(t)&&w<=ggml_nbytes(t)-offsets[i],"state offset extent");ggml_backend_tensor_get(t,static_cast<unsigned char*>(dst)+i*w,offsets[i],w);}return true;});
  if(owner_.records().back().type==Type::none)md.shape.clear();meta_.push_back(std::move(md));return id;
 }
 void initialize(Mapping&m){if(m.group.id)return;auto&g=m.group;g.id=uint32_t(groups_.size()+1);g.arm=phase_==1?"CPU":"HYBRID";g.position=position_;g.token=token_;g.layer=m.sel.layer;g.occurrence=m.sel.occurrence;g.state_epoch=g.context_epoch=context_;g.graph_epoch=graph_;}
 void shortconv_geometry(Mapping&m){
  auto*w=m.write;need(w&&w->op==GGML_OP_CPY&&w->src[0]&&w->src[1],"shortconv actual CPY");auto*src=w->src[0];auto*dst=w->src[1];auto*cache=root(dst);
  need(src->type==GGML_TYPE_F32&&dst->type==GGML_TYPE_F32&&cache->type==GGML_TYPE_F32&&m.sequences==1&&m.slot==0&&m.size>0&&m.size<=512&&m.head<m.size&&m.sel.cells==1&&m.sel.cell==m.head&&m.sel.occurrence==0&&token_==m.sel.token,"shortconv single sequence/head allowlist");
  need(cache->ne[0]>0&&cache->ne[1]>=int64_t(m.size)&&cache->ne[2]==1&&cache->ne[3]==1&&ggml_is_contiguous(cache)&&ggml_nelements(src)==cache->ne[0]&&ggml_nelements(dst)==cache->ne[0]&&dst->view_offs==m.head*cache->nb[1],"shortconv actual slot destination");
  need(m.sel.element+m.sel.elements<=size_t(cache->ne[0]),"shortconv selected extent");m.cache=cache;m.offsets.clear();m.source_offsets.clear();
  for(size_t i=0;i<m.sel.elements;++i){size_t linear=m.sel.element+i,offset=0;m.offsets.push_back(m.head*cache->nb[1]+linear*4);for(int d=0;d<4;++d){offset=add(offset,mul(linear%size_t(src->ne[d]),src->nb[d]));linear/=size_t(src->ne[d]);}m.source_offsets.push_back(offset);}
  auto&g=m.group;g.kind="SHORTCONV_WINDOW";g.mapping="LFM2_SHORTCONV_F32_RING_V1";g.cast="F32_COPY";g.head=m.head;g.size=m.size;g.cells={m.head};g.positions={position_};g.indices={m.head};g.shape={uint64_t(cache->ne[0]),uint64_t(cache->ne[1])};g.strides={cache->nb[0],cache->nb[1]};g.origin={m.sel.element,m.head};g.window={m.sel.elements,1};
 }
 void check_indices()const{size_t sum=0;for(const auto&m:maps_){const auto&g=m.group;sum+=g.cells.size()+g.positions.size()+g.indices.size()+g.read_cells.size()+g.read_indices.size();}need(sum<=1024,"state shared read/write index cap");}
 void read_geometry(Mapping&m,const std::vector<uint64_t>&cells,const std::vector<uint64_t>&indices,const std::vector<uint64_t>&origin){auto&g=m.group;g.read_cells=cells;g.read_indices=indices;g.read_shape=g.shape;g.read_strides=g.strides;g.read_origin=origin;g.read_window_shape=g.window;check_indices();}
 void prepare(Mapping&m){
  if(m.sel.kind=="SHORTCONV"){
   shortconv_geometry(m);check_indices();auto&g=m.group;g.write_source=snapshot(m,m.write->src[0],m.source_offsets,Type::f32,"state.write_source","READY_BEFORE_WRITE",node(m.write));g.before=snapshot(m,m.cache,m.offsets,Type::f32,"state.before","READY_BEFORE_WRITE",node(m.write));g.ready_sequence=sequence_;g.write_sequence=sequence_=owner_.next_sequence();g.supported=true;return;
  }
  auto*w=m.write;need(w&&w->op==GGML_OP_SET_ROWS,"state SET_ROWS binding");auto*src=w->src[0];auto*idx=w->src[1];auto*cache=w->src[2];
  need(src&&idx&&cache&&cache->type==GGML_TYPE_F16&&(src->type==GGML_TYPE_F32||src->type==GGML_TYPE_F16),"state actual source/storage type");
  need(m.sel.cells==1&&m.sel.occurrence==0&&token_==m.sel.token,"state single current-token allowlist");
  need(root(cache)->ne[2]==1&&root(cache)->ne[3]==1&&ggml_is_contiguous(cache)&&ggml_is_contiguous(src)&&idx->data,"state single stream contiguous allowlist");
  need(idx->type==GGML_TYPE_I64||idx->type==GGML_TYPE_I32,"state actual index type");
  auto read_index=[&](size_t at){int64_t v=0;if(idx->type==GGML_TYPE_I64)ggml_backend_tensor_get(idx,&v,at*8,8);else{int32_t a;ggml_backend_tensor_get(idx,&a,at*4,4);v=a;}need(v>=0,"state negative scatter");return uint64_t(v);};
  auto&g=m.group;g.mapping=m.sel.kind=="KV_K"?"LLAMA_KV_F16_CONTIG_K_V1":"LLAMA_KV_F16_TRANSPOSED_V_V1";g.transposed=m.sel.kind=="KV_V_TRANSPOSED";g.cast=src->type==GGML_TYPE_F32?"F32_TO_F16_RNE":"F16_COPY";
  m.cache=cache;const size_t sw=ggml_type_size(src->type);const size_t start=m.sel.element,count=m.sel.elements;
  if(!g.transposed){
   need(cache->ne[1]==512&&cache->ne[2]==1&&cache->ne[3]==1&&src->ne[1]==1&&src->ne[2]==1&&src->ne[3]==1&&src->ne[0]==cache->ne[0]&&ggml_nelements(idx)==1,"state K row mapping");
   need(src->ne[0]<=2048&&start+count<=size_t(src->ne[0])&&read_index(0)==m.sel.cell,"state K selected cell");g.indices={m.sel.cell};
   for(size_t i=0;i<count;++i){m.offsets.push_back(m.sel.cell*cache->nb[1]+(start+i)*2);m.source_offsets.push_back((start+i)*sw);}
   g.shape={uint64_t(cache->ne[0]),512};g.strides={cache->nb[0],cache->nb[1]};g.origin={start,m.sel.cell};g.window={count,1};
  }else{
   need(cache->ne[0]==1&&cache->ne[2]==1&&cache->ne[3]==1&&cache->ne[1]%512==0&&src->ne[0]==1&&src->ne[2]==1&&src->ne[3]==1&&size_t(src->ne[1])*512==size_t(cache->ne[1])&&ggml_nelements(idx)==src->ne[1],"state V transposed mapping");
   need(src->ne[1]<=2048&&start+count<=size_t(src->ne[1]),"state V selected range");
   // Authenticate the complete scatter, not only selected indices. Duplicate,
   // shifted, or multistream routes cannot inherit this mapping's claim.
   for(size_t i=0;i<size_t(src->ne[1]);++i)need(read_index(i)==i*512+m.sel.cell,"state V scatter mapping");
   for(size_t i=0;i<count;++i){g.indices.push_back((start+i)*512+m.sel.cell);m.offsets.push_back(g.indices.back()*2);m.source_offsets.push_back((start+i)*sw);}
   g.shape={512,uint64_t(src->ne[1])};g.strides={2,1024};g.origin={m.sel.cell,start};g.window={1,count};
  }
  g.cells={m.sel.cell};g.positions={position_};g.head=m.sel.cell;
  check_indices();
  g.write_source=snapshot(m,src,m.source_offsets,src->type==GGML_TYPE_F32?Type::f32:Type::f16,"state.write_source","READY_BEFORE_WRITE",node(w));
  g.before=snapshot(m,cache,m.offsets,Type::f16,"state.before","READY_BEFORE_WRITE",node(w));g.ready_sequence=sequence_;g.write_sequence=sequence_=owner_.next_sequence();g.supported=true;
 }
 void capture_read(Mapping&m){
  if(m.sel.kind=="SHORTCONV"){
   initialize(m);shortconv_geometry(m);auto*r=m.read;need(r&&r->op==GGML_OP_GET_ROWS&&r->src[0]&&root(r->src[0])==m.cache&&r->src[1]&&r->src[1]->type==GGML_TYPE_I32&&ggml_nelements(r->src[1])==1,"shortconv actual gather allowlist");int32_t index=-1;ggml_backend_tensor_get(r->src[1],&index,0,4);
   need(index>=0&&uint64_t(index)<uint64_t(m.cache->ne[1]),"shortconv actual read cell extent");m.read_offsets.clear();for(size_t i=0;i<m.sel.elements;++i)m.read_offsets.push_back(uint64_t(index)*m.cache->nb[1]+(m.sel.element+i)*4);read_geometry(m,{uint64_t(index)},{uint64_t(index)},{m.sel.element,uint64_t(index)});
   m.group.read_window=snapshot(m,m.cache,m.read_offsets,Type::f32,"state.read","READY_BEFORE_READ",node(r));m.group.read_sequence=sequence_;m.group.read_consumer=node(r);m.read_done=true;return;
  }
  if(!m.cache||!m.group.supported)return;
  auto*r=m.read;need(r&&r->op==GGML_OP_MUL_MAT&&r->src[0]&&root(r->src[0])==root(m.cache),"state actual attention read binding");
  auto*view=r->src[0];need(view->type==GGML_TYPE_F16&&view->data&&m.cache->data,"state live read storage");
  // Check that each selected storage address is actually reachable by the
  // consumer's measured view. Singleton dimensions impose no fake stride.
  std::vector<std::pair<size_t,size_t>> axes;for(int i=0;i<4;++i)if(view->ne[i]>1)axes.push_back({view->nb[i],size_t(view->ne[i])});
  std::sort(axes.begin(),axes.end());size_t covered=2;for(const auto&axis:axes){need(axis.first>=covered,"state overlapping read view unsupported");covered=add(mul(axis.second-1,axis.first),covered);}
  for(size_t off:m.offsets){need(off<=ggml_nbytes(m.cache)&&2<=ggml_nbytes(m.cache)-off,"state read cache bound");const auto base=reinterpret_cast<uintptr_t>(view->data),cache_base=reinterpret_cast<uintptr_t>(m.cache->data);need(off<=UINTPTR_MAX-cache_base,"state read address overflow");const auto addr=cache_base+off;need(addr>=base,"state read address before view");size_t delta=addr-base;
   for(auto it=axes.rbegin();it!=axes.rend();++it){need(delta/it->first<it->second,"state selected window outside actual read");delta%=it->first;}need(delta==0,"state selected window read alignment");}
  read_geometry(m,m.group.cells,m.group.indices,m.group.origin);m.group.read_window=snapshot(m,m.cache,m.offsets,Type::f16,"state.read","READY_BEFORE_READ",node(r));m.group.read_sequence=sequence_;m.group.read_consumer=node(r);m.read_done=true;
 }
public:
 StateCapture(const Plan&p,Owner&o):owner_(o){for(auto&s:p.selections){if(s.kind!="KV_K"&&s.kind!="KV_V_TRANSPOSED"&&s.kind!="SHORTCONV")continue;Mapping m;m.sel=s;maps_.push_back(std::move(m));}nodes_.reserve(256);meta_.reserve(p.limits.records);groups_.reserve(32);}
 void context(uint32_t phase){phase_=phase;++context_;++graph_;nodes_.clear();for(auto&m:maps_){auto s=m.sel;m=Mapping{};m.sel=s;}}
 void frame(uint32_t phase,uint32_t pos,uint32_t token){need(phase==phase_,"state phase");position_=pos;token_=token;graph_=(context_<<32)|(uint64_t(position_)+1);}
 bool event(const ggml_capture_event&e)noexcept{try{auto measurement=owner_.instrument();
  for(auto&m:maps_){if(e.layer==int32_t(m.sel.layer)&&e.semantic&&!m.done){const std::string_view label(e.semantic);const bool k=m.sel.kind=="KV_K",v=m.sel.kind=="KV_V_TRANSPOSED";
    if(m.sel.kind=="SHORTCONV"&&label=="@capture.shortconv.write"&&e.values[2]==0){m.write=e.tensor;m.head=e.values[0];m.size=e.values[1];m.slot=e.values[2];m.sequences=e.values[3];}
    if(m.sel.kind=="SHORTCONV"&&label=="@capture.shortconv.read")m.read=e.tensor;
    if((k&&label=="@capture.kv.K.write")||(v&&label=="@capture.kv.V.write"))m.write=e.tensor;
    if((k&&label=="@capture.kv.K.read")||(v&&label=="@capture.kv.V.read"))m.read=e.tensor;}
   if(!active(m))continue;
   if(e.kind==GGML_CAPTURE_WANT_BEFORE&&e.tensor==m.write)return true;
   if(e.kind==GGML_CAPTURE_WANT_READ&&e.tensor==m.read)return true;
   if(e.kind==GGML_CAPTURE_READY_BEFORE_WRITE&&e.tensor==m.write){need(!m.entered,"state duplicate occurrence");m.entered=true;initialize(m);prepare(m);}
   if(e.kind==GGML_CAPTURE_COMPLETE&&e.tensor==m.write&&m.group.supported){m.group.after=snapshot(m,m.cache,m.offsets,m.sel.kind=="SHORTCONV"?Type::f32:Type::f16,"state.after","AFTER_COMPUTE",0);m.group.completed_sequence=sequence_;}
   if(e.kind==GGML_CAPTURE_READY_BEFORE_READ&&e.tensor==m.read&&!m.read_done)capture_read(m);
   if(m.group.after&&m.read_done){m.done=true;groups_.push_back(m.group);}
  }
 }catch(...){unavailable_=true;}return false;}
 size_t owned_bytes()const{size_t n=sizeof(*this)+maps_.capacity()*sizeof(Mapping)+nodes_.capacity()*sizeof(Node)+meta_.capacity()*sizeof(Metadata)+groups_.capacity()*sizeof(StateGroup);for(const auto&m:maps_)n+=m.offsets.capacity()*sizeof(size_t)+m.source_offsets.capacity()*sizeof(size_t)+m.read_offsets.capacity()*sizeof(size_t);return n;}
 const auto&metadata()const{return meta_;}const auto&groups()const{return groups_;}uint64_t sequence()const{return owner_.sequence();}uint64_t next_sequence(){return sequence_=owner_.next_sequence();}bool unavailable()const{return unavailable_;}
};
}
