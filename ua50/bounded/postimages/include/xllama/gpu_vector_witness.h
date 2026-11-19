#pragma once
#include "ua50/expert_gpu.h"
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>

namespace xllama::gpu_proof {
// Model2 diagnostic only. No pointers or raw vectors survive observe().
class VectorWitness final : public ua50::VectorObserver {
public:
 static constexpr size_t count=12, output_cap=16384;
 // Includes one bounded 2.58 MiB hash copy, oracle stack, rows and serializer.
 static constexpr size_t charged_bytes=(3u<<20)+output_cap+65536;
 using Digest=std::function<std::string(const void*,size_t)>;
 struct Row {
  uint32_t ordinal=0,role=0,expert=0,k=0,n=0,first_difference=UINT32_MAX,first_violation=UINT32_MAX,violations=0,zero_reference_mismatches=0;
  uint64_t row_stride=0,expert_stride=0;
  std::string quant,q8_sha256,weight_sha256;
  double max_abs=0,max_ratio=0,max_relative_nonzero=0;
 };
private:
 Digest digest_;
 std::array<Row,count> rows_{};
 size_t seen_=0;
 bool active_=false,complete_=false,failed_=false,used_=false;
 std::chrono::steady_clock::time_point deadline_{};
 static constexpr uint32_t order_[count]={0,0,0,0,2,2,2,2,1,1,1,1};
 static void number(std::string&out,double x){char b[64];auto r=std::to_chars(b,b+sizeof(b),x,std::chars_format::general,17);if(r.ec!=std::errc{})throw std::runtime_error("vector witness number");out.append(b,r.ptr);}
 static bool hex(const std::string&s){if(s.size()!=64)return false;for(char c:s)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
 void timely()const{if(std::chrono::steady_clock::now()>deadline_)throw std::runtime_error("vector witness time bound");}
public:
 explicit VectorWitness(Digest digest):digest_(std::move(digest)){if(!digest_)throw std::invalid_argument("vector witness digest missing");}
 VectorWitness(const VectorWitness&)=delete;VectorWitness&operator=(const VectorWitness&)=delete;
 void begin(uint32_t phase,uint32_t position)noexcept{
  if(phase==2&&position==0){if(used_){unavailable();return;}used_=true;active_=true;deadline_=std::chrono::steady_clock::now()+std::chrono::seconds(30);}
 }
 bool select(const ua50::ExpertVector&) noexcept override{return active_&&!failed_&&seen_<count;}
 void unavailable()noexcept override{failed_=true;active_=false;}
 void observe(const ua50::ExpertVector&x,const float*cpu,const float*gpu) override{
  try{
   timely();if(!active_||failed_||seen_>=count||!cpu||!gpu||x.role!=order_[seen_]||x.expert>=64||
     x.n>2048||!x.n||!x.weights||!x.q8)throw std::runtime_error("vector witness role/extent/order");
   if(seen_>=4&&x.expert!=rows_[seen_%4].expert)throw std::runtime_error("vector witness expert order differs");
   if(seen_%4&&x.expert<=rows_[seen_-1].expert)throw std::runtime_error("vector witness expert monotonic order");
   if((x.role==1&&(x.quant!=ua50::Quant::q6_k||x.k!=1536||x.n!=2048))||
      (x.role!=1&&(x.quant!=ua50::Quant::q4_k||x.k!=2048||x.n!=1536)))
    throw std::runtime_error("vector witness first MoE role shape");
   ua50::check_vector(x);Row row;row.ordinal=uint32_t(seen_);row.role=x.role;row.expert=x.expert;row.k=x.k;row.n=x.n;
   row.quant=x.quant==ua50::Quant::q4_k?"Q4_K":x.quant==ua50::Quant::q6_k?"Q6_K":"";
   row.row_stride=ua50::mul(x.k/256,ua50::block(x.quant).bytes);row.expert_stride=x.weight_bytes;
   if(row.quant.empty()||row.row_stride*x.n!=x.weight_bytes)throw std::runtime_error("vector witness weight stride");
   row.q8_sha256=digest_(x.q8,x.q8_bytes);row.weight_sha256=digest_(x.weights,x.weight_bytes);
   if(!hex(row.q8_sha256)||!hex(row.weight_sha256))throw std::runtime_error("vector witness digest syntax");
   for(uint32_t i=0;i<x.n;++i){
    if(!std::isfinite(cpu[i])||!std::isfinite(gpu[i]))throw std::runtime_error("vector witness nonfinite");
    uint32_t cpu_bits=0,gpu_bits=0;std::memcpy(&cpu_bits,cpu+i,4);std::memcpy(&gpu_bits,gpu+i,4);
    if(cpu_bits!=gpu_bits&&row.first_difference==UINT32_MAX)row.first_difference=i;
    const double diff=std::abs(double(cpu[i])-double(gpu[i]));
    const double tolerance=0.001+0.001*std::abs(double(cpu[i]));
    if(diff>row.max_abs)row.max_abs=diff;
    const double ratio=diff/tolerance;if(ratio>row.max_ratio)row.max_ratio=ratio;
    if(cpu[i]==0){if(diff>0)++row.zero_reference_mismatches;}
    else {const double relative=diff/std::abs(double(cpu[i]));if(relative>row.max_relative_nonzero)row.max_relative_nonzero=relative;}
    if(diff>tolerance){if(row.first_violation==UINT32_MAX)row.first_violation=i;++row.violations;}
   }
   timely();rows_[seen_]=std::move(row);++seen_;
  }catch(...){unavailable();}
 }
 void finish()noexcept{
  if(active_){active_=false;if(std::chrono::steady_clock::now()>deadline_||failed_||seen_!=count)unavailable();else complete_=true;}
 }
 bool complete()const noexcept{return complete_&&!failed_&&seen_==count;}
 std::string summary()const{
  if(!complete())throw std::runtime_error("vector witness unavailable");
  std::string s="{\"schema\":\"ua68-model2-first-vector-witness-v1\",\"complete\":true,\"phase\":2,\"position\":0,\"rows\":[";
  for(size_t i=0;i<count;++i){const auto&r=rows_[i];if(i)s+=",";
   s+="{\"ordinal\":"+std::to_string(r.ordinal)+",\"role\":"+std::to_string(r.role)+",\"expert\":"+std::to_string(r.expert)+",\"quant\":\""+r.quant+"\",\"k\":"+std::to_string(r.k)+",\"n\":"+std::to_string(r.n)+",\"row_stride\":"+std::to_string(r.row_stride)+",\"expert_stride\":"+std::to_string(r.expert_stride)+",\"q8_sha256\":\""+r.q8_sha256+"\",\"weight_sha256\":\""+r.weight_sha256+"\",\"finite_pairs\":"+std::to_string(r.n)+",\"first_difference\":"+(r.first_difference==UINT32_MAX?"null":std::to_string(r.first_difference))+",\"violations\":"+std::to_string(r.violations)+",\"first_violation\":"+(r.first_violation==UINT32_MAX?"null":std::to_string(r.first_violation))+",\"zero_reference_mismatches\":"+std::to_string(r.zero_reference_mismatches)+",\"max_abs\":";
   number(s,r.max_abs);s+=",\"max_ratio\":";number(s,r.max_ratio);s+=",\"max_relative_nonzero\":";number(s,r.max_relative_nonzero);s+="}";
  }
  s+="]}";if(s.size()>output_cap)throw std::length_error("vector witness summary cap");return s;
 }
};
}
