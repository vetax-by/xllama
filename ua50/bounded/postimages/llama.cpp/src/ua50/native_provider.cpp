#include "native_provider.h"
#include <cstdio>
#include <cstring>
#include <exception>
#include <chrono>
#include "ggml-cpu.h"

namespace {
using ua50::NativeProvider;
NativeProvider * model(ggml_backend_buffer_type_t b) { return static_cast<NativeProvider *>(b->context); }
const char * name(ggml_backend_buffer_type_t) { return "UA50-EXTERNAL-CPU"; }
size_t alignment(ggml_backend_buffer_type_t) { return 64; }
bool host(ggml_backend_buffer_type_t) { return false; }
void * base(ggml_backend_buffer_t) { return nullptr; }
void clear(ggml_backend_buffer_t,uint8_t) { GGML_ABORT("external expert clear forbidden"); }
void set(ggml_backend_buffer_t,ggml_tensor *,const void *,size_t,size_t) { GGML_ABORT("external expert set forbidden"); }
void get(ggml_backend_buffer_t,const ggml_tensor *,void *,size_t,size_t) { GGML_ABORT("external expert full get/copy forbidden"); }
void memset_tensor(ggml_backend_buffer_t,ggml_tensor *,uint8_t,size_t,size_t) { GGML_ABORT("external expert memset forbidden"); }
bool copy(ggml_backend_buffer_t,const ggml_tensor *,ggml_tensor *) { return false; }
ggml_backend_buffer_t alloc(ggml_backend_buffer_type_t,size_t) { return nullptr; }
ggml_backend_buffer_t alloc_n(ggml_backend_buffer_type_t b,ggml_tensor ** tensors,int n) {
    auto * p=model(b);
    if(n<=0 || n>128) return nullptr;
    for(int i=0;i<n;++i) if(!p->registered(tensors[i]) || tensors[i]->data || tensors[i]->buffer || tensors[i]->view_src) return nullptr;
    ggml_backend_buffer_i iface{};
    iface.get_base=base; iface.clear=clear; iface.set_tensor=set; iface.get_tensor=get;
    iface.memset_tensor=memset_tensor; iface.cpy_tensor=copy;
    // No payload allocation and no artificial address: only the native buffer
    // descriptor is allocated here. Cache storage is owned by NativeProvider.
    ggml_backend_buffer_t buffer=nullptr;
    try { buffer=ggml_backend_buffer_init(b,iface,p,p->requested_owned_bytes()); } catch(...) { return nullptr; }
    if(!buffer) return nullptr;
    for(int i=0;i<n;++i) tensors[i]->buffer=buffer;
    return buffer;
}
size_t alloc_size_n(ggml_backend_buffer_type_t b,ggml_tensor ** tensors,int n) {
    auto * p=model(b);
    for(int i=0;i<n;++i) if(!p->registered(tensors[i])) GGML_ABORT("unregistered external expert");
    return p->reserved_budget(); // memory-fit planning is bounded/reserved, not full expert bytes
}
ua50::Quant quant(ggml_type t) {
    switch(t) {
    case GGML_TYPE_IQ3_S:return ua50::Quant::iq3_s;
    case GGML_TYPE_Q4_K:return ua50::Quant::q4_k;
    case GGML_TYPE_Q4_0:return ua50::Quant::q4_0;
    case GGML_TYPE_Q6_K:return ua50::Quant::q6_k;
    case GGML_TYPE_F16:return ua50::Quant::f16;
    case GGML_TYPE_F32:return ua50::Quant::f32;
    default:throw std::invalid_argument("external tensor dtype not in reviewed whitelist");
    }
}
NativeProvider * owner(const ggml_tensor * t) {
    return ggml_ua50_is_external(t)?model(t->buffer->buft):nullptr;
}
}
namespace ua50 {
NativeProvider::NativeProvider(Reader & r,uint64_t bytes,size_t slots,size_t max_expert,size_t budget)
    :reader_(r),file_bytes_(bytes),slots_(slots),max_expert_(max_expert),budget_(budget) {
    if(budget<=sizeof(*this)+sizeof(ggml_backend_buffer)) throw std::invalid_argument("provider metadata budget");
    buft_.iface.get_name=name; buft_.iface.alloc_buffer=alloc; buft_.iface.alloc_buffer_n=alloc_n;
    buft_.iface.get_alignment=alignment; buft_.iface.get_alloc_size_n=alloc_size_n; buft_.iface.is_host=host;
    buft_.context=this; // device remains nullptr; CPU supports_buft bridge is explicit
}
NativeProvider::NativeProvider(std::shared_ptr<Reader> r,uint64_t bytes,size_t slots,size_t max_expert,size_t budget)
    :NativeProvider(r ? *r : throw std::invalid_argument("owned Reader required"),bytes,slots,max_expert,budget) {
    reader_owner_=std::move(r);
}
NativeProvider::~NativeProvider(){release();if(cache_)cache_->set_observer(nullptr,nullptr);}
bool NativeProvider::registered(const ggml_tensor * t) const {
    for(size_t i=0;i<count_;++i) if(records_[i].tensor==t) return true;
    return false;
}
void NativeProvider::register_tensor(ggml_tensor * t,uint16_t file_index,uint64_t off) {
    if(!t || cache_ || file_index!=0 || count_>=records_.size() || registered(t) || t->data || t->buffer || t->view_src || t->ne[3]!=1)
        throw std::invalid_argument("external registration/lifetime/file/shape");
    for(int i=0;i<3;++i) if(t->ne[i]<=0) throw std::invalid_argument("external dimension");
    auto q=quant(t->type); auto b=block(q);
    if(gpu_&&!((t->ne[0]==2048&&t->ne[1]==1536&&q==Quant::q4_k)||
      (t->ne[0]==1536&&t->ne[1]==2048&&(q==Quant::q4_k||q==Quant::q6_k))))
      throw std::invalid_argument("hybrid all-role quant/shape admission");
    if(t->nb[0]!=b.bytes || static_cast<uint64_t>(t->ne[0])%b.elements)
        throw std::invalid_argument("external quant block stride");
    const uint64_t row=mul(static_cast<uint64_t>(t->ne[0])/b.elements,b.bytes);
    const uint64_t expert=mul(row,static_cast<uint64_t>(t->ne[1]));
    if(t->nb[1]!=row || t->nb[2]!=expert || t->nb[3]!=mul(expert,t->ne[2]) || expert>max_expert_ || t->ne[2]>64 || add(off,mul(expert,t->ne[2]))>file_bytes_)
        throw std::invalid_argument("external tensor extent/stride/budget");
    Tensor d{static_cast<uint32_t>(count_),q,static_cast<uint64_t>(t->ne[0]),static_cast<uint64_t>(t->ne[1]),static_cast<uint64_t>(t->ne[2]),off,t->nb[1],t->nb[2]};
    descriptors_[count_]=d; records_[count_++]={t,d};
}
void NativeProvider::finalize() {
    if(cache_ || !count_) throw std::logic_error("provider finalize exactly once after registration");
    if(gpu_)validate_hybrid_catalog();
    cache_=std::make_unique<Cache>(reader_,file_bytes_,descriptors_.data(),count_,slots_,max_expert_,budget_-sizeof(*this)-sizeof(ggml_backend_buffer));
    if(dispatch_observer_)cache_->set_observer([](const CacheEvent&e,void*p)noexcept{auto*self=static_cast<NativeProvider*>(p);if(self->dispatch_observer_)self->dispatch_observer_->cache_event(e);},this);
}
const ggml_tensor*NativeProvider::tensor_named(const char*name)const{for(size_t i=0;i<count_;++i)if(std::strcmp(ggml_get_name(records_[i].tensor),name)==0)return records_[i].tensor;throw std::invalid_argument("expert primitive tensor missing");}
void NativeProvider::validate_hybrid_catalog()const{
 if(count_!=114||slots_<2||slots_>512||max_expert_!=2580480)throw std::invalid_argument("hybrid complete LFM24 registry required");
 size_t q4=0,q6=0;for(size_t i=0;i<count_;++i){auto&d=records_[i].layout;
  if(d.experts!=64||!((d.columns==2048&&d.rows==1536&&d.quant==Quant::q4_k)||(d.columns==1536&&d.rows==2048&&(d.quant==Quant::q4_k||d.quant==Quant::q6_k))))throw std::invalid_argument("hybrid metadata role/quant/shape");
  (d.quant==Quant::q4_k?q4:q6)++;}
 if(q4!=96||q6!=18)throw std::invalid_argument("hybrid actual mixed-quant role counts");
}
size_t NativeProvider::required_owned_bytes(size_t slots,size_t max_expert) {
    uint64_t n=add(sizeof(NativeProvider)+sizeof(ggml_backend_buffer),Cache::required_owned_bytes(slots,max_expert));
    if(n>SIZE_MAX)throw std::overflow_error("provider sizing host size");
    return size_t(n);
}
size_t NativeProvider::requested_owned_bytes() const {
    if(!cache_) throw std::logic_error("cache not finalized before native allocation");
    return sizeof(*this)+sizeof(ggml_backend_buffer)+cache_->counters().arena_bytes+cache_->counters().metadata_bytes;
}
const Counters & NativeProvider::counters() const { if(!cache_) throw std::logic_error("cache missing"); return cache_->counters(); }
bool NativeProvider::acquire(const ggml_tensor * t,uint64_t e) noexcept {
    // A failed provider is terminal: preserve the first cause and never read again.
    if(error_[0]) return false;
    try {
        if(active_tensor_ || !cache_) throw std::logic_error("provider failed/busy/not finalized");
        for(size_t i=0;i<count_;++i) if(records_[i].tensor==t) {
            const auto& d=records_[i].layout;
            if(trace_)trace_(d.id,e,add(d.offset,mul(d.nb02,e)),d.nb02);
            active_=cache_->acquire(records_[i].layout.id,e);
            if(cache_->counters().clock_errors){active_.reset();throw std::runtime_error("cache monotonic clock failed");}
            active_tensor_=t; active_expert_=e;if(dispatch_observer_)dispatch_observer_->acquired(d,uint32_t(e),active_.data(),active_.size());return true;
        }
        throw std::logic_error("unregistered provider tensor");
    } catch(const std::exception & e) { std::snprintf(error_,sizeof(error_),"%s",e.what()); }
      catch(...) { std::snprintf(error_,sizeof(error_),"unknown expert reader error"); }
    return false;
}
void NativeProvider::release() noexcept {
 if(gpu_){gpu_->drain();if(!gpu_->terminal())std::terminate();}
 if(dispatch_observer_&&active_tensor_)dispatch_observer_->released();
 active_.reset(); active_tensor_=nullptr;
}
void NativeProvider::diagnostic_input(const ggml_tensor*t,uint32_t slot,uint32_t row,
                                      const float*f32,size_t k,const unsigned char*q8,size_t bytes) noexcept{
 if(dispatch_observer_){if(active_tensor_==t&&f32&&q8)dispatch_observer_->input(slot,row,f32,k,q8,bytes);else dispatch_observer_->unavailable("LEASE_OR_INPUT");}
 if(!cross_arm_observer_)return;
 if(active_tensor_!=t||!f32||!q8){cross_arm_observer_->unavailable("LEASE_OR_INPUT");return;}
 for(size_t i=0;i<count_;++i)if(records_[i].tensor==t){
  const auto&d=records_[i].layout;
  if(k!=d.columns||bytes!=k/256*292||active_expert_>=d.experts){cross_arm_observer_->unavailable("ROLE_INPUT_SHAPE");return;}
  cross_arm_observer_->input(d.id,uint32_t(active_expert_),slot,row,f32,k,q8,bytes);return;
 }
 cross_arm_observer_->unavailable("UNREGISTERED_ROLE");
}
void NativeProvider::diagnostic_output(const ggml_tensor*t,uint32_t slot,uint32_t row,
                                       const float*values,size_t n) noexcept{
 if(dispatch_observer_&&!gpu_){if(active_tensor_==t&&values)dispatch_observer_->cpu_output(slot,row,values,n);else dispatch_observer_->unavailable("LEASE_OR_OUTPUT");}
 if(!cross_arm_observer_)return;
 if(active_tensor_!=t||!values){cross_arm_observer_->unavailable("LEASE_OR_OUTPUT");return;}
 for(size_t i=0;i<count_;++i)if(records_[i].tensor==t){
  const auto&d=records_[i].layout;
  if(n!=d.rows){cross_arm_observer_->unavailable("ROLE_OUTPUT_SHAPE");return;}
  cross_arm_observer_->output(d.id,uint32_t(active_expert_),slot,row,values,n);return;
 }
 cross_arm_observer_->unavailable("UNREGISTERED_ROLE");
}
bool NativeProvider::gpu_vector(const ggml_tensor*t,const void*q,size_t bytes,float*dst,size_t n) noexcept {
 if(error_[0]||!gpu_)return false;
 try{
  if(active_tensor_!=t||!q)throw std::logic_error("GPU vector without active host lease");
  for(size_t i=0;i<count_;++i)if(records_[i].tensor==t){const auto&d=records_[i].layout;
   const ExpertVector request{d.quant,d.id,uint32_t(active_expert_),uint32_t(d.columns),uint32_t(d.rows),active_.data(),active_.size(),static_cast<const unsigned char*>(q),bytes};
   check_vector(request);
   // Optional, synchronous diagnostic under the authentic held lease. A copied
   // CPU-arm Q8 row is matched by semantic identity and exact hybrid Q8 bytes.
   // Neither input nor the GPU publication buffer is modified.
   ua50::HeldWeightQuery held_key{};std::array<unsigned char,2336> cpu_q8{};
   std::array<float,2048> held_x{},held_h{};bool held_ready=false;
   if(cross_arm_observer_&&cross_arm_observer_->held_reference(d.id,uint32_t(active_expert_),
       request.q8,request.q8_bytes,cpu_q8.data(),cpu_q8.size(),held_key)){
    const auto started=std::chrono::steady_clock::now();
    uint64_t completed_dots=0;
    try{
     const auto*trait=ggml_get_type_traits_cpu(t->type);
     if(!trait||trait->vec_dot_type!=GGML_TYPE_Q8_K||!trait->vec_dot||
        !(d.quant==Quant::q4_k||d.quant==Quant::q6_k)||
        !(d.columns==1536||d.columns==2048)||d.rows>held_x.size()||
        d.nb01>SIZE_MAX/d.rows||d.nb01*d.rows!=request.weight_bytes)
      throw std::runtime_error("held-weight diagnostic layout refused");
     for(size_t row=0;row<d.rows;++row){
      const auto*weight=request.weights+row*d.nb01;
      trait->vec_dot(int(request.k),held_x.data()+row,0,weight,0,cpu_q8.data(),0,1);
      ++completed_dots;
      trait->vec_dot(int(request.k),held_h.data()+row,0,weight,0,request.q8,0,1);
      ++completed_dots;
     }
     held_ready=true;
    }catch(...){cross_arm_observer_->held_unavailable(held_key,"HELD_ORACLE_EXCEPTION");}
    const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now()-started).count();
    const uint64_t units=uint64_t(d.columns)*completed_dots;
    cross_arm_observer_->held_charge(held_key,units,elapsed<0?UINT64_MAX:uint64_t(elapsed));
   }
   std::array<float,2048> oracle;
   const bool selected=diagnostic_oracle(vector_observer_.get(),request,[&]{
    const auto*trait=ggml_get_type_traits_cpu(t->type);
    if(!trait||trait->vec_dot_type!=GGML_TYPE_Q8_K||!trait->vec_dot||t->nb[1]!=d.nb01||
       d.rows>oracle.size()||d.nb01>SIZE_MAX/d.rows||d.nb01*d.rows!=request.weight_bytes)
      throw std::runtime_error("vector witness CPU oracle layout refused");
    for(size_t row=0;row<request.n;++row)
      trait->vec_dot(int(request.k),oracle.data()+row,0,request.weights+row*d.nb01,0,request.q8,0,1);
   });
   // A separately selected bounded CPU reference uses the SAME held W/Q8.
   // Its output range and work/time are reported; it never changes dispatch.
   if(dispatch_observer_){uint32_t first=0,count=0;if(dispatch_observer_->oracle_range(first,count)){
    const auto started=std::chrono::steady_clock::now();std::array<float,2048> bounded_oracle{};
    try{if(!count||first>d.rows||count>d.rows-first)throw std::runtime_error("dispatch oracle range");
     const auto*trait=ggml_get_type_traits_cpu(t->type);if(!trait||trait->vec_dot_type!=GGML_TYPE_Q8_K||!trait->vec_dot)throw std::runtime_error("dispatch oracle type");
     for(uint32_t row=0;row<count;++row)trait->vec_dot(int(request.k),bounded_oracle.data()+row,0,request.weights+size_t(first+row)*d.nb01,0,request.q8,0,1);
     const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count();
     dispatch_observer_->oracle(bounded_oracle.data(),count,first,uint64_t(count)*request.k,ns<0?UINT64_MAX:uint64_t(ns));
    }catch(...){dispatch_observer_->unavailable("DISPATCH_ORACLE_EXCEPTION");}
   }}
   const float* result=gpu_->execute(request);
   if(dispatch_observer_)dispatch_observer_->pre_publish(result,n,gpu_->terminal());
   if(held_ready){if(gpu_->terminal()&&result)cross_arm_observer_->held_result(
      held_key,held_x.data(),held_h.data(),result,d.rows);
     else cross_arm_observer_->held_unavailable(held_key,"HELD_GPU_RESULT_UNAVAILABLE");}
   if(cross_arm_observer_)cross_arm_observer_->held_vector_done();
   if(selected){if(gpu_->terminal()&&result)diagnostic_result(vector_observer_.get(),true,request,oracle.data(),result);else vector_observer_->unavailable();}
   if(n!=d.rows||!gpu_->publish(result,dst,n))throw std::runtime_error("GPU stopped/nonfinite graph publication refused");if(dispatch_observer_)dispatch_observer_->post_publish(dst,n);return true;}
  throw std::logic_error("GPU vector unregistered role");
 }catch(const std::exception&e){if(cross_arm_observer_)cross_arm_observer_->held_vector_done();if(!error_[0])std::snprintf(error_,sizeof(error_),"%s",e.what());}
 catch(...){if(cross_arm_observer_)cross_arm_observer_->held_vector_done();if(!error_[0])std::snprintf(error_,sizeof(error_),"GPU unknown provider failure");}
 gpu_->request_stop();gpu_->drain();return false;
}
const char * NativeProvider::data(const ggml_tensor * t) const noexcept {
    return !error_[0] && active_tensor_==t?reinterpret_cast<const char *>(active_.data()):nullptr;
}
}
extern "C" bool ggml_ua50_is_buft(const ggml_backend_buffer_type * b) { return b && b->iface.alloc_buffer_n==alloc_n; }
extern "C" bool ggml_ua50_is_external(const ggml_tensor * t) { return t && t->buffer && ggml_ua50_is_buft(t->buffer->buft); }
extern "C" bool ggml_ua50_acquire(const ggml_tensor * t,int64_t e) {
    auto * p=owner(t); return p && e>=0 && p->acquire(t,static_cast<uint64_t>(e));
}
extern "C" const char * ggml_ua50_data(const ggml_tensor * t) { auto * p=owner(t);return p?p->data(t):nullptr; }
extern "C" void ggml_ua50_release(const ggml_tensor * t) { if(auto * p=owner(t)) p->release(); }
extern "C" const char * ggml_ua50_error(const ggml_tensor * t) { auto * p=owner(t);return p?p->error():"not external"; }

