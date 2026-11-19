#pragma once
#include "expert_cache.h"
#include <array>
#include <memory>
namespace ua50 {
struct ExpertVector;
inline constexpr uint64_t diagnostic_echo_reserve=131072,diagnostic_trace_driver_reserve=8ull<<20;
struct GpuDiagnosticRequest {bool echo=false,trace=false;uint32_t row=0,rows=0,block=0,blocks=0,trace_row=0,trace_rows=0;};
struct GpuDiagnosticEvidence {
 const char*status="UNSUPPORTED_BACKEND";const unsigned char*weights=nullptr,*q8=nullptr;size_t weight_bytes=0,q8_bytes=0;
 const float*local_accumulator=nullptr,*reduction_final=nullptr;size_t trace_values=0;
 std::array<uint32_t,8>constants{};uint64_t resource_generation=0,submission_id=0,fence=0,completed=0,copy_ns=0;
 uint32_t pso=0;const char*shader_sha256="";bool guard=false,copy_before_dispatch=false;
};
class GpuDiagnosticObserver {
public:
 virtual ~GpuDiagnosticObserver()=default;
 virtual bool gpu_request(const ExpertVector&,GpuDiagnosticRequest&)noexcept{return false;}
 virtual void gpu_evidence(const ExpertVector&,const GpuDiagnosticRequest&,const GpuDiagnosticEvidence&)noexcept{}
};
// All byte ranges are a closed transformation of selected expert row/block ranges.
struct GpuDiagnosticLayout {uint64_t row_bytes=0,selected_row_bytes=0,weight_bytes=0,q8_bytes=0,trace_values=0,trace_bytes=0;};
inline GpuDiagnosticLayout diagnostic_layout(const GpuDiagnosticRequest&r,uint32_t k,uint32_t n,Quant q){
 GpuDiagnosticLayout d;d.row_bytes=mul(k/256,block(q).bytes);d.selected_row_bytes=mul(r.blocks,block(q).bytes);d.weight_bytes=mul(r.rows,d.selected_row_bytes);d.q8_bytes=mul(k/256,292);
 if((!r.echo&&!r.trace)||!k||k%256||!n||!r.rows||r.row>=n||r.rows>n-r.row||!r.blocks||r.block>=k/256||r.blocks>k/256-r.block||d.weight_bytes>65536||d.q8_bytes>65536)throw std::invalid_argument("GPU diagnostic exact bounded ranges");
 if(r.trace){if(!r.trace_rows||r.trace_rows>64||r.trace_row>=n||r.trace_rows>n-r.trace_row)throw std::invalid_argument("GPU trace selected output range");d.trace_values=mul(r.trace_rows,64);d.trace_bytes=mul(d.trace_values,8);}
 return d;
}
}
