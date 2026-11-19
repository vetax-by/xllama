#pragma once
#include "gpu_layer_witness.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace xllama::gpu_proof {
// Optional, selected graph boundaries. A construction callback is only a
// candidate: absence, aliasing and unsupported shape remain explicit gaps.
class GraphWitness final {
public:
 // Maximum encoded row, including two shapes and optional router arrays.
 static constexpr uint64_t max_encoded_row_bytes=1600;
 enum class Kind {f32,i32};
 struct Point {const char *key;const char *final_name;Kind kind;uint32_t max_elements;bool layer;};
 static constexpr Point catalog[]={
  {"embedding","model.embed_tokens",Kind::f32,2048,false},
  {"operator_norm","model.layers.{}.operator_norm",Kind::f32,2048,true},
  {"operator_conv_out","model.layers.{}.conv.out_proj",Kind::f32,2048,true},
  {"operator_attn_out","model.layers.{}.self_attn.out_proj",Kind::f32,2048,true},
  {"pre_ffn_residual","model.layers.{}.pre_ffn_residual",Kind::f32,2048,true},
  {"ffn_norm","model.layers.{}.ffn_norm",Kind::f32,2048,true},
  {"router_logits","ffn_moe_logits",Kind::f32,64,true},
  {"router_logits_biased","ffn_moe_logits_biased",Kind::f32,64,true},
  {"router_probs","ffn_moe_probs",Kind::f32,64,true},
  {"router_probs_biased","ffn_moe_probs_biased",Kind::f32,64,true},
  {"router_probs_masked","ffn_moe_probs_masked",Kind::f32,64,true},
  {"router_rank","ffn_moe_argsort",Kind::i32,64,true},
  {"router_topk","ffn_moe_topk",Kind::i32,4,true},
  {"router_weights","ffn_moe_weights",Kind::f32,4,true},
  {"router_weights_norm","ffn_moe_weights_norm",Kind::f32,4,true},
  {"router_weights_scaled","ffn_moe_weights_scaled",Kind::f32,4,true},
  {"moe_gate","ffn_moe_gate",Kind::f32,6144,true},
  {"moe_up","ffn_moe_up",Kind::f32,6144,true},
  {"moe_activation","ffn_moe_swiglu",Kind::f32,6144,true},
  {"moe_down","ffn_moe_down",Kind::f32,8192,true},
  {"moe_weighted","ffn_moe_weighted",Kind::f32,8192,true},
  {"moe_aggregate","ffn_moe_out",Kind::f32,2048,true},
  {"dense_ffn_out","model.layers.{}.dense_ffn_out",Kind::f32,2048,true},
  {"post_ffn_residual","model.layers.{}.post_ffn_residual",Kind::f32,2048,true},
  {"result_norm","result_norm",Kind::f32,2048,false},
  {"result_output","result_output",Kind::f32,128000,false},
 };
 struct Selection {uint32_t position=0,layer=0;std::string point;};
 struct Row {
  Selection selected;bool seen[2]{};int32_t token[2]{-1,-1};
  uint32_t elements=0,violations=0,first_difference=UINT32_MAX,first_violation=UINT32_MAX;
  double max_abs=0;uint64_t hash[2]{};std::string gap="NOT_OBSERVED";
  LayerWitness::Shape shape[2]{};
  bool score_stats_seen[2]{};double kth_margin[2]{};uint32_t exact_score_ties[2]{};
  bool topk_seen[2]{},weights_seen[2]{};std::array<int32_t,4> topk[2]{};std::array<float,4> weights[2]{};
  std::vector<unsigned char> reference;
 };
private:
 std::vector<Row> rows_;std::array<std::vector<size_t>,129> by_position_{};
 uint32_t phase_=0,position_=0,selected_upper_=0;int32_t token_=-1;
 bool workload_axis_=false,configured_=false;
 uint64_t memory_budget_,work_budget_,work_units_=0,time_ns_=0,time_cap_ns_,output_budget_;
 uint64_t ask_calls_=0,sync_requests_=0,observe_calls_=0;
 uint32_t abs_ppm_,rel_ppm_;bool unavailable_=false;const char*reason_=nullptr;
 static const Point*point(std::string_view name)noexcept{
  for(const auto&p:catalog)if(name==p.key)return &p;
  return nullptr;
 }
 static std::string final_name(const Point&p,uint32_t layer){
  std::string n=p.final_name;if(p.layer){n+='-';n+=std::to_string(layer);}return n;
 }
 static bool matches(const Point&p,uint32_t layer,std::string_view name)noexcept{
  const std::string_view base(p.final_name);
  if(!p.layer)return name==base;
  if(name.size()<=base.size()+1||name.substr(0,base.size())!=base||name[base.size()]!='-')return false;
  uint32_t parsed=0;
  for(size_t i=base.size()+1;i<name.size();++i){
   if(name[i]<'0'||name[i]>'9')return false;
   parsed=parsed*10+uint32_t(name[i]-'0');if(parsed>=40)return false;
  }
  return parsed==layer;
 }
 // Exact model2 one-token endpoint shapes and pinned ggml op IDs. A changed
 // producer or alias is a visible gap, never a numerically paired row.
 static bool admitted_endpoint(std::string_view key,const LayerWitness::Shape&s)noexcept{
  uint64_t n0=2048,n1=1;uint32_t op=0;
  if(key=="embedding")op=40;
  else if(key=="operator_norm"||key=="ffn_norm"||key=="result_norm")op=7;
  else if(key=="operator_conv_out"||key=="operator_attn_out"||key=="dense_ffn_out"||key=="result_output")op=29;
  else if(key=="pre_ffn_residual"||key=="post_ffn_residual")op=2;
  else if(key=="router_logits"){n0=64;op=29;}
  else if(key=="router_logits_biased"){n0=64;op=2;}
  else if(key=="router_probs"){n0=64;op=46;}
  else if(key=="router_probs_biased"){n0=64;op=2;}
  else if(key=="router_probs_masked"){n0=64;op=36;}
  else if(key=="router_rank"){n0=64;op=69;}
  else if(key=="router_topk"){n0=4;op=37;}
  else if(key=="router_weights"){n0=1;n1=4;op=40;}
  else if(key=="router_weights_norm"){n0=4;op=8;}
  else if(key=="router_weights_scaled"){n0=1;n1=4;op=32;}
  else if(key=="moe_gate"||key=="moe_up"||key=="moe_activation"){n0=1536;n1=4;op=key=="moe_activation"?100:30;}
  else if(key=="moe_down"){n0=2048;n1=4;op=30;}
  else if(key=="moe_weighted"){n0=2048;n1=4;op=7;}
  else if(key=="moe_aggregate")op=2;
  else return false;
  if(key=="result_output")n0=128000;
  return s.op==op&&s.ne[0]==n0&&s.ne[1]==n1&&s.ne[2]==1&&s.ne[3]==1;
 }
 static uint64_t fnv(const void*p,size_t n)noexcept{
  uint64_t h=14695981039346656037ull;const auto*b=static_cast<const unsigned char*>(p);
  for(size_t i=0;i<n;++i){h^=b[i];h*=1099511628211ull;}return h;
 }
 void fail(const char*why)noexcept{unavailable_=true;if(!reason_)reason_=why;}
public:
 GraphWitness(std::vector<Selection> selected,uint32_t abs_ppm,uint32_t rel_ppm,
              uint64_t memory_budget,uint64_t work_budget,uint32_t time_budget_ms,uint64_t output_budget=2ull<<20,bool workload_axis=false)
  :workload_axis_(workload_axis),memory_budget_(memory_budget),work_budget_(work_budget),time_cap_ns_(uint64_t(time_budget_ms)*1000000),output_budget_(output_budget),abs_ppm_(abs_ppm),rel_ppm_(rel_ppm){
  if(selected.empty()||selected.size()>8192||memory_budget>64ull*1024*1024||memory_budget<1024*1024||
     work_budget==0||work_budget>120000000||time_budget_ms<1000||time_budget_ms>120000||
     abs_ppm>1000000||rel_ppm>1000000||output_budget<4096||output_budget>(2ull<<20))throw std::invalid_argument("graph witness budget");
  uint64_t charge=sizeof(*this)+selected.size()*sizeof(Row)+1024*1024;
  for(auto&s:selected){
   const auto*p=point(s.point);if(!p||s.position>128||(p->layer?s.layer>=40:s.layer!=0))throw std::invalid_argument("graph point unsupported");
   for(const auto&r:rows_)if(r.selected.position==s.position&&r.selected.layer==s.layer&&r.selected.point==s.point)throw std::invalid_argument("graph point duplicate");
   charge+=uint64_t(p->max_elements)*8; // retained CPU row plus transient read buffer
   Row r;r.selected=std::move(s);by_position_[r.selected.position].push_back(rows_.size());rows_.push_back(std::move(r));
  }
  if(charge>memory_budget_)throw std::invalid_argument("graph witness memory");
  selected_upper_=uint32_t(rows_.size());
 }
 void configure(uint32_t prompt_count)noexcept{
  if(configured_||prompt_count<1||prompt_count>128){fail("GRAPH_PROMPT_ENVELOPE");return;}
  configured_=true;
  if(workload_axis_){
   try{
    rows_.erase(std::remove_if(rows_.begin(),rows_.end(),[prompt_count](const Row&r){return r.selected.position>prompt_count;}),rows_.end());
    for(auto&entries:by_position_)entries.clear();
    for(size_t i=0;i<rows_.size();++i)by_position_[rows_[i].selected.position].push_back(i);
   }catch(...){fail("GRAPH_ALLOCATION_UNAVAILABLE");}
  }else{
   for(auto&r:rows_)if(r.selected.position>prompt_count){r.gap="POSITION_OUTSIDE_WORKLOAD";fail("POSITION_OUTSIDE_WORKLOAD");}
  }
 }
 void begin(uint32_t phase,uint32_t position,int32_t token)noexcept{
  if(!configured_||(phase!=1&&phase!=2)||position>128||token<0){fail("FRAME_IDENTITY");return;}
  phase_=phase;position_=position;token_=token;
 }
 bool selected(std::string_view name)const noexcept{
  if(unavailable_||!phase_)return false;
  for(size_t index:by_position_[position_]){const auto&r=rows_[index];const auto*p=point(r.selected.point);
   if(p&&matches(*p,r.selected.layer,name))return true;}
  return false;
 }
 bool ask(std::string_view name)noexcept{
  if(unavailable_||!phase_)return false;
  const auto start=std::chrono::steady_clock::now();++ask_calls_;
  const bool chosen=selected(name);
  const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
  if(elapsed<0||uint64_t(elapsed)>UINT64_MAX-time_ns_){fail("GRAPH_TIME_ACCOUNTING");return false;}
  time_ns_+=uint64_t(elapsed);if(time_ns_>time_cap_ns_){fail("GRAPH_TIME_BUDGET");return false;}
  if(chosen)++sync_requests_;
  return chosen;
 }
 template<class Reader>void observe(std::string_view name,const LayerWitness::Shape&shape,Reader&&read)noexcept{
  if(unavailable_||!phase_)return;
  ++observe_calls_;
  const auto start=std::chrono::steady_clock::now();
  try{
   for(size_t index:by_position_[position_]){auto&r=rows_[index];
    const auto*p=point(r.selected.point);if(!p||!matches(*p,r.selected.layer,name))continue;
    const auto arm=phase_-1;
    if(r.seen[arm]){r.gap="DUPLICATE_OBSERVATION";return;}
    if(!shape.contiguous||!shape.buffer||!shape.data||shape.bytes==0||shape.bytes%4||
       shape.bytes>uint64_t(p->max_elements)*4||shape.ne[3]!=1||shape.nb[0]!=4||
       (p->kind==Kind::f32?!shape.f32:!shape.i32)||!admitted_endpoint(r.selected.point,shape)){
     r.gap="SHAPE_OR_TYPE_UNAVAILABLE";return;
    }
    uint64_t elements=1;
    for(uint64_t dim:shape.ne){if(dim==0||dim>p->max_elements/elements){r.gap="SHAPE_OR_TYPE_UNAVAILABLE";return;}elements*=dim;}
    if(elements*4!=shape.bytes){r.gap="SHAPE_OR_TYPE_UNAVAILABLE";return;}
    std::vector<unsigned char> bytes(size_t(shape.bytes),0);
    if(!read(bytes.data(),bytes.size())){r.gap="READ_UNAVAILABLE";return;}
    if(p->kind==Kind::f32){for(size_t i=0;i<bytes.size();i+=4){float value;std::memcpy(&value,bytes.data()+i,4);if(!std::isfinite(value)){r.gap="NONFINITE";return;}}}
    r.seen[arm]=true;r.token[arm]=token_;r.elements=uint32_t(bytes.size()/4);r.hash[arm]=fnv(bytes.data(),bytes.size());
    r.shape[arm]=shape;
    if(r.selected.point=="router_topk"&&r.elements==4){
     std::memcpy(r.topk[arm].data(),bytes.data(),16);
     std::array<bool,64> used{};
     for(int32_t id:r.topk[arm]){if(id<0||id>=64||used[size_t(id)]){r.gap="ROUTER_ID_INVALID";fail("ROUTER_ID_INVALID");return;}used[size_t(id)]=true;}
     r.topk_seen[arm]=true;
    }
    if((r.selected.point=="router_weights"||r.selected.point=="router_weights_norm"||
        r.selected.point=="router_weights_scaled")&&r.elements==4){
     std::memcpy(r.weights[arm].data(),bytes.data(),16);r.weights_seen[arm]=true;
    }
    if((r.selected.point=="router_logits"||r.selected.point=="router_logits_biased"||
        r.selected.point=="router_probs"||r.selected.point=="router_probs_biased"||
        r.selected.point=="router_probs_masked")&&r.elements==64){
     std::array<float,64> scores{};std::memcpy(scores.data(),bytes.data(),bytes.size());
     std::sort(scores.begin(),scores.end(),std::greater<float>{});
     r.kth_margin[arm]=double(scores[3])-double(scores[4]);
     for(size_t j=1;j<scores.size();++j)if(scores[j]==scores[j-1])++r.exact_score_ties[arm];
     r.score_stats_seen[arm]=true;
    }
    if(arm==0){r.reference=std::move(bytes);r.gap="HYBRID_NOT_OBSERVED";}
    else if(!r.seen[0]||r.token[0]!=token_||r.reference.size()!=bytes.size()||
            r.shape[0].type!=shape.type||r.shape[0].op!=shape.op||
            !std::equal(std::begin(r.shape[0].ne),std::end(r.shape[0].ne),std::begin(shape.ne))||
            !std::equal(std::begin(r.shape[0].nb),std::end(r.shape[0].nb),std::begin(shape.nb))){r.gap="SEMANTIC_SHAPE_UNPAIRED";}
    else if(r.selected.point=="moe_gate"||r.selected.point=="moe_up"||r.selected.point=="moe_activation"||
            r.selected.point=="moe_down"||r.selected.point=="moe_weighted"){
     const Row*route=nullptr;
     for(size_t route_ix:by_position_[position_]){const auto&candidate=rows_[route_ix];
      if(candidate.selected.layer==r.selected.layer&&candidate.selected.point=="router_topk"){route=&candidate;break;}}
     if(!route||!route->seen[0]||!route->seen[1])r.gap="ROUTE_IDENTITY_UNOBSERVED";
     else if(!route->gap.empty()||route->violations)r.gap="ROUTE_UNPAIRED";
     else r.gap="ROUTE_MATCHED_COMPARISON_PENDING";
    }
    else{
     for(size_t i=0;i<bytes.size();i+=4){
      uint32_t a,b;std::memcpy(&a,r.reference.data()+i,4);std::memcpy(&b,bytes.data()+i,4);
      if(a!=b&&r.first_difference==UINT32_MAX)r.first_difference=uint32_t(i/4);
      if(p->kind==Kind::i32){if(a!=b){++r.violations;if(r.first_violation==UINT32_MAX)r.first_violation=uint32_t(i/4);}}
      else{float af,bf;std::memcpy(&af,&a,4);std::memcpy(&bf,&b,4);
       const auto delta=std::abs(double(af)-double(bf));r.max_abs=std::max(r.max_abs,delta);
       if(delta>(double(abs_ppm_)+double(rel_ppm_)*std::abs(double(af)))/1000000.0){++r.violations;if(r.first_violation==UINT32_MAX)r.first_violation=uint32_t(i/4);}}
     }
     r.gap="";
    }
    if(r.gap=="ROUTE_MATCHED_COMPARISON_PENDING"){
     for(size_t i=0;i<bytes.size();i+=4){
      float af,bf;std::memcpy(&af,r.reference.data()+i,4);std::memcpy(&bf,bytes.data()+i,4);
      uint32_t a,b;std::memcpy(&a,&af,4);std::memcpy(&b,&bf,4);
      if(a!=b&&r.first_difference==UINT32_MAX)r.first_difference=uint32_t(i/4);
      const auto delta=std::abs(double(af)-double(bf));r.max_abs=std::max(r.max_abs,delta);
      if(delta>(double(abs_ppm_)+double(rel_ppm_)*std::abs(double(af)))/1000000.0){++r.violations;if(r.first_violation==UINT32_MAX)r.first_violation=uint32_t(i/4);}
     }
     r.gap="";
    }
    work_units_+=r.elements;if(work_units_>work_budget_)fail("GRAPH_WORK_BUDGET");
    const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    if(elapsed<0||uint64_t(elapsed)>UINT64_MAX-time_ns_)fail("GRAPH_TIME_ACCOUNTING");
    else{time_ns_+=uint64_t(elapsed);if(time_ns_>time_cap_ns_)fail("GRAPH_TIME_BUDGET");}
    return;
   }
  }catch(...){fail("GRAPH_OBSERVER_FAILURE");}
 }
 void unavailable(const char*reason)noexcept{fail(reason?reason:"GRAPH_UNAVAILABLE");}
 bool available()const noexcept{return !unavailable_;}
 const char*reason()const noexcept{return reason_;}
 uint64_t work_units()const noexcept{return work_units_;}
 uint64_t work_ns()const noexcept{return time_ns_;}
 const std::vector<Row>&rows()const noexcept{return rows_;}
 std::string summary()const{
  auto real_json=[](double value){char b[64];auto n=std::to_chars(b,b+sizeof(b),value,std::chars_format::general,17);
   if(n.ec!=std::errc{})throw std::runtime_error("graph score serialization");
   return std::string(b,n.ptr);};
  auto shape_json=[](const LayerWitness::Shape&shape){
   std::string out="{\"type\":"+std::to_string(shape.type)+",\"op\":"+std::to_string(shape.op)+
    ",\"bytes\":"+std::to_string(shape.bytes)+",\"ne\":[";
   for(size_t axis=0;axis<4;++axis){if(axis)out+=',';out+=std::to_string(shape.ne[axis]);}
   out+="],\"nb\":[";
   for(size_t axis=0;axis<4;++axis){if(axis)out+=',';out+=std::to_string(shape.nb[axis]);}
   out+="]}";return out;
  };
  std::string s="{\"schema\":\"ua68-model2-graph-v1\",\"unavailable\":"+std::string(unavailable_?"true":"false")+
   ",\"reason\":"+(reason_?std::string("\"")+reason_+"\"":"null")+
   ",\"selected_upper\":"+std::to_string(selected_upper_)+",\"selected_actual\":"+std::to_string(rows_.size())+",\"diagnostic_work_ns\":"+std::to_string(time_ns_)+
   ",\"diagnostic_work_units\":"+std::to_string(work_units_)+
   ",\"ask_calls\":"+std::to_string(ask_calls_)+",\"sync_requests\":"+std::to_string(sync_requests_)+
   ",\"observe_calls\":"+std::to_string(observe_calls_)+",\"rows\":[";
  s.reserve(size_t(output_budget_));
  for(size_t i=0;i<rows_.size();++i){const auto&r=rows_[i];if(i)s+=",";
   s+="{\"position\":"+std::to_string(r.selected.position)+",\"layer\":"+std::to_string(r.selected.layer)+
      ",\"point\":\""+r.selected.point+"\",\"CPU_seen\":"+(r.seen[0]?"true":"false")+
      ",\"hybrid_seen\":"+(r.seen[1]?"true":"false")+",\"CPU_token\":"+std::to_string(r.token[0])+
      ",\"hybrid_token\":"+std::to_string(r.token[1])+
      ",\"CPU_shape\":"+(r.seen[0]?shape_json(r.shape[0]):"null")+
      ",\"hybrid_shape\":"+(r.seen[1]?shape_json(r.shape[1]):"null")+
      ",\"elements\":"+std::to_string(r.elements)+",\"CPU_fnv1a64\":"+std::to_string(r.hash[0])+
      ",\"hybrid_fnv1a64\":"+std::to_string(r.hash[1])+
      ",\"CPU_kth_margin\":"+(r.score_stats_seen[0]?real_json(r.kth_margin[0]):"null")+
      ",\"hybrid_kth_margin\":"+(r.score_stats_seen[1]?real_json(r.kth_margin[1]):"null")+
      ",\"CPU_exact_score_ties\":"+(r.score_stats_seen[0]?std::to_string(r.exact_score_ties[0]):"null")+
      ",\"hybrid_exact_score_ties\":"+(r.score_stats_seen[1]?std::to_string(r.exact_score_ties[1]):"null")+
      ",\"CPU_topk_ids\":"+(r.topk_seen[0]?"["+std::to_string(r.topk[0][0])+","+std::to_string(r.topk[0][1])+","+std::to_string(r.topk[0][2])+","+std::to_string(r.topk[0][3])+"]":"null")+
      ",\"hybrid_topk_ids\":"+(r.topk_seen[1]?"["+std::to_string(r.topk[1][0])+","+std::to_string(r.topk[1][1])+","+std::to_string(r.topk[1][2])+","+std::to_string(r.topk[1][3])+"]":"null")+
      ",\"CPU_weight_values\":"+(r.weights_seen[0]?"["+real_json(r.weights[0][0])+","+real_json(r.weights[0][1])+","+real_json(r.weights[0][2])+","+real_json(r.weights[0][3])+"]":"null")+
      ",\"hybrid_weight_values\":"+(r.weights_seen[1]?"["+real_json(r.weights[1][0])+","+real_json(r.weights[1][1])+","+real_json(r.weights[1][2])+","+real_json(r.weights[1][3])+"]":"null")+
      ",\"first_difference\":"+(r.first_difference==UINT32_MAX?"null":std::to_string(r.first_difference))+
      ",\"first_violation\":"+(r.first_violation==UINT32_MAX?"null":std::to_string(r.first_violation))+
      ",\"violations\":"+std::to_string(r.violations)+",\"gap\":\""+r.gap+"\",\"max_abs\":";
   char b[64];auto n=std::to_chars(b,b+sizeof(b),r.max_abs,std::chars_format::general,17);
   if(n.ec!=std::errc{})throw std::runtime_error("graph numeric serialization");
   s.append(b,n.ptr);s+='}';
   if(s.size()>output_budget_)throw std::length_error("graph output budget");
  }
  s+="]}";if(s.size()>output_budget_)throw std::length_error("graph output budget");return s;
 }
};
}
