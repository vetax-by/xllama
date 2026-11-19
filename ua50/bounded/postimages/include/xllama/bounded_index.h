#pragma once
#include "bounded_capture.h"
#include <algorithm>
namespace xllama::capture {
inline std::string quote(const std::string&s){need(s.size()<=256,"capture string cap");std::string out="\"";for(unsigned char c:s){need(c>=32&&c<127,"capture ASCII metadata");if(c=='"'||c=='\\')out+='\\';out+=char(c);}return out+'"';}
inline std::string hex(const std::array<unsigned char,32>&h){static const char*d="0123456789abcdef";std::string s;for(auto c:h){s+=d[c>>4];s+=d[c&15];}return s;}
inline bool sha_text(const std::string&s){return s.size()==64&&std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
struct Identity {
 std::string run,selection,instance,pfn,source,profile,config,ticket,main,trace;uint32_t pid=0;
 std::string json()const{
  need(pid&&run.size()<=160&&selection.size()<=160&&instance.size()<=160,"capture identity");
  for(const auto*s:{&source,&profile,&config,&ticket,&main,&trace})need(sha_text(*s),"capture identity SHA");
  // Sorted compact keys: this exact byte string is the identity digest input.
  return "{\"config_sha256\":"+quote(config)+",\"full_package_name\":"+quote(pfn)+",\"instance_id\":"+quote(instance)+",\"main_sha256\":"+quote(main)+",\"pid\":"+std::to_string(pid)+",\"profile_sha256\":"+quote(profile)+",\"run_id\":"+quote(run)+",\"selection_id\":"+quote(selection)+",\"source_sha256\":"+quote(source)+",\"ticket_sha256\":"+quote(ticket)+",\"trace_sha256\":"+quote(trace)+"}";
 }
};
inline const char* status_name(Status s){static const char* names[]={"NONE","CAPTURED","ALIAS_OF_CAPTURED","NOT_SELECTED","ROUTE_NOT_OBSERVED","TOKEN_MISMATCH","UNSUPPORTED_OP","UNSUPPORTED_TYPE","UNSUPPORTED_LAYOUT","UNSUPPORTED_BACKEND_MAPPING","UNREGISTERED_PRODUCER","STATE_MAPPING_UNAVAILABLE","STATE_CHANGED","NOT_READY","DUPLICATE_OCCURRENCE","BUDGET_EXHAUSTED","NONFINITE_OBSERVED","READ_FAILED","WRITE_INCOMPLETE","UNKNOWN_PENDING"};need(uint32_t(s)<20,"capture status enum");return names[uint32_t(s)];}
inline const char* type_name(Type t){static const char* names[]={"NONE","F32","F16","I32","Q4_K_BLOCK","Q6_K_BLOCK","Q8_K_BLOCK","U8"};need(uint32_t(t)<8,"capture dtype enum");return names[uint32_t(t)];}
struct Source {uint32_t node=0,op=0,type=0;std::array<uint64_t,4> ne{},nb{};uint64_t view=0,context=0,graph=0;};
struct Event {std::string kind;uint64_t sequence=0;uint32_t producer=0,consumer=0;std::vector<uint32_t> deps;};
struct Metadata {uint32_t record_id=0;std::string semantic,arm;uint32_t position=0,token=0,layer=0,occurrence=0;std::vector<uint64_t> shape;Source source;Event event;};
template<class T>inline std::string numbers(const T&v){std::string s="[";bool comma=false;for(auto n:v){if(comma)s+=',';comma=true;s+=std::to_string(n);}return s+"]";}
inline std::string source_json(const Source&s){return "{\"node_id\":"+std::to_string(s.node)+",\"op\":"+std::to_string(s.op)+",\"type\":"+std::to_string(s.type)+",\"ne\":"+numbers(s.ne)+",\"nb\":"+numbers(s.nb)+",\"view_offset\":"+std::to_string(s.view)+",\"context_epoch\":"+std::to_string(s.context)+",\"graph_epoch\":"+std::to_string(s.graph)+"}";}
inline std::string metadata_json(const Record&r,const Metadata&m){
 need(m.record_id==r.id,"capture record/metadata identity");
 need(m.position<=128&&m.layer<40&&m.occurrence<=65535&&m.semantic.size()<=128&&(m.arm=="CPU"||m.arm=="HYBRID")&&m.event.deps.size()<=16,"capture metadata cap");
 need(m.shape.size()<=4,"capture shape cap");
 return "{\"id\":"+std::to_string(r.id)+",\"semantic\":"+quote(m.semantic)+",\"arm\":"+quote(m.arm)+",\"position\":"+std::to_string(m.position)+",\"token\":"+std::to_string(m.token)+",\"layer\":"+std::to_string(m.layer)+",\"occurrence\":"+std::to_string(m.occurrence)+",\"status\":"+quote(status_name(r.status))+",\"dtype\":"+quote(type_name(r.type))+",\"payload_id\":"+std::to_string(r.payload)+",\"alias_of\":"+(r.alias_of?std::to_string(r.alias_of):"null")+",\"shape\":"+numbers(m.shape)+",\"source\":"+source_json(m.source)+",\"event\":{\"kind\":"+quote(m.event.kind)+",\"sequence\":"+std::to_string(m.event.sequence)+",\"producer_id\":"+std::to_string(m.event.producer)+",\"consumer_id\":"+std::to_string(m.event.consumer)+",\"dependency_ids\":"+numbers(m.event.deps)+"}}";
}
struct ResidualGroup {
 uint32_t id=0;std::string arm,branch="UNKNOWN";uint32_t position=0,token=0,layer=0,occurrence=0,A=0,B=0,R=0,O=0,V=0,add=0,src0=0,src1=0,direction=0;
 uint64_t state_epoch=0;bool stable=false;int range_start=-1,range_end=-1;
 std::string json()const{return "{\"id\":"+std::to_string(id)+",\"kind\":\"RESIDUAL_CVEC\",\"arm\":"+quote(arm)+",\"position\":"+std::to_string(position)+",\"token\":"+std::to_string(token)+",\"layer\":"+std::to_string(layer)+",\"occurrence\":"+std::to_string(occurrence)+",\"records\":{\"A\":"+std::to_string(A)+",\"B\":"+std::to_string(B)+",\"R\":"+std::to_string(R)+",\"O\":"+std::to_string(O)+",\"V\":"+(V?std::to_string(V):"null")+"},\"add\":{\"node_id\":"+std::to_string(add)+",\"src0_id\":"+std::to_string(src0)+",\"src1_id\":"+std::to_string(src1)+"},\"cvec\":{\"branch\":"+quote(branch)+",\"state_epoch\":"+std::to_string(state_epoch)+",\"stable\":"+(stable?"true":"false")+",\"layer_start\":"+std::to_string(range_start)+",\"layer_end\":"+std::to_string(range_end)+",\"direction_node_id\":"+std::to_string(direction)+"}}";}
};
struct LifecycleGroup {
 uint32_t id=0;std::string arm;uint64_t context=0,previous=0,request=0,completion=0;bool requested=false,observed=false;std::vector<uint32_t>records;
 std::string json()const{return "{\"id\":"+std::to_string(id)+",\"kind\":\"LIFECYCLE\",\"arm\":"+quote(arm)+",\"context_epoch\":"+std::to_string(context)+",\"previous_context_epoch\":"+std::to_string(previous)+",\"request_sequence\":"+std::to_string(request)+",\"completion_sequence\":"+std::to_string(completion)+",\"data_clear_requested\":"+(requested?"true":"false")+",\"data_clear_observed\":"+(observed?"true":"false")+",\"records\":"+numbers(records)+"}";}
};
template<class Group>inline std::string index_json(const Identity&id,const std::string&leaf,const std::vector<unsigned char>&bundle,const Hash&hash,const std::vector<Record>&records,const std::vector<Metadata>&meta,const std::vector<Group>&groups,size_t cap,uint64_t sequence){
 need(cap<=262144&&records.size()==meta.size()&&records.size()<=64&&groups.size()<=64,"capture index cap");
 std::string s="{\"schema\":\"xllama-bounded-diagnostics-index-v1\",\"identity\":"+id.json()+",\"bundle\":{\"leaf\":"+quote(leaf)+",\"bytes\":"+std::to_string(bundle.size())+",\"sha256\":"+quote(hex(hash(bundle.data(),bundle.size())))+"},\"records\":[";
 for(size_t i=0;i<records.size();++i){if(i)s+=',';const auto row=metadata_json(records[i],meta[i]);need(s.size()<=cap&&row.size()<=cap-s.size(),"capture index record budget");s+=row;}
 s+="],\"groups\":[";for(size_t i=0;i<groups.size();++i){if(i)s+=',';const auto g=groups[i].json();need(s.size()<=cap&&g.size()<=cap-s.size(),"capture index group budget");s+=g;}
 s+="],\"capabilities\":[],\"ownership\":{\"writer_closed\":true,\"pending_owners\":0,\"closed_sequence\":"+std::to_string(sequence)+"}}";
 need(s.size()<=cap,"capture index output cap");return s;
}
}
