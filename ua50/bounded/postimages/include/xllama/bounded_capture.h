#pragma once
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace xllama::capture {
enum class Status:uint32_t {captured=1,alias=2,not_selected=3,route=4,token=5,op=6,type=7,layout=8,backend=9,producer=10,state=11,changed=12,not_ready=13,duplicate=14,budget=15,nonfinite=16,read=17,write=18,pending=19};
enum class Type:uint32_t {none=0,f32=1,f16=2,i32=3,q4=4,q6=5,q8=6,u8=7};
inline size_t width(Type t){switch(t){case Type::f32:case Type::i32:return 4;case Type::f16:return 2;case Type::q4:return 144;case Type::q6:return 210;case Type::q8:return 292;case Type::u8:return 1;default:return 0;}}
inline constexpr size_t metadata_reserve_bytes=524288,owner_static_reserve_bytes=4096,record_reserve_bytes=192;
struct Limits {uint32_t records=64;size_t raw=1048576,bundle=1081344,index=262144,owner=8388608;uint64_t work=40000000;uint32_t time_ms=20000;};
struct Record {uint32_t id=0,payload=0,alias_of=0;Status status=Status::not_ready;Type type=Type::none;uint64_t elements=0;size_t offset=0,bytes=0;std::array<unsigned char,32> hash{};};
using Hash=std::function<std::array<unsigned char,32>(const void*,size_t)>;
using Now=std::function<uint64_t()>;
inline void need(bool b,const char*s){if(!b)throw std::invalid_argument(s);}
inline size_t add(size_t a,size_t b){need(b<=SIZE_MAX-a,"capture addition overflow");return a+b;}
inline size_t mul(size_t a,size_t b){need(!a||b<=SIZE_MAX/a,"capture multiplication overflow");return a*b;}
inline bool finite(Type t,const unsigned char*p,size_t n){
 if(t==Type::f32){for(size_t i=0;i<n;i+=4){float f;std::memcpy(&f,p+i,4);if(!std::isfinite(f))return false;}}
 if(t==Type::f16){for(size_t i=0;i<n;i+=2){uint16_t x;std::memcpy(&x,p+i,2);if((x&0x7c00)==0x7c00)return false;}}
 if(t==Type::q4||t==Type::q6){const size_t stride=width(t);for(size_t i=0;i<n;i+=stride){const size_t first=t==Type::q4?0:208;uint16_t v;std::memcpy(&v,p+i+first,2);if((v&0x7c00)==0x7c00)return false;if(t==Type::q4){std::memcpy(&v,p+i+2,2);if((v&0x7c00)==0x7c00)return false;}}}
 if(t==Type::q8){for(size_t i=0;i<n;i+=292){float v;std::memcpy(&v,p+i,4);if(!std::isfinite(v))return false;}}
 return true;
}
/* A single pre-admitted arena: CPU and hybrid, graph/state/dispatch all consume
 * this same owner. No request pointer, pending I/O, or backend lease is retained. */
class Owner {
 struct NodeKey{const void*pointer;uint64_t context,graph;};std::vector<NodeKey>nodes_;
 Limits lim_;Hash hash_;std::vector<unsigned char> raw_;std::vector<Record> rows_;
 uint64_t work_=0,events_=0;uint32_t next_payload_=1;bool closed_=false,failed_=false;
 Now now_;uint64_t used_ns_=0,active_start_=0;uint32_t depth_=0;
 struct Timed {
  Owner&o;explicit Timed(Owner&v):o(v){if(!o.depth_)o.active_start_=o.now_();++o.depth_;}
  Timed(const Timed&)=delete;Timed&operator=(const Timed&)=delete;
  ~Timed(){if(--o.depth_)return;const uint64_t end=o.now_();if(end<o.active_start_||end-o.active_start_>UINT64_MAX-o.used_ns_)o.failed_=true;else o.used_ns_+=end-o.active_start_;if(o.used_ns_>=uint64_t(o.lim_.time_ms)*1000000)o.failed_=true;}
 };
 bool consume(uint64_t work){
  const uint64_t now=now_();
  if(depth_&&now<active_start_){failed_=true;return false;}
  const uint64_t delta=depth_?now-active_start_:0;
  if(delta>UINT64_MAX-used_ns_||failed_||used_ns_+delta>=uint64_t(lim_.time_ms)*1000000||work>lim_.work-work_){failed_=true;return false;}work_+=work;return true;
 }
 Record& pending(uint32_t id){need(!closed_,"capture already closed");need(id&&rows_.size()<lim_.records,"capture record cap");for(const auto&r:rows_)need(r.id!=id,"capture duplicate record");rows_.push_back({});rows_.back().id=id;return rows_.back();}
public:
 Owner(Limits l,Hash h,Now clock=[](){return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}):lim_(l),hash_(std::move(h)),now_(std::move(clock)){
  static_assert(sizeof(float)==4&&std::numeric_limits<float>::is_iec559,"capture IEEE binary32");const uint16_t endian=1;need(*reinterpret_cast<const unsigned char*>(&endian)==1,"capture little endian target");
  need(bool(hash_)&&l.records&&l.records<=64&&l.raw<=1048576&&l.bundle<=1081344&&l.index<=262144&&l.time_ms&&l.time_ms<=120000,"capture hard caps");
  static_assert(sizeof(Record)<=record_reserve_bytes,"capture record reserve");need(sizeof(*this)<=owner_static_reserve_bytes,"capture static owner reserve");
  const size_t charged=add(add(add(owner_static_reserve_bytes,metadata_reserve_bytes),add(l.raw,mul(l.records,record_reserve_bytes))),add(l.bundle,l.index));
  need(charged<=l.owner&&l.owner<=67108864,"capture owner coexistence");
  raw_.reserve(l.raw);rows_.reserve(l.records);nodes_.reserve(256);
 }
 uint32_t register_node(const void*p,uint64_t context,uint64_t graph){need(p&&context&&graph,"capture source handle");for(size_t i=0;i<nodes_.size();++i)if(nodes_[i].pointer==p&&nodes_[i].context==context&&nodes_[i].graph==graph)return uint32_t(i+1);need(nodes_.size()<256,"capture global source handle cap");nodes_.push_back({p,context,graph});return uint32_t(nodes_.size());}
 uint32_t next_id()const{need(!closed_,"capture already closed");uint32_t id=0;for(const auto&r:rows_)if(r.id>id)id=r.id;need(id<UINT32_MAX,"capture ID overflow");return id+1;}
 uint64_t next_sequence(){need(!closed_&&events_<UINT64_MAX,"capture event sequence");return ++events_;}
 uint64_t sequence()const{return events_;}
 const auto& records()const{return rows_;}const auto& raw()const{return raw_;}
 size_t owned_bytes()const{return sizeof(*this)+raw_.capacity()+rows_.capacity()*sizeof(Record)+nodes_.capacity()*sizeof(NodeKey);}
 auto instrument(){return Timed(*this);}
 bool within_budget(){return consume(0);}
 // Output may be charged after sealing raw records; this never reopens them.
 bool reserve_output(uint64_t work){return consume(work);}
 bool closed()const{return closed_;}bool failed()const{return failed_;}
 bool reserve_work(uint64_t work){return !closed_&&consume(work);}
 bool charge_external(uint64_t work,uint64_t elapsed){if(closed_||failed_)return false;if(elapsed>UINT64_MAX-used_ns_){failed_=true;return false;}used_ns_+=elapsed;return consume(work);}
 uint64_t work()const{return work_;}
 uint64_t diagnostic_ns()const{return used_ns_;}
 void gap(uint32_t id,Status s){need(uint32_t(s)>=3&&uint32_t(s)<=19&&s!=Status::nonfinite,"capture gap status");pending(id).status=s;}
 template<class Reader>bool copy(uint32_t id,Type type,uint64_t elements,Reader&&reader){
  Timed timed(*this);auto&r=pending(id);const size_t w=width(type);
  if(!w||elements>SIZE_MAX/w){r.status=Status::type;return false;}
  const size_t n=size_t(elements)*w;
  if(!n||n>65536||n>lim_.raw-raw_.size()||!consume(n*2)){r.status=Status::budget;return false;}
  const size_t at=raw_.size();raw_.resize(at+n);
  try{
   if(!reader(raw_.data()+at,n)){raw_.resize(at);r.status=Status::read;return false;}
   if(!consume(n)){raw_.resize(at);r.status=Status::budget;return false;}
   const bool is_finite=finite(type,raw_.data()+at,n);
   const auto digest=hash_(raw_.data()+at,n);
   if(!consume(0)){raw_.resize(at);r.status=Status::budget;return false;}
   r.status=is_finite?Status::captured:Status::nonfinite;r.type=type;r.elements=elements;r.offset=at;r.bytes=n;r.hash=digest;r.payload=next_payload_++;return true;
  }catch(...){raw_.resize(at);r.status=Status::read;failed_=true;return false;}
 }
 bool alias(uint32_t id,uint32_t original){
  Timed timed(*this);Record base;bool found=false;for(const auto&r:rows_)if(r.id==original&&r.status==Status::captured){base=r;found=true;break;}
  auto&r=pending(id);if(!found||!consume(0)){r.status=Status::not_ready;return false;}
  r=base;r.id=id;r.alias_of=original;r.status=Status::alias;return true;
 }
 std::vector<unsigned char> finish(const std::array<unsigned char,32>&identity){
  Timed timed(*this);need(!closed_,"capture already closed");
  const size_t po=add(96,mul(rows_.size(),96)),footer=add(po,raw_.size()),total=add(footer,56);
  need(total<=lim_.bundle&&consume(mul(total,2)),"capture bundle/work cap");
  std::vector<unsigned char> out(total,0);
  auto put=[&](size_t o,uint64_t v,size_t n){need(o<=out.size()&&n<=out.size()-o,"capture encode bound");for(size_t i=0;i<n;++i)out[o+i]=unsigned(v>>(8*i))&255;};
  std::memcpy(out.data(),"XLDIAG01",8);put(8,1,2);put(10,0x4c45,2);put(12,96,4);put(16,96,4);put(20,rows_.size(),4);put(28,po,8);put(36,total,8);put(44,footer,8);std::memcpy(out.data()+52,identity.data(),32);
  bool gaps=false;for(size_t i=0;i<rows_.size();++i){const auto&r=rows_[i];const size_t o=96+96*i;put(o,r.id,4);put(o+4,r.payload,4);put(o+8,uint32_t(r.status),4);put(o+12,uint32_t(r.type),4);put(o+16,r.elements,8);if(r.bytes)put(o+24,po+r.offset,8);put(o+32,r.bytes,8);std::memcpy(out.data()+o+40,r.hash.data(),32);gaps|=uint32_t(r.status)>2;}
  std::memcpy(out.data()+po,raw_.data(),raw_.size());std::memcpy(out.data()+footer,"XLDEND01",8);put(footer+8,total,8);put(footer+16,rows_.size(),4);put(footer+20,gaps?2:1,4);
  auto body=hash_(out.data(),footer);need(consume(0),"capture expired during serialization");std::memcpy(out.data()+footer+24,body.data(),32);closed_=true;return out;
 }
};
}
