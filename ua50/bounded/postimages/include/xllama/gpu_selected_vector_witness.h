#pragma once
#include "gpu_diagnostic_plan.h"
#include "ua50/expert_gpu.h"
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace xllama::gpu_proof {
// Optional held-hybrid oracle. Selection is by semantic position/role/expert, never pointer.
class SelectedVectorWitness final : public ua50::VectorObserver {
public:
 using Digest=std::function<std::string(const void*,size_t)>;
 struct Row {
  gpu_diagnostic::VectorKey key;uint32_t k=0,n=0,finite_pairs=0,violations=0,first_difference=UINT32_MAX,first_violation=UINT32_MAX;
  uint64_t row_stride=0,expert_stride=0;std::string quant,q8_sha256,weight_sha256;double max_abs=0,max_ratio=0;bool seen=false;
 };
private:
 gpu_diagnostic::Plan plan_;Digest digest_;std::vector<Row> rows_;
 size_t observed_=0,selected_=SIZE_MAX;uint32_t phase_=0,position_=0;bool active_=false,failed_=false,oracle_done_=false;const char*reason_=nullptr;uint64_t work_ns_=0,work_units_=0;std::chrono::steady_clock::time_point selected_start_{};
 static bool hex(const std::string&s){if(s.size()!=64)return false;for(char c:s)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
 static void real(std::string&s,double x){char b[64];auto r=std::to_chars(b,b+sizeof(b),x,std::chars_format::general,17);if(r.ec!=std::errc{})throw std::runtime_error("vector numeric serialization");s.append(b,r.ptr);}
 void fail(const char*s)noexcept{failed_=true;if(!reason_)reason_=s;}
 void account_elapsed()noexcept{
  if(selected_==SIZE_MAX)return;
  const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-selected_start_).count();
  if(elapsed<0||uint64_t(elapsed)>UINT64_MAX-work_ns_){fail("VECTOR_TIME_ACCOUNTING");return;}
  work_ns_+=uint64_t(elapsed);if(work_ns_>uint64_t(plan_.time_budget_ms)*1000000ull)fail("VECTOR_TIME_BUDGET");
 }
public:
 SelectedVectorWitness(gpu_diagnostic::Plan plan,Digest digest):plan_(std::move(plan)),digest_(std::move(digest)){
  if(!digest_)throw std::invalid_argument("vector digest missing");for(const auto&k:plan_.vectors){Row r;r.key=k;rows_.push_back(std::move(r));}
 }
 void begin(uint32_t phase,uint32_t position)noexcept{phase_=phase;position_=position;selected_=SIZE_MAX;oracle_done_=false;active_=phase==2&&!failed_;}
 bool select(const ua50::ExpertVector&x)noexcept override{
  if(!active_||selected_!=SIZE_MAX)return false;
  for(size_t i=0;i<rows_.size();++i){const auto&k=rows_[i].key;
   if(!rows_[i].seen&&k.position==position_&&k.role==x.role&&k.expert==x.expert){selected_=i;oracle_done_=false;selected_start_=std::chrono::steady_clock::now();return true;}}
  return false;
 }
 void oracle_completed()noexcept override{account_elapsed();oracle_done_=true;}
 void unavailable()noexcept override{if(!oracle_done_)account_elapsed();fail("VECTOR_OBSERVATION_UNAVAILABLE");selected_=SIZE_MAX;active_=false;}
 void observe(const ua50::ExpertVector&x,const float*cpu,const float*gpu)override{
  if(failed_){selected_=SIZE_MAX;return;}
  selected_start_=std::chrono::steady_clock::now();oracle_done_=false;
  try{
   if(selected_>=rows_.size()||!cpu||!gpu)throw std::logic_error("VECTOR_IDENTITY");
   auto&r=rows_[selected_];if(r.key.position!=position_||r.key.role!=x.role||r.key.expert!=x.expert||r.seen)throw std::logic_error("VECTOR_IDENTITY");
   ua50::check_vector(x);
   r.k=x.k;r.n=x.n;r.quant=x.quant==ua50::Quant::q4_k?"Q4_K":"Q6_K";r.row_stride=ua50::mul(x.k/256,ua50::block(x.quant).bytes);r.expert_stride=x.weight_bytes;
   if(r.row_stride*x.n!=x.weight_bytes)throw std::runtime_error("VECTOR_STRIDE");
   r.q8_sha256=digest_(x.q8,x.q8_bytes);r.weight_sha256=digest_(x.weights,x.weight_bytes);
   if(!hex(r.q8_sha256)||!hex(r.weight_sha256))throw std::runtime_error("VECTOR_DIGEST");
   for(uint32_t i=0;i<x.n;++i){if(!std::isfinite(cpu[i])||!std::isfinite(gpu[i]))throw std::runtime_error("VECTOR_NONFINITE");
    uint32_t a=0,b=0;std::memcpy(&a,cpu+i,4);std::memcpy(&b,gpu+i,4);if(a!=b&&r.first_difference==UINT32_MAX)r.first_difference=i;
    const double delta=std::abs(double(cpu[i])-double(gpu[i]));const double tolerance=(double(plan_.analysis_abs_ppm)+double(plan_.analysis_rel_ppm)*std::abs(double(cpu[i])))/1000000.0;
    if(delta>r.max_abs)r.max_abs=delta;const double ratio=tolerance>0?delta/tolerance:delta>0?1e300:0;if(ratio>r.max_ratio)r.max_ratio=ratio;
    if(delta>tolerance){if(r.first_violation==UINT32_MAX)r.first_violation=i;++r.violations;}++r.finite_pairs;
   }
   work_units_+=uint64_t(x.k)*x.n+x.n;if(work_units_>plan_.reference_work_budget)fail("VECTOR_WORK_BUDGET");
   r.seen=true;++observed_;
  }catch(...){fail("VECTOR_OBSERVATION_FAILURE");}
  account_elapsed();selected_=SIZE_MAX;
 }
 void finish(uint32_t phase,uint32_t position)noexcept{
  if(phase==2&&!failed_)for(const auto&r:rows_)if(r.key.position==position&&!r.seen)fail("VECTOR_MISSING_SELECTION");
  selected_=SIZE_MAX;active_=false;
 }
 bool complete()const noexcept{return !failed_&&observed_==rows_.size();}
 std::string summary()const{
  std::string s="{\"schema\":\"ua68-model2-selected-vectors-v1\",\"complete\":"+std::string(complete()?"true":"false")+",\"unavailable\":"+(failed_?"true":"false")+",\"reason\":";
  s+=reason_?std::string("\"")+reason_+"\"":"null";s+=",\"expected\":"+std::to_string(rows_.size())+",\"observed\":"+std::to_string(observed_)+",\"diagnostic_work_ns\":"+std::to_string(work_ns_)+",\"diagnostic_work_units\":"+std::to_string(work_units_)+",\"rows\":[";
  for(size_t i=0;i<rows_.size();++i){const auto&r=rows_[i];if(i)s+=",";
   s+="{\"phase\":2,\"position\":"+std::to_string(r.key.position)+",\"role\":"+std::to_string(r.key.role)+",\"expert\":"+std::to_string(r.key.expert)+",\"seen\":"+(r.seen?"true":"false");
   if(r.seen){s+=",\"quant\":\""+r.quant+"\",\"k\":"+std::to_string(r.k)+",\"n\":"+std::to_string(r.n)+",\"row_stride\":"+std::to_string(r.row_stride)+",\"expert_stride\":"+std::to_string(r.expert_stride)+",\"q8_sha256\":\""+r.q8_sha256+"\",\"weight_sha256\":\""+r.weight_sha256+"\",\"finite_pairs\":"+std::to_string(r.finite_pairs)+",\"violations\":"+std::to_string(r.violations)+",\"first_difference\":"+(r.first_difference==UINT32_MAX?"null":std::to_string(r.first_difference)) +",\"first_violation\":"+(r.first_violation==UINT32_MAX?"null":std::to_string(r.first_violation)) +",\"max_abs\":";real(s,r.max_abs);s+=",\"max_ratio\":";real(s,r.max_ratio);}
   s+="}";
  }s+="]}";if(s.size()>plan_.output_budget)throw std::length_error("vector output budget");return s;
 }
};
}
