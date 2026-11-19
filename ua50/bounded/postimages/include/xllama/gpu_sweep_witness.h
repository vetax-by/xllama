#pragma once
#include "gpu_diagnostic_plan.h"
#include "gpu_layer_witness.h"
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace xllama::gpu_proof {
// Optional model2 overview. No raw tensor values leave this owner.
class SweepWitness final {
public:
 using Shape=LayerWitness::Shape;
 struct Row {
  uint32_t position=0,layer=0,first_difference=UINT32_MAX,first_violation=UINT32_MAX,max_abs_index=UINT32_MAX,violations=0,finite_pairs=0;
  uint64_t cpu_hash=0,hybrid_hash=0;double max_abs=0;bool cpu=false,hybrid=false;
 };
private:
 gpu_diagnostic::Plan plan_;
 std::array<int32_t,gpu_diagnostic::Plan::max_positions*gpu_diagnostic::Plan::max_layers> index_{};
 std::vector<Row> rows_;std::vector<float> reference_;std::array<float,2048> scratch_{};
 std::array<uint32_t,6> histogram_{};
 uint32_t expected_frames_=0,expected_pairs_=0,outside_positions_=0,phase_=0,position_=0,seen_=0,frames_[2]{},first_stop_position_=UINT32_MAX;
 uint64_t compared_=0,work_ns_=0,work_units_=0;bool configured_=false,in_decode_=false,failed_=false,truncated_=false;
 const char*reason_=nullptr;
 static uint64_t fnv(const void*data,size_t size)noexcept{const auto*p=static_cast<const unsigned char*>(data);uint64_t h=14695981039346656037ull;for(size_t i=0;i<size;++i){h^=p[i];h*=1099511628211ull;}return h;}
 static uint32_t bits(float x)noexcept{uint32_t b=0;std::memcpy(&b,&x,4);return b;}
 static int layer(std::string_view s)noexcept{
  if(s.size()<7||s.substr(0,6)!="l_out-")return -1;
  if(s.size()>7&&s[6]=='0')return -1;int n=0;
  for(size_t i=6;i<s.size();++i){if(s[i]<'0'||s[i]>'9')return -1;n=n*10+s[i]-'0';if(n>=40)return -1;}return n;
 }
 static void real(std::string&out,double v){char b[64];auto x=std::to_chars(b,b+sizeof(b),v,std::chars_format::general,17);if(x.ec!=std::errc{})throw std::runtime_error("sweep numeric serialization");out.append(b,x.ptr);}
 void fail(const char*reason)noexcept{failed_=true;if(!reason_)reason_=reason;}
 bool timely()noexcept{if(work_ns_>uint64_t(plan_.time_budget_ms)*1000000ull){fail("TIME_BUDGET");return false;}return true;}
public:
 explicit SweepWitness(gpu_diagnostic::Plan plan):plan_(std::move(plan)){index_.fill(-1);}
 void configure(uint32_t prompt_count)noexcept{
  if(configured_){fail("CONFIGURE_TWICE");return;}configured_=true;
  if(prompt_count<1||prompt_count>128){fail("PROMPT_ENVELOPE");return;}
  expected_frames_=prompt_count+1;expected_pairs_=plan_.positions.count(expected_frames_)*plan_.layers.count(40);
  if(plan_.positions.mode==gpu_diagnostic::Axis::Mode::range||plan_.positions.mode==gpu_diagnostic::Axis::Mode::set){
   outside_positions_=plan_.positions.count(gpu_diagnostic::Plan::max_positions)-plan_.positions.count(expected_frames_);
   if(outside_positions_){fail("POSITION_OUTSIDE_WORKLOAD");return;}
  }
  if(!expected_pairs_||expected_pairs_>plan_.max_records){fail("ACTUAL_RECORD_BUDGET");return;}
  const uint64_t actual_bytes=plan_.exact?uint64_t(expected_pairs_)*2048u*4u:0;
  if(actual_bytes+4u*1024u*1024u+2u*2048u*4u>plan_.memory_budget||uint64_t(expected_pairs_)*(4096u+(plan_.exact?plan_.element_count:0u))>plan_.reference_work_budget){fail("ACTUAL_WORK_MEMORY_BUDGET");return;}
  try{
   rows_.reserve(expected_pairs_);
   for(uint32_t p=0;p<expected_frames_;++p)if(plan_.positions.contains(p,expected_frames_))
    for(uint32_t l=0;l<40;++l)if(plan_.layers.contains(l,40)){
     index_[p*40+l]=int32_t(rows_.size());Row row;row.position=p;row.layer=l;rows_.push_back(row);
    }
   if(plan_.exact)reference_.resize(rows_.size()*2048);
  }catch(...){fail("ALLOCATION_UNAVAILABLE");}
 }
 void begin(uint32_t phase,uint32_t position)noexcept{
  if(!configured_||in_decode_||phase<1||phase>2||position>=expected_frames_||position!=frames_[phase-1]||(phase==2&&frames_[0]!=expected_frames_)){fail("FRAME_ORDER");return;}
  phase_=phase;position_=position;seen_=0;in_decode_=true;
 }
 bool ask(std::string_view name)noexcept{
  if(!in_decode_||failed_||truncated_||!timely())return false;
  if((phase_==1&&plan_.arms==gpu_diagnostic::Plan::Arms::hybrid_only)||(phase_==2&&plan_.arms==gpu_diagnostic::Plan::Arms::cpu_only))return false;
  const int l=layer(name);return l>=0&&index_[position_*40+uint32_t(l)]>=0;
 }
 void callback_failure()noexcept{fail("CALLBACK_FAILURE");}
 template<class Reader> void observe(std::string_view name,const Shape&shape,Reader&&read)noexcept{
  const auto work_start=std::chrono::steady_clock::now();
  try{
   if(!in_decode_||failed_||truncated_||!timely())return;
   const int l=layer(name);if(l<0)throw std::logic_error("NAME");const int32_t ix=index_[position_*40+uint32_t(l)];if(ix<0)throw std::logic_error("UNSELECTED");
   uint32_t ordinal=0;for(uint32_t prior=0;prior<uint32_t(l);++prior)if(index_[position_*40+prior]>=0)++ordinal;
   if(ordinal!=seen_)throw std::logic_error("LAYER_ORDER");
   auto&r=rows_[size_t(ix)];if((phase_==1&&r.cpu)||(phase_==2&&r.hybrid)||(!r.cpu&&phase_==2&&plan_.arms==gpu_diagnostic::Plan::Arms::paired))throw std::logic_error("PAIR_ORDER");
   if(!shape.f32||!shape.contiguous||!shape.buffer||!shape.data||shape.bytes!=8192||shape.ne[0]!=2048||shape.ne[1]!=1||shape.ne[2]!=1||shape.ne[3]!=1||shape.nb[0]!=4||shape.nb[1]!=8192||shape.nb[2]!=8192||shape.nb[3]!=8192)throw std::invalid_argument("SHAPE_STRIDE");
   if(!read(scratch_.data(),8192))throw std::runtime_error("READ");
   for(float v:scratch_)if(!std::isfinite(v))throw std::runtime_error("NONFINITE");
   const auto h=fnv(scratch_.data(),8192);
   if(phase_==1){r.cpu=true;r.cpu_hash=h;if(plan_.exact)std::memcpy(reference_.data()+size_t(ix)*2048,scratch_.data(),8192);}
   else{
    r.hybrid=true;r.hybrid_hash=h;
    if(plan_.exact){const auto*cpu=reference_.data()+size_t(ix)*2048;
     for(uint32_t s=0;s<plan_.element_count;++s){const uint32_t e=plan_.element_start+s*plan_.element_stride;
      if(r.first_difference==UINT32_MAX&&bits(cpu[e])!=bits(scratch_[e]))r.first_difference=e;
      const double delta=std::abs(double(cpu[e])-double(scratch_[e]));
      const double tolerance=(double(plan_.analysis_abs_ppm)+double(plan_.analysis_rel_ppm)*std::abs(double(cpu[e])))/1000000.0;
      if(delta>r.max_abs){r.max_abs=delta;r.max_abs_index=e;}
      const double ratio=tolerance>0?delta/tolerance:delta>0?1e300:0;
      const uint32_t bin=ratio==0?0:ratio<=.25?1:ratio<=.5?2:ratio<=1?3:ratio<=2?4:5;++histogram_[bin];
      if(delta>tolerance){if(r.first_violation==UINT32_MAX)r.first_violation=e;++r.violations;}
      ++r.finite_pairs;++compared_;
     }
     if(plan_.stop_on_first&&r.violations){truncated_=true;first_stop_position_=position_;}
    }
   }
   work_units_+=2048u+(phase_==2&&plan_.exact?plan_.element_count:0u);if(work_units_>plan_.reference_work_budget)fail("WORK_BUDGET");
   ++seen_;
  }catch(const std::exception&e){
   if(std::strcmp(e.what(),"SHAPE_STRIDE")==0)fail("SHAPE_STRIDE");else fail("OBSERVATION_FAILURE");
  }catch(...){fail("OBSERVATION_FAILURE");}
  const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-work_start).count();
  if(elapsed<0||uint64_t(elapsed)>UINT64_MAX-work_ns_)fail("TIME_ACCOUNTING");else{work_ns_+=uint64_t(elapsed);(void)timely();}
 }
 void finish(int decode_status)noexcept{
  if(!in_decode_){fail("FINISH_LIFETIME");return;}in_decode_=false;++frames_[phase_-1];
  if(decode_status)fail("DECODE_FAILED");
  if(!failed_&&!truncated_){uint32_t expected=0;
   if(!((phase_==1&&plan_.arms==gpu_diagnostic::Plan::Arms::hybrid_only)||(phase_==2&&plan_.arms==gpu_diagnostic::Plan::Arms::cpu_only)))
    for(uint32_t l=0;l<40;++l)if(index_[position_*40+l]>=0)++expected;
   if(seen_!=expected)fail("MISSED_OBSERVATION");}
 }
 bool complete()const noexcept{if(!configured_||failed_||truncated_||in_decode_||frames_[0]!=expected_frames_||frames_[1]!=expected_frames_)return false;for(const auto&r:rows_)if((plan_.arms!=gpu_diagnostic::Plan::Arms::hybrid_only&&!r.cpu)||(plan_.arms!=gpu_diagnostic::Plan::Arms::cpu_only&&!r.hybrid))return false;return true;}
 std::string summary()const{
  if(!configured_)throw std::logic_error("sweep unconfigured");
  const char*arms=plan_.arms==gpu_diagnostic::Plan::Arms::paired?"paired":plan_.arms==gpu_diagnostic::Plan::Arms::cpu_only?"cpu_only":"hybrid_only";
  std::string s="{\"schema\":\"ua68-model2-sweep-v1\",\"arms\":\""+std::string(arms)+"\",\"complete\":"+std::string(complete()?"true":"false")+",\"unavailable\":"+(failed_?"true":"false")+",\"truncated\":"+(truncated_?"true":"false")+",\"reason\":";
  s+=reason_?std::string("\"")+reason_+"\"":truncated_?"\"DIAGNOSTIC_FIRST_VIOLATION\"":"null";
  s+=",\"expected_frames_per_phase\":"+std::to_string(expected_frames_)+",\"CPU_frames\":"+std::to_string(frames_[0])+",\"hybrid_frames\":"+std::to_string(frames_[1]);
  s+=",\"expected_pairs\":"+std::to_string(expected_pairs_)+",\"requested_outside_workload\":"+std::to_string(outside_positions_)+",\"compared_elements\":"+std::to_string(compared_)+",\"diagnostic_work_ns\":"+std::to_string(work_ns_)+",\"diagnostic_work_units\":"+std::to_string(work_units_)+",\"first_stop_position\":"+(first_stop_position_==UINT32_MAX?"null":std::to_string(first_stop_position_));
  s+=",\"histogram\":[";for(size_t i=0;i<histogram_.size();++i){if(i)s+=",";s+=std::to_string(histogram_[i]);}s+="],\"rows\":[";
  for(size_t i=0;i<rows_.size();++i){const auto&r=rows_[i];if(i)s+=",";
   s+="{\"position\":"+std::to_string(r.position)+",\"layer\":"+std::to_string(r.layer)+",\"CPU_seen\":"+(r.cpu?"true":"false")+",\"hybrid_seen\":"+(r.hybrid?"true":"false")+",\"CPU_fnv1a64\":"+std::to_string(r.cpu_hash)+",\"hybrid_fnv1a64\":"+std::to_string(r.hybrid_hash)+",\"finite_pairs\":"+std::to_string(r.finite_pairs)+",\"first_difference\":"+(r.first_difference==UINT32_MAX?"null":std::to_string(r.first_difference)) +",\"first_violation\":"+(r.first_violation==UINT32_MAX?"null":std::to_string(r.first_violation)) +",\"violations\":"+std::to_string(r.violations)+",\"max_abs\":";
   real(s,r.max_abs);s+=",\"max_abs_index\":"+(r.max_abs_index==UINT32_MAX?"null":std::to_string(r.max_abs_index))+"}";
  }
  s+="],\"top_k\":[";
  std::vector<size_t> ranked;for(size_t i=0;i<rows_.size();++i)if(rows_[i].max_abs_index!=UINT32_MAX)ranked.push_back(i);
  std::sort(ranked.begin(),ranked.end(),[this](size_t a,size_t b){const auto&x=rows_[a],&y=rows_[b];if(x.max_abs!=y.max_abs)return x.max_abs>y.max_abs;return a<b;});
  for(size_t i=0;i<ranked.size()&&i<plan_.top_k;++i){const auto&r=rows_[ranked[i]];if(i)s+=",";s+="{\"position\":"+std::to_string(r.position)+",\"layer\":"+std::to_string(r.layer)+",\"element\":"+std::to_string(r.max_abs_index)+",\"max_abs\":";real(s,r.max_abs);s+="}";}
  s+="]}";if(s.size()>plan_.output_budget)throw std::length_error("sweep output budget");return s;
 }
};
}
