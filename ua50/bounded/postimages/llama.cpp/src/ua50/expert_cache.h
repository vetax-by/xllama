#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>
#include <functional>
#include <chrono>

// Exact packed layout definitions from the installed llama submodule.
#define GGML_COMMON_DECL_CPP
#include "ggml-common.h"

namespace ua50 {
enum class Quant { iq3_s, q4_k, q4_0, q6_k, f16, f32 };
struct Block { uint64_t elements, bytes; };
inline Block block(Quant q) {
    switch (q) {
    case Quant::iq3_s: return {QK_K, sizeof(block_iq3_s)};
    case Quant::q4_k: return {QK_K, sizeof(block_q4_K)};
    case Quant::q4_0: return {QK4_0, sizeof(block_q4_0)};
    case Quant::q6_k: return {QK_K, sizeof(block_q6_K)};
    case Quant::f16: return {1, sizeof(ggml_half)};
    case Quant::f32: return {1, sizeof(float)};
    }
    throw std::invalid_argument("unsupported dtype");
}
inline uint64_t add(uint64_t a, uint64_t b) {
    if (a > UINT64_MAX-b) throw std::overflow_error("offset addition");
    return a+b;
}
inline uint64_t mul(uint64_t a, uint64_t b) {
    if (a && b > UINT64_MAX/a) throw std::overflow_error("size multiplication");
    return a*b;
}
struct Tensor {
    uint32_t id;
    Quant quant;
    uint64_t columns, rows, experts, offset, nb01, nb02;
};
// Reader must implement one bounded synchronous offset read into destination.
// It may return a short result; such a result never becomes visible as a lease.
// File identity/lifetime and provider exclusion from concurrent writers belong
// to the caller; this prototype does not parse or authenticate GGUF.
struct Reader {
    virtual ~Reader() = default;
    virtual size_t read(uint64_t offset, unsigned char * destination, size_t bytes) = 0;
};
struct Counters {
    uint64_t hits=0, misses=0, evictions=0, read_calls=0, read_bytes=0, failures=0;
    uint64_t requests=0,requested_bytes=0,hit_bytes=0;
    uint64_t acquire_ns_total=0,acquire_ns_max=0,reader_ns_total=0,reader_ns_max=0,clock_errors=0;
    size_t arena_bytes=0, metadata_bytes=0, peak_live_payload=0;
};
enum class CacheEventKind:uint32_t { acquire=1,hit=2,miss=3,evict=4,read_begin=5,read_end=6,read_error=7,lease=8,release=9 };
struct CacheEvent {CacheEventKind kind;uint32_t tensor=0,expert=0,slot=UINT32_MAX;uint64_t generation=0,offset=0,requested=0,returned=0,refs=0;uint32_t error=0;};
using CacheObserver=void(*)(const CacheEvent&,void*) noexcept;
class Cache {
    struct Slot {
        uint32_t tensor=0;
        uint64_t expert=0, age=0, generation=0;
        size_t bytes=0, refs=0;
        bool valid=false;
    };
    struct AlignedDelete { void operator()(unsigned char * p) const { ::operator delete(p, std::align_val_t(64)); } };
public:
    // Single coordinator only. Multiple simultaneously live leases are supported;
    // concurrent member-function calls require external serialization.
    class Lease {
        Cache * owner_=nullptr;
        size_t slot_=0;
        friend class Cache;
        Lease(Cache * c, size_t s):owner_(c),slot_(s) { ++c->slots_[s].refs; c->emit(CacheEventKind::lease,s); }
    public:
        Lease()=default;
        Lease(const Lease&)=delete;
        Lease& operator=(const Lease&)=delete;
        Lease(Lease&& x) noexcept:owner_(std::exchange(x.owner_,nullptr)),slot_(x.slot_) {}
        Lease& operator=(Lease&& x) noexcept {
            if (this != &x) { reset(); owner_=std::exchange(x.owner_,nullptr); slot_=x.slot_; }
            return *this;
        }
        ~Lease() { reset(); }
        void reset() noexcept { if(owner_) { --owner_->slots_[slot_].refs; owner_->emit(CacheEventKind::release,slot_); owner_=nullptr; } }
        const unsigned char * data() const {
            if(!owner_) throw std::logic_error("empty lease");
            return owner_->arena_.get()+slot_*owner_->stride_;
        }
        size_t size() const {
            if(!owner_) throw std::logic_error("empty lease");
            return owner_->slots_[slot_].bytes;
        }
    };
private:
    static constexpr size_t max_tensors=128;
    std::array<Tensor,max_tensors> tensors_{};
    size_t nt_=0, count_, stride_;
    uint64_t clock_=0;
    Reader & reader_;
    std::unique_ptr<unsigned char,AlignedDelete> arena_;
    std::unique_ptr<Slot[]> slots_;
    Counters c_;
    CacheObserver observer_=nullptr;void*observer_data_=nullptr;
    void emit(CacheEventKind kind,size_t slot,uint32_t tensor=UINT32_MAX,uint64_t expert=0,uint64_t offset=0,uint64_t requested=0,uint64_t returned=0,uint32_t error=0)noexcept{
        if(!observer_)return;
        CacheEvent e{};e.kind=kind;e.tensor=tensor;e.expert=uint32_t(expert);e.offset=offset;e.requested=requested;e.returned=returned;e.error=error;
        if(slot<count_){const auto&s=slots_[slot];e.slot=uint32_t(slot);e.generation=s.generation;e.refs=s.refs;if(tensor==UINT32_MAX){e.tensor=s.tensor;e.expert=uint32_t(s.expert);}}
        observer_(e,observer_data_);
    }
    std::function<int64_t()> timer_clock_;
    int64_t tick() noexcept {
        if(!timer_clock_)return -1;
        try{const auto t=timer_clock_();if(t>=0)return t;}catch(...){}
        ++c_.clock_errors;return -1;
    }
    struct Timer {
        Cache* c;bool reader;int64_t begin;
        Timer(Cache* p,bool r):c(p),reader(r),begin(p->tick()){}
        ~Timer() noexcept {
            if(!c->timer_clock_)return;
            const auto end=c->tick();
            if(begin<0 || end<begin){++c->c_.clock_errors;return;}
            auto& total=reader?c->c_.reader_ns_total:c->c_.acquire_ns_total;
            auto& maximum=reader?c->c_.reader_ns_max:c->c_.acquire_ns_max;
            const auto ns=uint64_t(end-begin);
            if(total>UINT64_MAX-ns){++c->c_.clock_errors;return;}
            total+=ns;if(ns>maximum)maximum=ns;
        }
    };
public:
    static size_t required_owned_bytes(size_t slots,size_t max_expert) {
        if(!slots || slots>1024 || !max_expert)throw std::invalid_argument("cache sizing configuration");
        uint64_t stride=add(max_expert,63)&~uint64_t(63);
        uint64_t total=add(mul(stride,slots),add(sizeof(Cache),mul(sizeof(Slot),slots)));
        if(total>SIZE_MAX)throw std::overflow_error("cache sizing host size");
        return size_t(total);
    }
    Cache(Reader & r, uint64_t file_bytes, const Tensor * descriptors, size_t n,
          size_t slot_count, size_t max_expert_bytes, size_t owned_budget=16*1024*1024):count_(slot_count),stride_(0),reader_(r) {
        if (!descriptors || !n || n>max_tensors || !slot_count || slot_count>1024 || !max_expert_bytes)
            throw std::invalid_argument("bounded cache configuration");
        const uint64_t padded=add(max_expert_bytes,63) & ~uint64_t(63);
        const uint64_t arena_bytes=mul(padded,slot_count);
        if(padded>SIZE_MAX || arena_bytes>SIZE_MAX) throw std::overflow_error("host size");
        const auto metadata=add(sizeof(*this),mul(sizeof(Slot),slot_count));
        if(add(arena_bytes,metadata)>owned_budget) throw std::invalid_argument("owned-memory budget exceeded");
        stride_=static_cast<size_t>(padded);
        for(size_t i=0;i<n;++i) {
            const auto & t=descriptors[i];
            const auto b=block(t.quant);
            if(!t.columns || !t.rows || !t.experts || t.experts>64 || t.columns%b.elements)
                throw std::invalid_argument("not contiguous LFM expert shape");
            const auto row_bytes=mul(t.columns/b.elements,b.bytes);
            const auto expert_bytes=mul(row_bytes,t.rows);
            if(t.nb01!=row_bytes || t.nb02!=expert_bytes || expert_bytes>max_expert_bytes)
                throw std::invalid_argument("row/expert stride or oversized expert");
            if(add(t.offset,mul(t.experts,t.nb02))>file_bytes)
                throw std::out_of_range("tensor outside held file");
            for(size_t j=0;j<i;++j) if(tensors_[j].id==t.id) throw std::invalid_argument("duplicate tensor id");
            tensors_[nt_++]=t;
        }
        // Payload storage and metadata are allocated once, never per miss.
        arena_.reset(static_cast<unsigned char *>(::operator new(static_cast<size_t>(arena_bytes),std::align_val_t(64))));
        slots_=std::make_unique<Slot[]>(count_);
        c_.arena_bytes=static_cast<size_t>(arena_bytes);
        c_.metadata_bytes=sizeof(*this)+sizeof(Slot)*count_;
    }
    Cache(const Cache&)=delete;
    Cache& operator=(const Cache&)=delete;
    ~Cache()=default; // All leases MUST be destroyed before Cache, like a view owner.
    void enable_timing(std::function<int64_t()> clock=[] {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }) { if(active_leases())throw std::logic_error("timing while leased");timer_clock_=std::move(clock); }
    void set_observer(CacheObserver callback,void*data)noexcept{observer_=callback;observer_data_=data;}
    const Counters & counters() const { return c_; }
    // Diagnostic only: caller must first drain all graph workers. Not OS I/O state.
    size_t active_leases() const noexcept {
        size_t n=0; for(size_t i=0;i<count_;++i) n+=slots_[i].refs; return n;
    }
    Lease acquire(uint32_t tensor_id, uint64_t expert) {
        Timer acquire_time(this,false);
        const Tensor * t=nullptr;
        for(size_t i=0;i<nt_;++i) if(tensors_[i].id==tensor_id) { t=&tensors_[i]; break; }
        if(!t || expert>=t->experts) throw std::out_of_range("expert/tensor id");
        if(clock_==UINT64_MAX) throw std::overflow_error("LRU clock exhausted");
        ++clock_;
        emit(CacheEventKind::acquire,count_,tensor_id,expert,add(t->offset,mul(expert,t->nb02)),t->nb02);
        ++c_.requests;c_.requested_bytes=add(c_.requested_bytes,t->nb02);
        for(size_t i=0;i<count_;++i) if(slots_[i].valid && slots_[i].tensor==tensor_id && slots_[i].expert==expert) {
            ++c_.hits;c_.hit_bytes=add(c_.hit_bytes,slots_[i].bytes); slots_[i].age=clock_;emit(CacheEventKind::hit,i,tensor_id,expert,add(t->offset,mul(expert,t->nb02)),t->nb02); return Lease(this,i);
        }
        size_t victim=count_;
        for(size_t i=0;i<count_;++i) if(slots_[i].refs==0 && (!slots_[i].valid || victim==count_ || slots_[i].age<slots_[victim].age)) {
            victim=i; if(!slots_[i].valid) break;
        }
        if(victim==count_) throw std::runtime_error("all cache slots pinned");
        ++c_.misses;
        auto & s=slots_[victim];
        if(s.valid){++c_.evictions;emit(CacheEventKind::evict,victim);}
        if(s.generation!=UINT64_MAX)++s.generation;
        emit(CacheEventKind::miss,victim,tensor_id,expert,add(t->offset,mul(expert,t->nb02)),t->nb02);
        s.valid=false; s.bytes=0;
        const auto offset=add(t->offset,mul(expert,t->nb02));
        const size_t bytes=static_cast<size_t>(t->nb02);
        size_t got=0;
        ++c_.read_calls;
        emit(CacheEventKind::read_begin,victim,tensor_id,expert,offset,bytes);
        try { Timer reader_time(this,true);got=reader_.read(offset,arena_.get()+victim*stride_,bytes); }
        catch(...) {emit(CacheEventKind::read_error,victim,tensor_id,expert,offset,bytes,0,1); ++c_.failures; throw; }
        emit(CacheEventKind::read_end,victim,tensor_id,expert,offset,bytes,got,got>bytes?3:got!=bytes?2:0);
        if(got>bytes) { ++c_.failures; throw std::runtime_error("reader over-report"); }
        c_.read_bytes+=got;
        if(got!=bytes) { ++c_.failures; throw std::runtime_error("short expert read"); }
        s.tensor=tensor_id; s.expert=expert; s.bytes=bytes; s.age=clock_; s.valid=true;
        size_t live=0;
        for(size_t i=0;i<count_;++i) if(slots_[i].valid) live+=slots_[i].bytes;
        if(live>c_.peak_live_payload) c_.peak_live_payload=live;
        return Lease(this,victim);
    }
};
}
