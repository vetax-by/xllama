#pragma once
#include "ggml-ua50.h"
#include "ggml-backend-impl.h"
#include "expert_cache.h"
#include "expert_gpu.h"
#include "cross_arm_observer.h"
#include "dispatch_observer.h"
#include <array>
#include <memory>
#include <functional>
namespace ua50 {
// One immutable file, one CPU graph at a time. No public xllama enablement yet.
class NativeProvider {
    struct Record { ggml_tensor * tensor=nullptr; Tensor layout{}; };
    std::shared_ptr<Reader> reader_owner_; // declared first: outlives cache/leases
    Reader & reader_;
    uint64_t file_bytes_;
    size_t slots_, max_expert_, budget_, count_=0;
    std::array<Record,128> records_{};
    std::array<Tensor,128> descriptors_{};
    std::unique_ptr<Cache> cache_;
    Cache::Lease active_;
    std::shared_ptr<ExpertGpu> gpu_;
    std::shared_ptr<VectorObserver> vector_observer_;
    std::shared_ptr<CrossArmObserver> cross_arm_observer_;
    std::shared_ptr<DispatchObserver> dispatch_observer_;
    uint64_t active_expert_=0;
    const ggml_tensor * active_tensor_=nullptr;
    char error_[256]{};
    std::function<void(uint32_t,uint64_t,uint64_t,uint64_t)> trace_;
    ggml_backend_buffer_type buft_{};
    void selected_cpu_dot(const ggml_tensor*, const unsigned char*, size_t, uint32_t, uint32_t) noexcept;
public:
    NativeProvider(Reader &,uint64_t file_bytes,size_t slots,size_t max_expert,size_t total_owned_budget);
    NativeProvider(std::shared_ptr<Reader>,uint64_t file_bytes,size_t slots,size_t max_expert,size_t total_owned_budget);
    ~NativeProvider();
    NativeProvider(const NativeProvider&)=delete;
    NativeProvider& operator=(const NativeProvider&)=delete;
    ggml_backend_buffer_type_t buffer_type() { return &buft_; }
    void register_tensor(ggml_tensor *,uint16_t file_index,uint64_t absolute_offset);
    void finalize();
    void validate_hybrid_catalog()const;
    const ggml_tensor* tensor_named(const char*)const;
    uint32_t role_id(const ggml_tensor*t)const{for(size_t i=0;i<count_;++i)if(records_[i].tensor==t)return uint32_t(i);throw std::invalid_argument("role unregistered");}
    void visit_registry(const std::function<void(uint32_t,const Tensor&,const char*)>& f)const{for(size_t i=0;i<count_;++i)f(uint32_t(i),records_[i].layout,ggml_get_name(records_[i].tensor));}
    bool registered(const ggml_tensor *) const;
    bool acquire(const ggml_tensor *,uint64_t) noexcept;
    void release() noexcept;
    void set_gpu(std::shared_ptr<ExpertGpu> g){if(active_tensor_||cache_||!g)throw std::logic_error("GPU install before finalize only");gpu_=std::move(g);}
    void set_vector_observer(std::shared_ptr<VectorObserver> v){if(active_tensor_||!gpu_||!v)throw std::logic_error("vector witness setup on busy/nonhybrid provider");vector_observer_=std::move(v);}
    void set_cross_arm_observer(std::shared_ptr<CrossArmObserver> v){if(active_tensor_||!v)throw std::logic_error("cross-arm witness setup on busy provider");cross_arm_observer_=std::move(v);}
    void set_dispatch_observer(std::shared_ptr<DispatchObserver> v){if(active_tensor_||!v)throw std::logic_error("dispatch observer setup on busy provider");dispatch_observer_=std::move(v);if(gpu_)gpu_->set_diagnostic_observer(dispatch_observer_);if(cache_)cache_->set_observer([](const CacheEvent&e,void*p)noexcept{auto*self=static_cast<NativeProvider*>(p);if(self->dispatch_observer_)self->dispatch_observer_->cache_event(e);},this);}
    void diagnostic_frame(uint32_t phase,uint32_t position,int32_t token) noexcept {if(cross_arm_observer_)cross_arm_observer_->frame(phase,position,token);if(dispatch_observer_)dispatch_observer_->frame(phase,position,token);}
    void io_known(const ggml_tensor *tensor,const int64_t *counts,int64_t experts) noexcept {
        if (!dispatch_observer_ || !dispatch_observer_->io_timing_enabled()) return;
        for (size_t i=0;i<count_;++i) if (records_[i].tensor==tensor) {
            dispatch_observer_->io_route(records_[i].layout.id,counts,experts);
            return;
        }
    }
    void io_consumer(const ggml_tensor *tensor) noexcept {
        if (!dispatch_observer_ || !dispatch_observer_->io_timing_enabled() || tensor!=active_tensor_) return;
        for (size_t i=0;i<count_;++i) if (records_[i].tensor==tensor) {
            dispatch_observer_->io_consumer(records_[i].layout.id,uint32_t(active_expert_));
            return;
        }
    }
    void diagnostic_binding(const ggml_tensor*weight,const ggml_tensor*input,const ggml_tensor*output){if(dispatch_observer_&&active_tensor_==weight)dispatch_observer_->binding(weight,input,output);}
    void diagnostic_input(const ggml_tensor*,uint32_t slot,uint32_t row,const float*,size_t,const unsigned char*,size_t) noexcept;
    void diagnostic_output(const ggml_tensor*,uint32_t slot,uint32_t row,const float*,size_t) noexcept;
    bool has_cross_arm_observer()const noexcept{return bool(cross_arm_observer_)||bool(dispatch_observer_);}
    void diagnostic_unavailable(const char*reason)noexcept{if(cross_arm_observer_)cross_arm_observer_->unavailable(reason);if(dispatch_observer_)dispatch_observer_->unavailable(reason);}
    bool has_gpu()const noexcept{return bool(gpu_);}
    bool gpu_vector(const ggml_tensor*,const void*,size_t,float*,size_t) noexcept;
    void set_trace(std::function<void(uint32_t,uint64_t,uint64_t,uint64_t)> trace) {
        if(active_tensor_ || error_[0])throw std::logic_error("trace setup on busy/failed provider");
        trace_=std::move(trace);
    }
    const char * data(const ggml_tensor *) const noexcept;
    const char * error() const { return error_; }
    static size_t required_owned_bytes(size_t slots,size_t max_expert);
    size_t requested_owned_bytes() const;
    size_t slot_count() const{return slots_;}
    size_t reserved_budget() const { return budget_; }
    bool owns_reader() const noexcept { return bool(reader_owner_); }
    const Counters & counters() const;
    void enable_timing(){if(!cache_)throw std::logic_error("timing cache missing");cache_->enable_timing();}
    // Only inspect after synchronous graph drain; does not report pending native OS I/O.
    size_t active_leases() const noexcept { return cache_ ? cache_->active_leases() : 0; }
};
}