extern "C" bool ggml_ua50_has_gpu(const ggml_tensor*t){auto*p=owner(t);return p&&p->has_gpu();}
extern "C" bool ggml_ua50_gpu_vector(const ggml_tensor*t,const void*q,uint64_t bytes,float*dst,uint64_t n){auto*p=owner(t);return p&&bytes<=SIZE_MAX&&n<=SIZE_MAX&&p->gpu_vector(t,q,size_t(bytes),dst,size_t(n));}
extern "C" bool ggml_ua50_has_cross_arm_observer(const ggml_tensor*t){auto*p=owner(t);return p&&p->has_cross_arm_observer();}
extern "C" void ggml_ua50_diagnostic_unavailable(const ggml_tensor*t,const char*reason){auto*p=owner(t);if(p)p->diagnostic_unavailable(reason);}
extern "C" void ggml_ua50_diagnostic_binding(const ggml_tensor*w,const ggml_tensor*i,const ggml_tensor*o){auto*p=owner(w);if(p)p->diagnostic_binding(w,i,o);}
extern "C" void ggml_ua50_diagnostic_input(const ggml_tensor*t,uint32_t slot,uint32_t row,const float*f32,uint64_t k,const void*q8,uint64_t bytes){
 auto*p=owner(t);if(p){if(k>SIZE_MAX||bytes>SIZE_MAX)p->diagnostic_input(t,slot,row,nullptr,0,nullptr,0);
 else p->diagnostic_input(t,slot,row,f32,size_t(k),static_cast<const unsigned char*>(q8),size_t(bytes));}
}
extern "C" void ggml_ua50_diagnostic_output(const ggml_tensor*t,uint32_t slot,uint32_t row,const float*values,uint64_t n){
 auto*p=owner(t);if(p)p->diagnostic_output(t,slot,row,values,n<=SIZE_MAX?size_t(n):0);
}
