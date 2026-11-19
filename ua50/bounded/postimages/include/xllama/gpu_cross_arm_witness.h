#pragma once
#include "ua50/cross_arm_observer.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace xllama::gpu_proof {
// Synchronous diagnostic copy at the common CPU pack barrier. The only retained
// bytes are bounded CPU-reference rows; hybrid data is compared before return.
class CrossArmWitness final : public ua50::CrossArmObserver {
public:
 struct Key {uint32_t position,role,expert,slot,input_row,occurrence;};
 static constexpr uint32_t any_slot=4;
 struct HeldSelection {Key key;uint32_t layer;int32_t token;};
 struct HeldRow {
  HeldSelection selected{};size_t cross_index=0;
  bool cpu_input=false,hybrid_input=false,computed=false;
  const char* gap=nullptr;uint32_t observed_slot[2]{any_slot,any_slot};
  uint32_t n=0,bit_differences[4]{},violations[4]{};
  double max_abs[4]{};uint64_t hash[4]{};
  std::array<unsigned char,2336> hybrid_q8{};
  std::array<float,2048> raw_x{},raw_h{};
 };
 struct Record {
  Key key{};int32_t token[2]{-1,-1};uint32_t observed_slot[2]{any_slot,any_slot};uint32_t input_calls[2]{},output_calls[2]{};
  bool input_seen[2]{},output_seen[2]{};bool q8_equal=false,f32_equal=false,output_equal=false;
  uint32_t f32_violations=0,output_violations=0,q8_byte_differences=0,zero_blocks[2]{},bad_bsums[2]{},nonfinite_scales[2]{};
  uint32_t scale_diff_blocks=0,qs_diff_bytes=0,bsums_diff_bytes=0;
  double f32_max_abs=0,output_max_abs=0;
  uint64_t f32_hash[2]{},q8_hash[2]{},output_hash[2]{};
  uint32_t k=0,n=0;std::array<float,2048> cpu_f32{},cpu_output{};
  std::array<unsigned char,2336> cpu_q8{};
  std::array<float,2048> hybrid_f32{},hybrid_output{};std::array<unsigned char,2336> hybrid_q8{};
 };
 static constexpr size_t max_records=16,max_q8_bytes=2336;
private:
 std::vector<Record> rows_;std::vector<HeldRow> held_;bool held_raw_=false;
 uint32_t phase_=0,position_=0,prompt_count_=UINT32_MAX;int32_t token_=-1;
 size_t last_hybrid_=SIZE_MAX;uint64_t held_work_=0,held_ns_=0,held_work_cap_=0,held_ns_cap_=0;
 bool unavailable_=false,raw_=false;const char*reason_=nullptr;uint64_t work_ns_=0,work_units_=0;
 uint64_t max_work_,max_time_ns_;uint32_t abs_ppm_,rel_ppm_;
 static uint64_t hash(const void*data,size_t n)noexcept{
  const auto*p=static_cast<const unsigned char*>(data);uint64_t h=14695981039346656037ull;
  for(size_t i=0;i<n;++i){h^=p[i];h*=1099511628211ull;}return h;
 }
 static bool same(const Key&a,const Key&b)noexcept{
  return a.position==b.position&&a.role==b.role&&a.expert==b.expert&&a.slot==b.slot&&a.input_row==b.input_row&&a.occurrence==b.occurrence;
 }
 static void compare_held(const float*a,const float*b,size_t n,uint32_t abs_ppm,uint32_t rel_ppm,
                          uint32_t&bits,uint32_t&violations,double&maximum)noexcept{
  for(size_t i=0;i<n;++i){uint32_t x=0,y=0;std::memcpy(&x,a+i,4);std::memcpy(&y,b+i,4);
   bits+=x!=y;const double delta=std::abs(double(a[i])-double(b[i]));maximum=std::max(maximum,delta);
   violations+=delta>(double(abs_ppm)+double(rel_ppm)*std::abs(double(a[i])))/1e6;
  }
 }
 void fail(const char*why)noexcept{unavailable_=true;if(!reason_)reason_=why;}
 void charge(std::chrono::steady_clock::time_point start,uint64_t units)noexcept{
  auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
  if(ns<0||uint64_t(ns)>UINT64_MAX-work_ns_||units>UINT64_MAX-work_units_){fail("ACCOUNTING");return;}
  work_ns_+=uint64_t(ns);work_units_+=units;
  if(work_ns_>max_time_ns_||work_units_>max_work_)fail("BUDGET");
 }
 static void q8_check(const unsigned char*q,size_t k,uint32_t&zeros,uint32_t&bad,uint32_t&nonfinite)noexcept{
  for(size_t b=0;b<k/256;++b){const auto*p=q+b*292;float d=0;std::memcpy(&d,p,4);
   if(!std::isfinite(d))++nonfinite;
   bool zero=true;
   for(size_t i=0;i<256;++i)if(p[4+i])zero=false;
   if(zero)++zeros;
   for(size_t group=0;group<16;++group){int sum=0;for(size_t i=0;i<16;++i)sum+=int(int8_t(p[4+group*16+i]));
    int16_t stored=0;std::memcpy(&stored,p+260+group*2,2);if(stored!=sum)++bad;}
  }
 }
 static void compare(const float*cpu,const float*other,size_t n,uint32_t abs_ppm,uint32_t rel_ppm,
                     bool&equal,uint32_t&violations,double&max_abs)noexcept{
  equal=true;for(size_t i=0;i<n;++i){uint32_t a=0,b=0;std::memcpy(&a,cpu+i,4);std::memcpy(&b,other+i,4);
   if(a!=b)equal=false;
   const double delta=std::abs(double(cpu[i])-double(other[i]));
   max_abs=std::max(max_abs,delta);
   if(delta>(double(abs_ppm)+double(rel_ppm)*std::abs(double(cpu[i])))/1e6)++violations;
  }
 }
public:
 void configure(uint32_t prompt_count)noexcept{prompt_count_=prompt_count;}
 void select_held(std::vector<HeldSelection> chosen,uint64_t max_work,uint32_t max_time_ms,bool raw){
  if(chosen.empty()||chosen.size()>8||!held_.empty()||max_work==0||max_work>120000000||
     max_time_ms<1000||max_time_ms>120000)throw std::invalid_argument("held-weight selection bounds");
  if(raw&&(!raw_||chosen.size()!=1))throw std::invalid_argument("held-weight private raw bound");
  held_raw_=raw;
  held_work_cap_=max_work;held_ns_cap_=uint64_t(max_time_ms)*1000000;
  for(const auto&s:chosen){
   if(s.layer<2||s.layer>39||s.key.role/3!=s.layer-2||s.token<0||s.token>=128000)
    throw std::invalid_argument("held-weight layer/token/role identity");
   size_t found=SIZE_MAX;
   for(size_t i=0;i<rows_.size();++i)if(same(s.key,rows_[i].key)){
    if(found!=SIZE_MAX)throw std::invalid_argument("held-weight ambiguous cross key");found=i;
   }
   if(found==SIZE_MAX)throw std::invalid_argument("held-weight row requires exact cross selection");
   for(const auto&h:held_)if(same(s.key,h.selected.key))throw std::invalid_argument("duplicate held-weight selection");
   held_.emplace_back();held_.back().selected=s;held_.back().cross_index=found;
  }
 }
 CrossArmWitness(std::vector<Key>keys,uint32_t abs_ppm,uint32_t rel_ppm,uint64_t max_work,uint32_t max_time_ms,bool raw=false)
  :raw_(raw),max_work_(max_work),max_time_ns_(uint64_t(max_time_ms)*1000000),abs_ppm_(abs_ppm),rel_ppm_(rel_ppm){
  if(keys.empty()||keys.size()>max_records||(raw&&keys.size()>8)||max_time_ms<1000||max_time_ms>120000||max_work==0||max_work>120000000||abs_ppm>1000000||rel_ppm>1000000)throw std::invalid_argument("cross-arm bounds");
  for(const auto&key:keys){
   if(key.position>128||key.role>113||key.expert>63||key.slot>any_slot||key.input_row>128||key.occurrence>3)throw std::invalid_argument("cross-arm identity range");
   for(const auto&row:rows_)if((same(key,row.key))||
    (key.position==row.key.position&&key.role==row.key.role&&key.expert==row.key.expert&&
     key.input_row==row.key.input_row&&key.occurrence==row.key.occurrence&&
     (key.slot==any_slot||row.key.slot==any_slot)))throw std::invalid_argument("cross-arm overlapping selection");
   rows_.emplace_back();rows_.back().key=key;
  }
 }
 void frame(uint32_t phase,uint32_t position,int32_t token)noexcept override{
  if((phase!=1&&phase!=2)||position>128||token<0){fail("FRAME_IDENTITY");return;}
  phase_=phase;position_=position;token_=token;last_hybrid_=SIZE_MAX;
 }
 void input(uint32_t role,uint32_t expert,uint32_t slot,uint32_t input_row,const float*f32,size_t k,
            const unsigned char*q8,size_t bytes)noexcept override{
  const auto start=std::chrono::steady_clock::now();if(unavailable_)return;
  if(!phase_||!f32||!q8||!(k==1536||k==2048)||bytes!=k/256*292||bytes>max_q8_bytes){fail("INPUT_SHAPE");return;}
  for(auto&r:rows_)if(r.key.position==position_&&r.key.role==role&&r.key.expert==expert&&(r.key.slot==any_slot||r.key.slot==slot)&&r.key.input_row==input_row){
   const auto arm=phase_-1,call=r.input_calls[arm]++;if(call!=r.key.occurrence)continue;
   if(r.input_seen[arm]||(r.k&&r.k!=k)){fail("INPUT_ORDER_SHAPE");return;}
   for(size_t i=0;i<k;++i)if(!std::isfinite(f32[i])){fail("NONFINITE_F32");return;}
   r.input_seen[arm]=true;r.token[arm]=token_;r.observed_slot[arm]=slot;r.k=uint32_t(k);
   r.f32_hash[arm]=hash(f32,k*4);r.q8_hash[arm]=hash(q8,bytes);
   q8_check(q8,k,r.zero_blocks[arm],r.bad_bsums[arm],r.nonfinite_scales[arm]);
   if(arm==0){std::memcpy(r.cpu_f32.data(),f32,k*4);std::memcpy(r.cpu_q8.data(),q8,bytes);
    const size_t index=size_t(&r-rows_.data());for(auto&h:held_)if(h.cross_index==index)h.observed_slot[0]=slot;}
   else if(r.input_seen[0]&&r.token[0]==token_){
    if(raw_){std::memcpy(r.hybrid_f32.data(),f32,k*4);std::memcpy(r.hybrid_q8.data(),q8,bytes);}
    compare(r.cpu_f32.data(),f32,k,abs_ppm_,rel_ppm_,r.f32_equal,r.f32_violations,r.f32_max_abs);
    r.q8_equal=std::memcmp(r.cpu_q8.data(),q8,bytes)==0;
    for(size_t i=0;i<bytes;++i)if(r.cpu_q8[i]!=q8[i])++r.q8_byte_differences;
    for(size_t b=0;b<k/256;++b){const auto*old=r.cpu_q8.data()+b*292;const auto*now=q8+b*292;
     if(std::memcmp(old,now,4))++r.scale_diff_blocks;
     for(size_t i=4;i<260;++i)if(old[i]!=now[i])++r.qs_diff_bytes;
     for(size_t i=260;i<292;++i)if(old[i]!=now[i])++r.bsums_diff_bytes;
    }
   }
   if(arm==1){
    last_hybrid_=size_t(&r-rows_.data());
    for(auto&h:held_)if(h.cross_index==last_hybrid_){
     h.hybrid_input=true;h.observed_slot[0]=r.observed_slot[0];h.observed_slot[1]=slot;
     if(h.selected.token!=token_||!r.input_seen[0]||r.token[0]!=token_){h.gap="TOKEN_OR_CPU_INPUT_UNPAIRED";continue;}
     std::memcpy(h.hybrid_q8.data(),q8,bytes);
    }
   }
   charge(start,2*k+bytes);return;
  }
 }
 void output(uint32_t role,uint32_t expert,uint32_t slot,uint32_t input_row,const float*values,size_t n)noexcept override{
  const auto start=std::chrono::steady_clock::now();if(unavailable_)return;
  if(!phase_||!values||n<1||n>2048){fail("OUTPUT_SHAPE");return;}
  for(auto&r:rows_)if(r.key.position==position_&&r.key.role==role&&r.key.expert==expert&&(r.key.slot==any_slot||r.key.slot==slot)&&r.key.input_row==input_row){
   const auto arm=phase_-1,call=r.output_calls[arm]++;if(call!=r.key.occurrence)continue;
   if(!r.input_seen[arm]||r.output_seen[arm]||r.observed_slot[arm]!=slot||(r.n&&r.n!=n)){fail("OUTPUT_ORDER_SHAPE");return;}
   for(size_t i=0;i<n;++i)if(!std::isfinite(values[i])){fail("NONFINITE_OUTPUT");return;}
   r.output_seen[arm]=true;r.n=uint32_t(n);r.output_hash[arm]=hash(values,n*4);
   if(arm==0)std::memcpy(r.cpu_output.data(),values,n*4);
   else if(r.output_seen[0]&&r.token[0]==token_){
    if(raw_)std::memcpy(r.hybrid_output.data(),values,n*4);
    compare(r.cpu_output.data(),values,n,abs_ppm_,rel_ppm_,r.output_equal,r.output_violations,r.output_max_abs);
   }
   charge(start,n);return;
  }
 }
 void unavailable(const char*reason)noexcept override{fail(reason?reason:"DIAGNOSTIC_UNAVAILABLE");}
 bool held_reference(uint32_t role,uint32_t expert,const unsigned char*q8,size_t bytes,
                     unsigned char*copy,size_t capacity,ua50::HeldWeightQuery&query)noexcept override{
  if(phase_!=2||last_hybrid_==SIZE_MAX||!q8||!copy)return false;
  for(size_t i=0;i<held_.size();++i){auto&h=held_[i];if(h.cross_index!=last_hybrid_)continue;
   const auto&r=rows_[h.cross_index];const auto&key=h.selected.key;
   if(key.role!=role||key.expert!=expert||key.position!=position_||h.selected.token!=token_||
      !r.input_seen[0]||!r.input_seen[1]||!r.output_seen[0]||r.token[0]!=token_||
      r.observed_slot[0]!=r.observed_slot[1]&&key.slot!=any_slot){if(!h.gap)h.gap="SEMANTIC_OR_CPU_OUTPUT_UNPAIRED";return false;}
   const size_t required=size_t(r.k)/256*292;
   if(r.k<1||required!=bytes||required>capacity||required>max_q8_bytes||
      std::memcmp(q8,h.hybrid_q8.data(),bytes)!=0){if(!h.gap)h.gap="HYBRID_Q8_IDENTITY_UNPAIRED";return false;}
   if(h.computed){if(!h.gap)h.gap="DUPLICATE_HELD_QUERY";return false;}
   std::memcpy(copy,r.cpu_q8.data(),bytes);h.cpu_input=true;
   query={2,position_,h.selected.layer,role,expert,r.observed_slot[1],key.input_row,key.occurrence,
          token_,i};
   last_hybrid_=SIZE_MAX;return true;
  }
  return false;
 }
 void held_result(const ua50::HeldWeightQuery&q,const float*x,const float*h,const float*g,size_t n)noexcept override{
  if(q.row_index>=held_.size())return;auto&row=held_[q.row_index];
  if(row.gap)return;
  if(!x||!h||!g||n==0||n>2048||row.computed){row.gap="HELD_RESULT_UNAVAILABLE";return;}
  const auto&base=rows_[row.cross_index];if(q.phase!=2||q.position!=row.selected.key.position||
    q.layer!=row.selected.layer||q.role!=row.selected.key.role||q.expert!=row.selected.key.expert||
    q.token!=row.selected.token||q.slot!=row.observed_slot[1]||n!=base.n){row.gap="HELD_RESULT_IDENTITY_UNPAIRED";return;}
  for(size_t i=0;i<n;++i)if(!std::isfinite(x[i])||!std::isfinite(h[i])||!std::isfinite(g[i])){
   row.gap="NONFINITE_HELD_RESULT";return;}
  row.n=uint32_t(n);row.hash[0]=hash(base.cpu_output.data(),n*4);row.hash[1]=hash(x,n*4);
  row.hash[2]=hash(h,n*4);row.hash[3]=hash(g,n*4);
  if(held_raw_){std::memcpy(row.raw_x.data(),x,n*4);std::memcpy(row.raw_h.data(),h,n*4);}
  compare_held(base.cpu_output.data(),x,n,abs_ppm_,rel_ppm_,row.bit_differences[0],row.violations[0],row.max_abs[0]);
  compare_held(x,h,n,abs_ppm_,rel_ppm_,row.bit_differences[1],row.violations[1],row.max_abs[1]);
  compare_held(h,g,n,abs_ppm_,rel_ppm_,row.bit_differences[2],row.violations[2],row.max_abs[2]);
  compare_held(base.cpu_output.data(),g,n,abs_ppm_,rel_ppm_,row.bit_differences[3],row.violations[3],row.max_abs[3]);
  row.computed=true;
 }
 void held_unavailable(const ua50::HeldWeightQuery&q,const char*why)noexcept override{
  if(q.row_index<held_.size()&&!held_[q.row_index].gap)held_[q.row_index].gap=why?why:"HELD_UNAVAILABLE";
 }
 void held_vector_done()noexcept override{last_hybrid_=SIZE_MAX;}
 void held_charge(const ua50::HeldWeightQuery&q,uint64_t work,uint64_t ns)noexcept override{
  if(q.row_index>=held_.size())return;
  if(work>UINT64_MAX-held_work_||ns>UINT64_MAX-held_ns_){if(!held_[q.row_index].gap)held_[q.row_index].gap="HELD_ACCOUNTING";return;}
  held_work_+=work;held_ns_+=ns;
  if(held_work_>held_work_cap_||held_ns_>held_ns_cap_)held_[q.row_index].gap="HELD_BUDGET";
 }
 uint64_t held_work()const noexcept{return held_work_;}
 uint64_t held_ns()const noexcept{return held_ns_;}
 const std::vector<HeldRow>&held_rows()const noexcept{return held_;}
 std::string held_summary(uint64_t cap)const{
  if(cap<4096||cap>(2u<<20))throw std::invalid_argument("held-weight output cap");
  std::string s="{\"schema\":\"ua68-held-weight-cross-input-v1\",\"selected\":"+
  std::to_string(held_.size())+",\"diagnostic_work_ns\":"+std::to_string(held_ns_)+
   ",\"diagnostic_work_units\":"+std::to_string(held_work_)+
   ",\"raw_private\":"+(held_raw_?"true":"false")+",\"rows\":[";
  auto real=[](double d){char b[64];auto x=std::to_chars(b,b+sizeof(b),d,std::chars_format::general,17);
   if(x.ec!=std::errc{})throw std::runtime_error("held-weight numeric serialization");return std::string(b,x.ptr);};
  for(size_t i=0;i<held_.size();++i){const auto&h=held_[i];const auto&k=h.selected.key;
   if(i)s+=',';const char*gap=k.position>prompt_count_?"POSITION_OUTSIDE_WORKLOAD":
    h.gap?h.gap:!h.hybrid_input?"HYBRID_INPUT_NOT_OBSERVED":
    !h.cpu_input?"HELD_QUERY_NOT_OBSERVED":!h.computed?"HELD_RESULT_NOT_OBSERVED":"";
   s+="{\"phase\":2,\"position\":"+std::to_string(k.position)+
    ",\"layer\":"+std::to_string(h.selected.layer)+",\"token\":"+std::to_string(h.selected.token)+
    ",\"role\":"+std::to_string(k.role)+",\"expert\":"+std::to_string(k.expert)+
    ",\"slot\":"+(k.slot==any_slot?"null":std::to_string(k.slot))+
    ",\"CPU_slot\":"+(rows_[h.cross_index].input_seen[0]?std::to_string(h.observed_slot[0]):"null")+
    ",\"hybrid_slot\":"+(h.hybrid_input?std::to_string(h.observed_slot[1]):"null")+
    ",\"input_row\":"+std::to_string(k.input_row)+",\"occurrence\":"+std::to_string(k.occurrence)+
    ",\"CPU_input_seen\":"+(rows_[h.cross_index].input_seen[0]?"true":"false")+
    ",\"hybrid_input_seen\":"+(h.hybrid_input?"true":"false")+
    ",\"computed\":"+(h.computed?"true":"false")+",\"gap\":\""+gap+"\",\"n\":"+
    std::to_string(h.computed?h.n:0)+",\"hashes\":[";
   for(size_t j=0;j<4;++j){if(j)s+=',';s+=std::to_string(h.computed?h.hash[j]:0);}s+="],\"bit_differences\":[";
   for(size_t j=0;j<4;++j){if(j)s+=',';s+=std::to_string(h.computed?h.bit_differences[j]:0);}s+="],\"violations\":[";
   for(size_t j=0;j<4;++j){if(j)s+=',';s+=std::to_string(h.computed?h.violations[j]:0);}s+="],\"max_abs\":[";
   for(size_t j=0;j<4;++j){if(j)s+=',';s+=real(h.computed?h.max_abs[j]:0);}s+="]";
   if(held_raw_&&h.computed){
    static constexpr char digits[]="0123456789abcdef";
    auto hex=[&](const float*data){const auto*bytes=reinterpret_cast<const unsigned char*>(data);
     const size_t count=size_t(h.n)*4;if(count>(cap-s.size())/2)throw std::length_error("held raw cap");
     for(size_t j=0;j<count;++j){s+=digits[bytes[j]>>4];s+=digits[bytes[j]&15];}};
    s+=",\"X_hex\":\"";hex(h.raw_x.data());s+='"';
    s+=",\"H_hex\":\"";hex(h.raw_h.data());s+='"';
   }
   s+='}';
   if(s.size()>cap)throw std::length_error("held-weight output cap");
  }
  s+="]}";if(s.size()>cap)throw std::length_error("held-weight output cap");return s;
 }
 bool available()const noexcept{return !unavailable_;}
 const char*reason()const noexcept{return reason_;}
 uint64_t work_ns()const noexcept{return work_ns_;}
 uint64_t work_units()const noexcept{return work_units_;}
 const std::vector<Record>&records()const noexcept{return rows_;}
 std::string summary(uint64_t max_output_bytes)const{
  if(max_output_bytes<4096||max_output_bytes>(2ull<<20))throw std::invalid_argument("cross-arm output cap");
  std::string s="{\"schema\":\"ua68-model2-cross-arm-v1\",\"unavailable\":"+
   std::string(unavailable_?"true":"false")+",\"reason\":"+
   (reason_?std::string("\"")+reason_+"\"":"null")+
   ",\"selected\":"+std::to_string(rows_.size())+",\"diagnostic_work_ns\":"+std::to_string(work_ns_)+
   ",\"diagnostic_work_units\":"+std::to_string(work_units_)+",\"raw_private\":"+
   (raw_?"true":"false")+",\"rows\":[";
  s.reserve(size_t(max_output_bytes));
  auto hex=[&](const void*data,size_t n){static constexpr char digits[]="0123456789abcdef";
   const auto*b=static_cast<const unsigned char*>(data);
   if(n>(max_output_bytes-s.size())/2)throw std::length_error("cross-arm raw cap");
   for(size_t k=0;k<n;++k){s+=digits[b[k]>>4];s+=digits[b[k]&15];}
  };
  auto real=[&](double d){char b[64];auto x=std::to_chars(b,b+sizeof(b),d,std::chars_format::general,17);
   if(x.ec!=std::errc{})throw std::runtime_error("cross-arm numeric serialization");
   s.append(b,x.ptr);};
  for(size_t i=0;i<rows_.size();++i){const auto&r=rows_[i];if(i)s+=',';
   const bool input_pair=r.input_seen[0]&&r.input_seen[1]&&r.token[0]==r.token[1];
   const bool output_pair=input_pair&&r.output_seen[0]&&r.output_seen[1];
   const char*gap=r.key.position>prompt_count_?"POSITION_OUTSIDE_WORKLOAD":
    !r.input_seen[0]?"CPU_INPUT_NOT_OBSERVED":!r.input_seen[1]?"HYBRID_INPUT_NOT_OBSERVED":
    !input_pair?"TOKEN_UNPAIRED":!r.output_seen[0]?"CPU_OUTPUT_NOT_OBSERVED":
    !r.output_seen[1]?"HYBRID_OUTPUT_NOT_OBSERVED":"";
   s+="{\"position\":"+std::to_string(r.key.position)+",\"role\":"+std::to_string(r.key.role)+
     ",\"expert\":"+std::to_string(r.key.expert)+",\"slot\":"+(r.key.slot==any_slot?"null":std::to_string(r.key.slot))+
     ",\"CPU_slot\":"+(r.input_seen[0]?std::to_string(r.observed_slot[0]):"null")+
     ",\"hybrid_slot\":"+(r.input_seen[1]?std::to_string(r.observed_slot[1]):"null")+
     ",\"input_row\":"+std::to_string(r.key.input_row)+",\"occurrence\":"+std::to_string(r.key.occurrence)+
     ",\"CPU_token\":"+std::to_string(r.token[0])+",\"hybrid_token\":"+std::to_string(r.token[1])+
     ",\"CPU_input_seen\":"+(r.input_seen[0]?"true":"false")+",\"hybrid_input_seen\":"+(r.input_seen[1]?"true":"false")+
     ",\"CPU_output_seen\":"+(r.output_seen[0]?"true":"false")+",\"hybrid_output_seen\":"+(r.output_seen[1]?"true":"false")+
     ",\"gap\":\""+gap+"\",\"F32_equal_bits\":"+(input_pair?(r.f32_equal?"true":"false"):"null")+
     ",\"output_equal_bits\":"+(output_pair?(r.output_equal?"true":"false"):"null")+
     ",\"F32_compared\":"+std::to_string(input_pair?r.k:0)+",\"output_compared\":"+std::to_string(output_pair?r.n:0)+
     ",\"k\":"+std::to_string(r.k)+",\"n\":"+std::to_string(r.n)+
     ",\"CPU_F32_fnv1a64\":"+std::to_string(r.f32_hash[0])+",\"hybrid_F32_fnv1a64\":"+std::to_string(r.f32_hash[1])+
     ",\"CPU_Q8_fnv1a64\":"+std::to_string(r.q8_hash[0])+",\"hybrid_Q8_fnv1a64\":"+std::to_string(r.q8_hash[1])+
     ",\"CPU_output_fnv1a64\":"+std::to_string(r.output_hash[0])+",\"hybrid_output_fnv1a64\":"+std::to_string(r.output_hash[1])+
     ",\"F32_violations\":"+std::to_string(r.f32_violations)+",\"output_violations\":"+std::to_string(r.output_violations)+
     ",\"Q8_byte_differences\":"+std::to_string(r.q8_byte_differences)+
     ",\"Q8_scale_diff_blocks\":"+std::to_string(r.scale_diff_blocks)+",\"Q8_qs_diff_bytes\":"+std::to_string(r.qs_diff_bytes)+
     ",\"Q8_bsums_diff_bytes\":"+std::to_string(r.bsums_diff_bytes)+
     ",\"CPU_Q8_zero_blocks\":"+std::to_string(r.zero_blocks[0])+",\"hybrid_Q8_zero_blocks\":"+std::to_string(r.zero_blocks[1])+
     ",\"CPU_Q8_bsums_anomalies\":"+std::to_string(r.bad_bsums[0])+",\"hybrid_Q8_bsums_anomalies\":"+std::to_string(r.bad_bsums[1])+
     ",\"CPU_Q8_nonfinite_scales\":"+std::to_string(r.nonfinite_scales[0])+",\"hybrid_Q8_nonfinite_scales\":"+std::to_string(r.nonfinite_scales[1])+
     ",\"F32_max_abs\":";real(r.f32_max_abs);s+=",\"output_max_abs\":";real(r.output_max_abs);
   if(raw_&&r.input_seen[0]&&r.input_seen[1]&&r.output_seen[0]&&r.output_seen[1]&&r.token[0]==r.token[1]){
    const size_t qbytes=size_t(r.k)/256*292;
    s+=",\"CPU_F32_hex\":\"";hex(r.cpu_f32.data(),size_t(r.k)*4);s+='"';
    s+=",\"hybrid_F32_hex\":\"";hex(r.hybrid_f32.data(),size_t(r.k)*4);s+='"';
    s+=",\"CPU_Q8_hex\":\"";hex(r.cpu_q8.data(),qbytes);s+='"';
    s+=",\"hybrid_Q8_hex\":\"";hex(r.hybrid_q8.data(),qbytes);s+='"';
    s+=",\"CPU_output_hex\":\"";hex(r.cpu_output.data(),size_t(r.n)*4);s+='"';
    s+=",\"hybrid_output_hex\":\"";hex(r.hybrid_output.data(),size_t(r.n)*4);s+='"';
   }
   s+='}';if(s.size()>max_output_bytes)throw std::length_error("cross-arm output cap");
  }
  s+="]}";if(s.size()>max_output_bytes)throw std::length_error("cross-arm output cap");return s;
 }
};
}
