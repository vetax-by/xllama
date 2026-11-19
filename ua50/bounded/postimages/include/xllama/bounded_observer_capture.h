#pragma once
#include "bounded_plan.h"
#include "bounded_index.h"
#include "ggml-impl.h"
#include "ggml-capture.h"
namespace xllama::capture {
struct ObserverGroup {
 uint32_t id=0,position=0,token=0;std::string arm,status="CAPTURED",logical,mapped;uint64_t generation=0,nodes=0,leafs=0,splits=0,calls=0,syncs=0,copies=0,bytes=0,wall=0,work=0,raw=0,records=0,ns=0;int result=-1;
 std::string json()const{return "{\"id\":"+std::to_string(id)+",\"kind\":\"OBSERVER_CONTROL\",\"arm\":"+quote(arm)+",\"position\":"+std::to_string(position)+",\"token\":"+std::to_string(token)+",\"status\":"+quote(status)+",\"signature_method\":\"FNV1A64_STRUCTURAL_ORDINAL_V1\",\"logical_signature\":"+quote(logical)+",\"mapped_signature\":"+quote(mapped)+",\"logical_generation\":"+std::to_string(generation)+",\"nodes\":"+std::to_string(nodes)+",\"leafs\":"+std::to_string(leafs)+",\"backend_splits\":"+std::to_string(splits)+",\"compute_calls\":"+std::to_string(calls)+",\"synchronize_calls\":"+std::to_string(syncs)+",\"copy_calls\":"+std::to_string(copies)+",\"copy_bytes\":"+std::to_string(bytes)+",\"compute_wall_ns\":"+std::to_string(wall)+",\"result\":"+std::to_string(result)+",\"owner_work\":"+std::to_string(work)+",\"owner_raw_bytes\":"+std::to_string(raw)+",\"owner_records\":"+std::to_string(records)+",\"owner_instrumentation_ns\":"+std::to_string(ns)+"}";}
};
class ObserverCapture {
 struct Logical {const ggml_cgraph*graph=nullptr;std::string signature;uint64_t generation=0;};
 Plan plan_;Owner&owner_;std::vector<Logical>logical_;std::vector<ObserverGroup>groups_;uint32_t phase_=0,position_=0,token_=0;uint64_t generation_=0;Logical current_;size_t active_=SIZE_MAX;
 bool selected()const{for(const auto&s:plan_.selections)if(s.position==position_&&s.token==token_)return true;return false;}
 std::string fingerprint(const ggml_cgraph*g){need(g&&g->n_nodes>=0&&g->n_leafs>=0&&g->n_nodes<=4096&&g->n_leafs<=4096,"observer graph extent unsupported");const size_t n=size_t(g->n_nodes)+g->n_leafs;need(owner_.reserve_work(n*16+size_t(g->n_nodes)*64),"observer graph metadata budget");std::vector<std::pair<const ggml_tensor*,uint64_t>>ids;ids.reserve(n);for(int i=0;i<g->n_nodes;++i)ids.push_back({g->nodes[i],uint64_t(i+1)});for(int i=0;i<g->n_leafs;++i)ids.push_back({g->leafs[i],uint64_t(g->n_nodes+i+1)});auto less=[](const auto&a,const auto&b){return std::less<const ggml_tensor*>{}(a.first,b.first);};std::sort(ids.begin(),ids.end(),less);for(size_t i=1;i<ids.size();++i)need(ids[i-1].first!=ids[i].first,"observer graph duplicate ordinal");
  auto ordinal=[&](const ggml_tensor*t){if(!t)return uint64_t(0);auto it=std::lower_bound(ids.begin(),ids.end(),std::make_pair(t,uint64_t(0)),less);need(it!=ids.end()&&it->first==t,"observer unregistered source edge");return it->second;};uint64_t fnv=14695981039346656037ull;auto byte=[&](uint8_t v){fnv^=v;fnv*=1099511628211ull;};auto number=[&](uint64_t v){for(int k=0;k<8;++k)byte(uint8_t(v>>(8*k)));};number(g->n_nodes);number(g->n_leafs);
  for(size_t i=0;i<n;++i){const auto*t=i<size_t(g->n_nodes)?g->nodes[i]:g->leafs[i-g->n_nodes];need(t,"observer null tensor");number(t->op);number(t->type);for(int d=0;d<4;++d){number(t->ne[d]);number(t->nb[d]);}number(t->view_offs);number(ordinal(t->view_src));for(auto*src:t->src)number(ordinal(src));for(size_t b=0;b<sizeof(t->op_params);++b)byte(reinterpret_cast<const uint8_t*>(t->op_params)[b]);}
  char text[17];for(int i=0;i<16;++i)text[i]="0123456789abcdef"[(fnv>>((15-i)*4))&15];text[16]=0;return text;
 }
public:
 ObserverCapture(const Plan&p,Owner&o):plan_(p),owner_(o){logical_.reserve(64);groups_.reserve(64);}
 void context(uint32_t phase){phase_=phase;logical_.clear();current_={};active_=SIZE_MAX;}
 void frame(uint32_t pos,uint32_t token){position_=pos;token_=token;active_=SIZE_MAX;}
 void event(const ggml_capture_event&e){if(!plan_.observer_control)return;
  if(e.kind==GGML_CAPTURE_LOGICAL_GRAPH){Logical entry;entry.graph=e.graph;entry.generation=++generation_;try{entry.signature=fingerprint(e.graph);}catch(...){entry.signature.clear();}auto it=std::find_if(logical_.begin(),logical_.end(),[&](const auto&old){return old.graph==e.graph;});if(it!=logical_.end())*it=std::move(entry);else{need(logical_.size()<64,"observer logical graph cache cap");logical_.push_back(std::move(entry));}return;}
  if(e.kind==GGML_CAPTURE_LOGICAL_REFERENCE){current_={};for(const auto&row:logical_)if(row.graph==e.graph)current_=row;return;}
  if(!selected())return;
  if(e.kind==GGML_CAPTURE_GRAPH_BEGIN){need(active_==SIZE_MAX&&groups_.size()<64,"observer receipt cap");ObserverGroup g;g.id=uint32_t(groups_.size()+1);g.arm=phase_==1?"CPU":"HYBRID";g.position=position_;g.token=token_;g.logical=current_.signature;g.generation=current_.generation;if(g.logical.empty())g.status="LOGICAL_GRAPH_UNAVAILABLE";try{g.mapped=fingerprint(e.graph);}catch(...){g.status="MAPPED_GRAPH_UNAVAILABLE";}if(e.graph){g.nodes=e.graph->n_nodes;g.leafs=e.graph->n_leafs;}groups_.push_back(std::move(g));active_=groups_.size()-1;return;}
  if(e.kind==GGML_CAPTURE_GRAPH_END&&active_!=SIZE_MAX){auto&g=groups_[active_];for(size_t i=1;i<=5;++i)need(e.values[i]>=0,"observer negative counter");need(e.values[7]>=0,"observer wall clock");g.splits=e.values[1];g.calls=e.values[2];g.syncs=e.values[3];g.copies=e.values[4];g.bytes=e.values[5];g.result=int(e.values[6]);g.wall=e.values[7];g.work=owner_.work();g.raw=owner_.raw().size();g.records=owner_.records().size();g.ns=owner_.diagnostic_ns();active_=SIZE_MAX;}
 }
 size_t owned_bytes()const{return sizeof(*this)+logical_.capacity()*sizeof(Logical)+groups_.capacity()*sizeof(ObserverGroup);}
 const auto&groups()const{return groups_;}
};
}
