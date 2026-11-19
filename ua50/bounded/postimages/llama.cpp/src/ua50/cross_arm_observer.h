#pragma once
#include <cstddef>
#include <cstdint>

namespace ua50 {
struct HeldWeightQuery {
    uint32_t phase=0,position=0,layer=0,role=0,expert=0,slot=0,input_row=0,occurrence=0;
    int32_t token=-1;
    size_t row_index=0;
};
// Diagnostic-only observer. Each call is synchronous, while the expert lease,
// packed workspace and graph output remain live. Implementations must copy any
// value they retain and must never throw across the C/ggml worker boundary.
struct CrossArmObserver {
    virtual ~CrossArmObserver() = default;
    virtual void frame(uint32_t phase,uint32_t position,int32_t token) noexcept = 0;
    virtual void input(uint32_t role,uint32_t expert,uint32_t slot,uint32_t input_row,
                       const float * f32,size_t k,const unsigned char * q8,size_t q8_bytes) noexcept = 0;
    virtual void output(uint32_t role,uint32_t expert,uint32_t slot,uint32_t input_row,
                        const float * values,size_t n) noexcept = 0;
    virtual void unavailable(const char * reason) noexcept = 0;
    // Optional held-weight diagnostic. The caller supplies a bounded copy buffer;
    // no pointer into an observer or lease escapes this synchronous call.
    virtual bool held_reference(uint32_t,uint32_t,const unsigned char *,size_t,
                                unsigned char *,size_t,HeldWeightQuery &) noexcept { return false; }
    virtual void held_result(const HeldWeightQuery &,const float *,const float *,
                             const float *,size_t) noexcept {}
    virtual void held_unavailable(const HeldWeightQuery &,const char *) noexcept {}
    virtual void held_charge(const HeldWeightQuery &,uint64_t,uint64_t) noexcept {}
    virtual void held_vector_done() noexcept {}
};
}
