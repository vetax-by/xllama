#pragma once
#include "expert_cache.h"
#include "expert_diagnostics.h"
struct ggml_tensor;
namespace ua50 {
/* Diagnostic-only synchronous lease events. Borrowed bytes expire at return;
 * implementations copy bounded selections and cannot retain or release leases. */
struct DispatchObserver:GpuDiagnosticObserver {
 virtual ~DispatchObserver()=default;
 virtual void frame(uint32_t,uint32_t,int32_t)noexcept=0;
 virtual void acquired(const Tensor&,uint32_t,const unsigned char*,size_t)noexcept=0;
 virtual void binding(const ggml_tensor*,const ggml_tensor*,const ggml_tensor*)noexcept=0;
 virtual void input(uint32_t,uint32_t,const float*,size_t,const unsigned char*,size_t)noexcept=0;
 virtual bool oracle_range(uint32_t&,uint32_t&)noexcept{return false;}
 virtual void oracle(const float*,size_t,uint32_t,uint64_t,uint64_t)noexcept{}
 virtual void pre_publish(const float*,size_t,bool)noexcept=0;
 virtual void post_publish(const float*,size_t)noexcept=0;
 virtual void cpu_output(uint32_t,uint32_t,const float*,size_t)noexcept=0;
 virtual void released()noexcept=0;
 virtual void unavailable(const char*)noexcept=0;
 virtual bool io_timing_enabled()const noexcept{return false;}
 virtual void io_phase(bool)noexcept{}
 virtual void io_route(uint32_t,const int64_t*,int64_t)noexcept{}
 virtual void io_consumer(uint32_t,uint32_t)noexcept{}
 virtual void cache_event(const CacheEvent&)noexcept{}
 virtual void owner_drained()noexcept{} // Called only AFTER actual context/provider destruction and drain.
 virtual bool cpu_control_range(uint32_t,uint32_t,uint32_t&,uint32_t&)noexcept{return false;}
};
}
