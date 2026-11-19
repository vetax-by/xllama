#pragma once
#include "expert_cache.h"
#include "expert_diagnostics.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <thread>
#include <mutex>
#include <string>

namespace ua50 {
// Explicit hybrid extension. Absent pointer means the accepted CPU path, never fallback.
struct GpuAllocation { const char* name=nullptr; uint64_t bytes=0; bool existing=false; };
struct GpuResources {
 std::array<GpuAllocation,16> entries{};size_t count=0;uint64_t driver_probe=256ull<<20;
 void add(const char* name,uint64_t bytes,bool existing=false){
  if(!name||!bytes||count==entries.size())throw std::invalid_argument("GPU resource ledger shape");
  for(size_t i=0;i<count;++i)if(std::strcmp(entries[i].name,name)==0)throw std::invalid_argument("GPU resource duplicate");
  entries[count++]={name,bytes,existing};(void)total();
 }
 uint64_t total()const{uint64_t n=driver_probe;for(size_t i=0;i<count;++i)n=ua50::add(n,entries[i].bytes);return n;}
};
struct GpuSnapshot {
 std::string adapter_luid,shader_sha256;GpuResources resources;
 uint64_t app_limit=0,app_usage=0,dxgi_budget=0,dxgi_usage=0,init_observed_app_delta=0,owned_creation_app_delta=0;
 bool cache_coherent_uma=false; // DXGI budget is a second constraint, not extra RAM.
};
struct GpuCounts {
 uint64_t requests=0,submits=0,completed=0,returned=0,published=0,q4=0,q6=0,weight_bytes=0,q8_bytes=0;
 uint64_t fence_wait_ns=0,copy_cpu_ns=0,dispatch_wall_ns=0,gpu_ticks=0,gpu_frequency=0;
 bool timestamp_available=false,terminal=true;uint64_t fence_value=0;
};
struct ExpertVector {
 Quant quant;uint32_t role,expert,k,n;const unsigned char* weights;size_t weight_bytes;
 const unsigned char* q8;size_t q8_bytes;
};
// Diagnostic observer sees only the exact held hybrid input and post-fence result.
// It must not retain pointers beyond observe(); source admission owns its lifetime.
class VectorObserver {
public:
 virtual ~VectorObserver()=default;
 virtual bool select(const ExpertVector&) noexcept=0;
 virtual void observe(const ExpertVector&,const float*cpu,const float*gpu)=0;
 virtual void unavailable() noexcept=0;
 virtual void oracle_completed() noexcept {} // Optional diagnostic timer boundary before GPU execute/fence.
};
template<class F> bool diagnostic_oracle(VectorObserver*observer,const ExpertVector&request,F&&compute)noexcept{
 if(!observer||!observer->select(request))return false;
 try{compute();observer->oracle_completed();return true;}catch(...){observer->unavailable();return false;}
}
inline void diagnostic_result(VectorObserver*observer,bool selected,const ExpertVector&request,const float*cpu,const float*gpu)noexcept{
 if(!selected)return;
 try{observer->observe(request,cpu,gpu);}catch(...){observer->unavailable();}
}
inline void check_vector(const ExpertVector& x){
 const bool shape=(x.k==2048&&x.n==1536&&x.quant==Quant::q4_k)||
  (x.k==1536&&x.n==2048&&(x.quant==Quant::q4_k||x.quant==Quant::q6_k));
 if(!shape||x.role>=114||x.expert>=64||!x.weights||!x.q8||
    x.weight_bytes!=mul(mul(x.k/256,block(x.quant).bytes),x.n)||x.q8_bytes!=mul(x.k/256,292))
  throw std::invalid_argument("hybrid exact Q4K/Q6K vector/extent refused");
}
inline size_t fill_q8_pattern(unsigned char*destination,size_t cap,uint32_t k,uint32_t pattern){
 const size_t bytes=size_t(k/256)*sizeof(block_q8_K);
 if(!destination||(k!=1536&&k!=2048)||pattern>1||bytes>cap||reinterpret_cast<uintptr_t>(destination)%alignof(block_q8_K))throw std::invalid_argument("primitive Q8 pattern envelope");
 auto*blocks=reinterpret_cast<block_q8_K*>(destination);
 for(uint32_t b=0;b<k/256;++b){blocks[b].d=pattern?1.0f/128:0;for(uint32_t i=0;i<256;++i)blocks[b].qs[i]=pattern?int8_t(int((i+17*b)%255)-127):0;for(uint32_t j=0;j<16;++j){int sum=0;for(uint32_t i=0;i<16;++i)sum+=blocks[b].qs[j*16+i];blocks[b].bsums[j]=int16_t(sum);}}
 return bytes;
}
static_assert(sizeof(block_q8_K)==292,"GPU shader Q8 ABI");
class ExpertGpu {
public:
 virtual ~ExpertGpu()=default;
 virtual void set_diagnostic_observer(std::shared_ptr<GpuDiagnosticObserver>){}
 virtual const float* execute(const ExpertVector&)=0; // scratch survives until next call; terminal on return
 virtual void request_stop()noexcept=0;
 virtual bool publish(const float* result,float* destination,size_t n)=0;
 virtual void drain()noexcept=0; // unknown waits; must NOT report closed or release anchors early
 virtual const char* error()const noexcept=0;
 virtual bool terminal()const noexcept=0;
 virtual GpuCounts counts()const=0;
 virtual GpuSnapshot snapshot()const=0;
 virtual size_t required_owned_bytes()const=0;
};
// Platform-boundary interface used by the SAME production state machine and fixture.
class ExpertQueue {
public:
 virtual ~ExpertQueue()=default;
 virtual void set_diagnostic_observer(std::shared_ptr<GpuDiagnosticObserver>){}
 virtual void stage_submit(const ExpertVector&)=0;
 virtual bool wait_terminal()noexcept=0; // false = UNKNOWN, never permission to release
 virtual const float* output()=0; // terminal GPU output; guards validated by platform
 virtual void release_terminal()noexcept=0;
 virtual bool submitted()const noexcept=0;
 virtual const char* error()const noexcept=0;
 virtual GpuSnapshot snapshot()const=0;
 virtual GpuCounts counts()const=0;
 virtual size_t owned_bytes()const=0;
};
class SynchronousExpertGpu final:public ExpertGpu {
 std::mutex publication_;std::shared_ptr<ExpertQueue> queue_;std::atomic<bool> stop_{false};bool busy_=false;
 char cause_[256]{};GpuCounts counts_{};std::array<float,2048> result_{};
 void latch(const char*s)noexcept{if(!cause_[0])std::snprintf(cause_,sizeof(cause_),"%s",s?s:"GPU unknown failure");}
public:
 explicit SynchronousExpertGpu(std::shared_ptr<ExpertQueue> q):queue_(std::move(q)){if(!queue_)throw std::invalid_argument("GPU queue missing");}
 ~SynchronousExpertGpu(){request_stop();drain();}
 void set_diagnostic_observer(std::shared_ptr<GpuDiagnosticObserver>o)override{if(busy_)throw std::logic_error("GPU observer install during submission");queue_->set_diagnostic_observer(std::move(o));}
 void request_stop()noexcept override{std::lock_guard<std::mutex> lock(publication_);stop_.store(true,std::memory_order_release);}
 bool publish(const float*result,float*destination,size_t n)override{
  std::lock_guard<std::mutex> lock(publication_);
  if(stop_.load()||cause_[0]||!terminal()||result!=result_.data()||!destination||!n||n>2048)return false;
  for(size_t i=0;i<n;++i)if(!std::isfinite(result[i]))return false;
  std::memcpy(destination,result,n*sizeof(float));++counts_.published;return true;
 }
 void drain()noexcept override{
  // Submitted work owns all memory through this loop. No timeout frees a pending lease.
  while(queue_->submitted()&&!queue_->wait_terminal())std::this_thread::yield();
  if(queue_->error()[0])latch(queue_->error());
  queue_->release_terminal();busy_=false;
 }
 const float* execute(const ExpertVector& x)override{
  if(cause_[0]||stop_.load()||busy_)throw std::runtime_error("hybrid stopped/failed/busy; no continuation");
  busy_=true;++counts_.requests;
  try{
   check_vector(x);
   {std::lock_guard<std::mutex> lock(publication_);if(stop_.load())throw std::runtime_error("hybrid stop before submit");queue_->stage_submit(x);}
   drain();
   if(cause_[0])throw std::runtime_error(cause_);
   if(stop_.load())throw std::runtime_error("hybrid stop before publication");
   const auto*p=queue_->output();if(!p)throw std::runtime_error("GPU terminal output missing");
   for(size_t i=0;i<x.n;++i)result_[i]=p[i]; // raw comparison may witness NaN; provider guards graph scatter
   if(stop_.load())throw std::runtime_error("hybrid final publication stopped");
   ++counts_.returned;(x.quant==Quant::q4_k?counts_.q4:counts_.q6)++;
   counts_.weight_bytes=add(counts_.weight_bytes,x.weight_bytes);counts_.q8_bytes=add(counts_.q8_bytes,x.q8_bytes);
   return result_.data();
  }catch(const std::exception&e){latch(e.what());stop_.store(true);drain();throw;}
   catch(...){latch("GPU unknown exception");stop_.store(true);drain();throw;}
 }
 const char* error()const noexcept override{return cause_;}
 bool terminal()const noexcept override{return !busy_&&!queue_->submitted();}
 GpuCounts counts()const override{if(!terminal())throw std::logic_error("GPU counters before terminal");auto x=queue_->counts();x.requests=counts_.requests;x.returned=counts_.returned;x.published=counts_.published;x.q4=counts_.q4;x.q6=counts_.q6;x.weight_bytes=counts_.weight_bytes;x.q8_bytes=counts_.q8_bytes;return x;}
 GpuSnapshot snapshot()const override{return queue_->snapshot();}
 size_t required_owned_bytes()const override{return size_t(add(sizeof(*this),queue_->owned_bytes()));}
};
}
