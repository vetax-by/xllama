#pragma once
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace xllama::gpu_proof {
// Model2-only diagnostic. All raw layer values stay in this bounded owner.
class LayerWitness final {
public:
 static constexpr uint32_t positions=3,layers=40,width=2048;
 static constexpr size_t row_bytes=width*sizeof(float),reference_bytes=positions*layers*row_bytes;
 static constexpr size_t summary_cap=65536,charged_bytes=(1u<<20)+summary_cap;
 struct Shape {bool f32=false,i32=false,contiguous=false,buffer=false,data=false;uint32_t type=0,op=0;uint64_t ne[4]{},nb[4]{};size_t bytes=0;};
 struct Row {uint64_t cpu_hash=0,hybrid_hash=0;uint32_t bit_first=UINT32_MAX,tolerance_first=UINT32_MAX,max_abs_index=UINT32_MAX,violations=0;double max_abs=0;bool cpu=false,hybrid=false;};
private:
 std::array<float,positions*layers*width> reference_{};
 std::array<float,width> scratch_{};
 std::array<Row,positions*layers> rows_{};
 uint32_t expected_frames_=0,next_[2]{},phase_=0,position_=0,seen_=0;
 bool configured_=false,in_decode_=false,active_=false;
 const char*error_=nullptr;
 static uint64_t fnv(const void*data,size_t bytes)noexcept{
  auto*p=static_cast<const unsigned char*>(data);uint64_t h=14695981039346656037ull;
  for(size_t i=0;i<bytes;++i){h^=p[i];h*=1099511628211ull;}return h;
 }
 static uint32_t bits(float v)noexcept{uint32_t x;std::memcpy(&x,&v,4);return x;}
 void fail(const char*s)noexcept{if(!error_)error_=s;}
 static int layer(std::string_view name)noexcept{
  if(name.size()<7||name.substr(0,6)!="l_out-")return -1;
  if(name.size()>7&&name[6]=='0')return -1;
  int n=0;for(size_t i=6;i<name.size();++i){if(name[i]<'0'||name[i]>'9')return -1;n=n*10+name[i]-'0';if(n>=int(layers))return -1;}
  return n;
 }
 static void real(std::string&out,double v){char b[64];auto r=std::to_chars(b,b+sizeof(b),v,std::chars_format::general,17);if(r.ec!=std::errc{})throw std::runtime_error("layer witness numeric serialization");out.append(b,r.ptr);}
public:
 LayerWitness()=default;
 LayerWitness(const LayerWitness&)=delete;LayerWitness&operator=(const LayerWitness&)=delete;
 void configure(uint32_t prompt_count){
  if(configured_||prompt_count<positions||prompt_count>128)throw std::invalid_argument("layer witness prompt envelope");
  expected_frames_=prompt_count+1;configured_=true;
 }
 void begin(uint32_t phase,uint32_t position){
  if(!configured_||error_||in_decode_||(phase!=1&&phase!=2)||position>=expected_frames_||position!=next_[phase-1]||(phase==2&&next_[0]!=expected_frames_))throw std::logic_error("layer witness phase/position order");
  phase_=phase;position_=position;seen_=0;active_=position<positions;in_decode_=true;
 }
 bool ask(std::string_view name)noexcept{
  if(!in_decode_||error_||!active_)return false;
  if(name.substr(0,5)!="l_out")return false;
  if(layer(name)<0){fail("layer witness name");return false;}
  return true;
 }
 void callback_failure()noexcept{fail("layer witness native callback failure");}
 template<class Reader> bool observe(std::string_view name,const Shape&shape,Reader&&read)noexcept{
  try{
   if(!in_decode_||!active_||error_)throw std::logic_error("layer witness callback lifetime");
   const int il=layer(name);if(il<0||uint32_t(il)!=seen_)throw std::logic_error("layer witness layer order/name");
   if(!shape.f32||!shape.contiguous||!shape.buffer||!shape.data||shape.bytes!=row_bytes||shape.ne[0]!=width||shape.ne[1]!=1||shape.ne[2]!=1||shape.ne[3]!=1||shape.nb[0]!=sizeof(float)||shape.nb[1]!=row_bytes||shape.nb[2]!=row_bytes||shape.nb[3]!=row_bytes)throw std::invalid_argument("layer witness type/shape/stride");
   if(!read(scratch_.data(),row_bytes))throw std::runtime_error("layer witness tensor read");
   const size_t index=size_t(position_)*layers+size_t(il);auto&r=rows_[index];
   for(float v:scratch_)if(!std::isfinite(v))throw std::runtime_error("layer witness nonfinite");
   if(phase_==1){
    if(r.cpu)throw std::logic_error("layer witness duplicate CPU");
    std::memcpy(reference_.data()+index*width,scratch_.data(),row_bytes);r.cpu_hash=fnv(scratch_.data(),row_bytes);r.cpu=true;
   }else{
    if(!r.cpu||r.hybrid)throw std::logic_error("layer witness missing CPU/duplicate hybrid");
    r.hybrid_hash=fnv(scratch_.data(),row_bytes);const float*cpu=reference_.data()+index*width;
    for(uint32_t i=0;i<width;++i){
     if(r.bit_first==UINT32_MAX&&bits(cpu[i])!=bits(scratch_[i]))r.bit_first=i;
     const double error=std::abs(double(scratch_[i])-double(cpu[i]));
     const double tolerance=.001+.001*std::abs(double(cpu[i]));
     if(!std::isfinite(error)||!std::isfinite(tolerance))throw std::runtime_error("layer witness finite difference");
     if(error>r.max_abs){r.max_abs=error;r.max_abs_index=i;}
     if(error>tolerance){if(r.tolerance_first==UINT32_MAX)r.tolerance_first=i;++r.violations;}
    }
    r.hybrid=true;
   }
   ++seen_;return true;
  }catch(const std::exception&e){
   // Never throw through ggml's C callback. Messages here are fixed literals.
   if(std::strstr(e.what(),"name"))fail("layer witness name/order");
   else if(std::strstr(e.what(),"shape"))fail("layer witness type/shape/stride");
   else if(std::strstr(e.what(),"nonfinite"))fail("layer witness nonfinite");
   else fail("layer witness callback/read/order");
  }catch(...){fail("layer witness callback unknown");}
  return false;
 }
 void finish_or_throw(int decode_status){
  if(!in_decode_)throw std::logic_error("layer witness finish lifetime");
  if(decode_status||error_||(active_&&seen_!=layers)||(!active_&&seen_))throw std::runtime_error(error_?error_:"layer witness missing layers/decode");
  ++next_[phase_-1];in_decode_=false;active_=false;
 }
 bool complete()const noexcept{return configured_&&!in_decode_&&!error_&&next_[0]==expected_frames_&&next_[1]==expected_frames_;}
 const char*error()const noexcept{return error_;}
 std::string summary()const{
  std::string out;out.reserve(32768);out+="{\"schema\":\"ua68-model2-layer-witness-v1\",\"complete\":";
  out+=complete()?"true":"false";out+=",\"expected_frames_per_phase\":"+std::to_string(expected_frames_)+",\"CPU_frames\":"+std::to_string(next_[0])+",\"hybrid_frames\":"+std::to_string(next_[1]);
  auto first=[this](bool tolerance){for(uint32_t p=0;p<positions;++p)for(uint32_t il=0;il<layers;++il){const auto&r=rows_[size_t(p)*layers+il];const auto index=tolerance?r.tolerance_first:r.bit_first;if(index!=UINT32_MAX)return std::string("{\"position\":")+std::to_string(p)+",\"layer\":"+std::to_string(il)+",\"element\":"+std::to_string(index)+"}";}return std::string("null");};
  out+=",\"first_bit_difference\":"+first(false)+",\"first_tolerance_violation\":"+first(true)+",\"rows\":[";
  bool comma=false;for(uint32_t p=0;p<positions;++p)for(uint32_t il=0;il<layers;++il){const auto&r=rows_[size_t(p)*layers+il];if(!r.cpu&&!r.hybrid)continue;if(comma)out+=",";comma=true;
   out+="{\"position\":"+std::to_string(p)+",\"layer\":"+std::to_string(il)+",\"CPU_seen\":"+(r.cpu?"true":"false")+",\"hybrid_seen\":"+(r.hybrid?"true":"false")+",\"CPU_fnv1a64\":"+std::to_string(r.cpu_hash)+",\"hybrid_fnv1a64\":"+std::to_string(r.hybrid_hash)+",\"first_bit_difference\":";
   out+=r.bit_first==UINT32_MAX?"null":std::to_string(r.bit_first);out+=",\"first_tolerance_violation\":";out+=r.tolerance_first==UINT32_MAX?"null":std::to_string(r.tolerance_first);out+=",\"violations\":"+std::to_string(r.violations)+",\"max_abs\":";real(out,r.max_abs);out+=",\"max_abs_index\":";out+=r.max_abs_index==UINT32_MAX?"null":std::to_string(r.max_abs_index);out+="}";
  }out+="]}";if(out.size()>summary_cap)throw std::length_error("layer witness summary cap");return out;
 }
};
static_assert(sizeof(LayerWitness)<(1u<<20),"model2 witness owner exceeds one MiB");
}
