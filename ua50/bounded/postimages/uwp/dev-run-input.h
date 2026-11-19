#pragma once
#include "xllama/dev_run_config.h"
#include "xllama/ingress_v2.h"
#include "xllama/first_answer_record.h"
#include "xllama/ingress_reason_v2.h"
#include "xllama/qualification_mailbox.h"
#include <chrono>
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <filesystem>
#include <cstring>
#include <memory>
#include <mutex>
namespace xllama::bridge {
struct DevHandle {
 HANDLE value=INVALID_HANDLE_VALUE;
 DevHandle()=default;explicit DevHandle(HANDLE v):value(v){}
 DevHandle(const DevHandle&)=delete;DevHandle&operator=(const DevHandle&)=delete;
 DevHandle(DevHandle&& other) noexcept:value(other.value){other.value=INVALID_HANDLE_VALUE;}
 DevHandle&operator=(DevHandle&& other) noexcept{if(this!=&other){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=other.value;other.value=INVALID_HANDLE_VALUE;}return *this;}
 ~DevHandle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
 void close(){HANDLE v=value;value=INVALID_HANDLE_VALUE;if(v!=INVALID_HANDLE_VALUE&&!CloseHandle(v))throw std::runtime_error("native handle close failure");}
};
inline uint64_t dev_utc_ms(){FILETIME f{};GetSystemTimeAsFileTime(&f);ULARGE_INTEGER v{};v.LowPart=f.dwLowDateTime;v.HighPart=f.dwHighDateTime;return v.QuadPart<116444736000000000ull?0:(v.QuadPart-116444736000000000ull)/10000;}
inline qualification::Clock qualification_now(){const auto steady=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();return {dev_utc_ms(),steady>0?uint64_t(steady):0};}
inline void qualification_guard_check(ingress_reason_v2::Recorder* recorder=nullptr){const auto n=qualification_now();const auto mask=qualification::run_guard.mask(n);if(!mask)return;
 ingress_reason_v2::Event e;e.mask=mask;e.stopped=true;e.utc_available=n.utc!=0;e.device_utc_ms=n.utc;const auto first=qualification::run_guard.clock.first.steady;if(n.steady>=first&&n.steady-first<=UINT64_MAX/1000000){e.steady_available=true;e.steady_elapsed_ns=(n.steady-first)*1000000;}qualification::run_guard.reasons.record(e);if(recorder)recorder->record(e);throw std::runtime_error("qualification stop predicate mask="+std::to_string(mask));}
inline std::string dev_sha(const std::string& bytes){
 BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::vector<unsigned char> object;std::array<unsigned char,32> sum{};
 auto check=[](NTSTATUS s){if(s<0)throw std::runtime_error("config BCrypt SHA");};
 try{check(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0));DWORD size=0,got=0;check(BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof(size),&got,0));if(got!=sizeof(size)||!size||size>65536)throw std::runtime_error("config SHA object bound");object.resize(size);check(BCryptCreateHash(alg,&hash,object.data(),size,nullptr,0,0));check(BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),ULONG(bytes.size()),0));check(BCryptFinishHash(hash,sum.data(),ULONG(sum.size()),0));BCryptDestroyHash(hash);hash=nullptr;BCryptCloseAlgorithmProvider(alg,0);alg=nullptr;
 }catch(...){if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);throw;}
 const char*d="0123456789abcdef";std::string out;out.reserve(64);for(auto x:sum){out+=d[x>>4];out+=d[x&15];}return out;
}
inline void open_dev_file(DevHandle& h,const std::filesystem::path& path,bool ticket){CREATEFILE2_EXTENDED_PARAMETERS p{};p.dwSize=sizeof(p);p.dwFileFlags=FILE_FLAG_OPEN_REPARSE_POINT;h.value=CreateFile2(path.c_str(),GENERIC_READ|(ticket?DELETE:0),ticket?0:FILE_SHARE_READ,OPEN_EXISTING,&p);if(h.value==INVALID_HANDLE_VALUE)throw std::runtime_error("config/ticket held open refused");}
inline std::string read_dev_file(DevHandle& h,size_t cap){
 FILE_STANDARD_INFO before{};FILE_ATTRIBUTE_TAG_INFO attr{};FILE_ID_INFO id{},postid{};
 if(GetFileType(h.value)!=FILE_TYPE_DISK||!GetFileInformationByHandleEx(h.value,FileStandardInfo,&before,sizeof(before))||!GetFileInformationByHandleEx(h.value,FileAttributeTagInfo,&attr,sizeof(attr))||!GetFileInformationByHandleEx(h.value,FileIdInfo,&id,sizeof(id))||before.NumberOfLinks!=1||before.Directory||before.DeletePending||(attr.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||before.EndOfFile.QuadPart<=0||uint64_t(before.EndOfFile.QuadPart)>cap)throw std::runtime_error("config/ticket file shape/bound");
 std::string raw(size_t(before.EndOfFile.QuadPart),'\0');DWORD got=0;if(!ReadFile(h.value,raw.data(),DWORD(raw.size()),&got,nullptr)||got!=raw.size())throw std::runtime_error("config/ticket short read");unsigned char extra=0;DWORD trailing=0;if(!ReadFile(h.value,&extra,1,&trailing,nullptr)||trailing)throw std::runtime_error("config/ticket trailing read");FILE_STANDARD_INFO after{};if(!GetFileInformationByHandleEx(h.value,FileStandardInfo,&after,sizeof(after))||!GetFileInformationByHandleEx(h.value,FileIdInfo,&postid,sizeof(postid))||after.EndOfFile.QuadPart!=before.EndOfFile.QuadPart||memcmp(&id,&postid,sizeof(id)))throw std::runtime_error("config/ticket held identity changed");return raw;
}
struct SelectedIngress {
 bool native_command=false;DevHandle command_file;std::string command_raw,command_sha;
 ingress_v2::Intent intent;DevHandle ticket,config,intent_file,window_file;std::string intent_raw,ticket_raw,ticket_sha,config_raw,config_sha,window_raw,window_sha;
 SelectedIngress()=default;SelectedIngress(const SelectedIngress&)=delete;SelectedIngress&operator=(const SelectedIngress&)=delete;
 SelectedIngress(SelectedIngress&&)=default;SelectedIngress&operator=(SelectedIngress&&)=default;
};
inline std::mutex selected_ingress_mutex;
inline std::unique_ptr<SelectedIngress> selected_ingress;
inline ingress_reason_v2::Recorder& selected_preclaim_reason(){static thread_local ingress_reason_v2::Recorder reason;return reason;}
inline void record_selected_preclaim_reason(uint32_t mask,uint64_t limit=0,uint64_t usage=0,bool quota_known=false){
 ingress_reason_v2::Event e;e.mask=mask;e.stopped=true;e.device_utc_ms=dev_utc_ms();e.utc_available=e.device_utc_ms!=0;e.limit_available=quota_known;e.usage_available=quota_known;e.old_limit=limit;e.new_limit=limit;e.usage=usage;e.pressure=(mask&ingress_reason_v2::pressure_or_headroom)!=0;selected_preclaim_reason().record(e);
}
inline void install_selected_ingress(SelectedIngress&& s){std::lock_guard<std::mutex> lock(selected_ingress_mutex);dev_run::need(!selected_ingress,"selected ingress already installed");selected_ingress=std::make_unique<SelectedIngress>(std::move(s));}
inline SelectedIngress take_selected_ingress(ingress_v2::Stage stage){std::lock_guard<std::mutex> lock(selected_ingress_mutex);dev_run::need(selected_ingress&&selected_ingress->intent.stage==stage,"selected ingress stage missing/mismatch");SelectedIngress out(std::move(*selected_ingress));selected_ingress.reset();return out;}
inline void verify_selected_ingress(const SelectedIngress& s){if(s.native_command){
 qualification_guard_check();dev_run::need(qualification::run_guard.active.load()&&s.command_file.value!=INVALID_HANDLE_VALUE&&dev_sha(s.command_raw)==s.command_sha,"native command lease");
 const auto c=qualification::command(s.command_raw);dev_run::need(c.run==s.intent.run&&c.selection==s.intent.selection&&c.package==s.intent.package&&c.expires==s.intent.expires&&s.ticket.value!=INVALID_HANDLE_VALUE&&dev_sha(s.ticket_raw)==s.ticket_sha&&s.ticket_sha==s.intent.ticket_sha,"native issued ticket binding");return;}
dev_run::need(s.intent_file.value!=INVALID_HANDLE_VALUE&&ingress_v2::parse_intent(s.intent_raw).run==s.intent.run,"selected intent lease/binding");dev_run::need(s.ticket.value!=INVALID_HANDLE_VALUE&&s.ticket_sha==s.intent.ticket_sha&&dev_sha(s.ticket_raw)==s.ticket_sha,"selected ticket lease/hash");dev_run::need(s.window_file.value!=INVALID_HANDLE_VALUE&&s.window_sha==dev_sha(s.window_raw),"selected operator window lease/hash");const auto w=ingress_v2::parse_operator_window(s.window_raw);dev_run::need(w.run==s.intent.run&&w.selection==s.intent.selection&&w.intent_sha==dev_sha(s.intent_raw)&&w.package==s.intent.package&&w.expires==s.intent.expires,"selected operator window binding");}
inline void commit_ingress_attempt(const std::filesystem::path& path,const ingress_v2::Intent& i){
 const auto quote=first_answer::quote;const std::string raw="{\"schema\":\"xllama-headless-attempt-v2\",\"stage\":"+quote(ingress_v2::stage_name(i.stage)) +",\"run_id\":"+quote(i.run)+",\"ticket_sha256\":"+quote(i.ticket_sha)+",\"full_package_name\":"+quote(i.package)+"}";
 dev_run::need(raw.size()<=768,"attempt marker cap");DevHandle h(CreateFile2(path.c_str(),GENERIC_WRITE,0,CREATE_NEW,nullptr));if(h.value==INVALID_HANDLE_VALUE)throw std::runtime_error("attempt CREATE_NEW refused");DWORD n=0;if(!WriteFile(h.value,raw.data(),DWORD(raw.size()),&n,nullptr)||n!=raw.size()||!FlushFileBuffers(h.value))throw std::runtime_error("attempt durable write refused");h.close();
}
inline void commit_ingress_terminal(const std::filesystem::path& path,ingress_v2::Stage stage,const std::string& run,const std::string& ticket_sha,const std::string& output_sha,const std::string& package,bool normal){
 const auto quote=first_answer::quote;
 const std::string raw="{\"schema\":\"xllama-headless-terminal-v2\",\"stage\":"+quote(ingress_v2::stage_name(stage)) +",\"run_id\":"+quote(run)+",\"ticket_sha256\":"+quote(ticket_sha)+",\"output_sha256\":"+quote(output_sha)+",\"full_package_name\":"+quote(package)+",\"normal\":"+(normal?"true":"false")+"}";
 dev_run::need(raw.size()<=1024,"terminal marker cap");DevHandle h(CreateFile2(path.c_str(),GENERIC_WRITE,0,CREATE_NEW,nullptr));if(h.value==INVALID_HANDLE_VALUE)throw std::runtime_error("terminal marker CREATE_NEW refused");DWORD n=0;if(!WriteFile(h.value,raw.data(),DWORD(raw.size()),&n,nullptr)||n!=raw.size()||!FlushFileBuffers(h.value))throw std::runtime_error("terminal marker durable write refused");h.close();
}
inline void commit_ingress_reason(const std::filesystem::path&,const ingress_reason_v2::Recorder&,ingress_reason_v2::Stage,const std::string&,const std::string&,const std::string&,uint64_t);
// A selected adapter may return before parsing or claiming (busy owner, quota,
// expired ticket). Close only that unclaimed, output-free attempt. A consumed
// ticket, any claim/output, or uncertain file state remains an orphan; never
// manufacture a successful stage result or silently replay the ticket.
inline bool finalize_selected_preclaim_refusal(const std::filesystem::path& root,const ingress_v2::Intent& i){
 {std::lock_guard<std::mutex> lock(selected_ingress_mutex);selected_ingress.reset();}
 const auto leaf=ingress_v2::result_prefix(i.stage,i.run);
 const auto base=root/std::filesystem::path(std::wstring(leaf.begin(),leaf.end()));
 auto present=[](const std::filesystem::path& p){const DWORD a=GetFileAttributesW(p.c_str());if(a==INVALID_FILE_ATTRIBUTES){if(GetLastError()==ERROR_FILE_NOT_FOUND)return false;throw std::runtime_error("preclaim path state unknown");}if(a&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE))throw std::runtime_error("preclaim path shape");return true;};
 const auto claim=std::filesystem::path(base.wstring()+L"claim"),output=std::filesystem::path(base.wstring()+L"output.json"),terminal=std::filesystem::path(base.wstring()+L"terminal-v2.json");
 const bool has_terminal=present(terminal),has_claim=present(claim),has_output=present(output);
 if(has_terminal||has_claim||has_output)return false;
 const auto ticket_leaf=ingress_v2::ticket_leaf(i.stage,i.run);const auto ticket=root/std::filesystem::path(std::wstring(ticket_leaf.begin(),ticket_leaf.end()));
 const bool has_ticket=present(ticket);if(!has_ticket)return false;
 DevHandle held;open_dev_file(held,ticket,true);const bool ticket_equal=dev_sha(read_dev_file(held,4096))==i.ticket_sha;
 const auto package=winrt::to_string(winrt::Windows::ApplicationModel::Package::Current().Id().FullName());
 if(!ingress_v2::preclaim_refusal_admitted(has_claim,has_output,has_terminal,has_ticket,ticket_equal,package==i.package))throw std::runtime_error("preclaim ticket/package drift");
 const auto quote=first_answer::quote;
 const std::string raw="{\"schema\":\"xllama-headless-preclaim-refusal-v2\",\"normal\":false,\"status\":\"REFUSED_BEFORE_NATIVE_CLAIM\",\"stage\":"+quote(ingress_v2::stage_name(i.stage)) +",\"run_id\":"+quote(i.run)+",\"ticket_sha256\":"+quote(i.ticket_sha)+",\"full_package_name\":"+quote(package)+"}";
 if(raw.size()>1024)throw std::runtime_error("preclaim refusal cap");
 DevHandle out(CreateFile2(output.c_str(),GENERIC_WRITE,0,CREATE_NEW,nullptr));if(out.value==INVALID_HANDLE_VALUE)throw std::runtime_error("preclaim refusal CREATE_NEW");DWORD n=0;
 if(!WriteFile(out.value,raw.data(),DWORD(raw.size()),&n,nullptr)||n!=raw.size()||!FlushFileBuffers(out.value))throw std::runtime_error("preclaim refusal write/flush");out.close();
 try{const auto reason_stage=i.stage==ingress_v2::Stage::identity?ingress_reason_v2::Stage::identity:i.stage==ingress_v2::Stage::proof?ingress_reason_v2::Stage::proof:ingress_reason_v2::Stage::cpu_dev;commit_ingress_reason(std::filesystem::path(base.wstring()+L"reason-v2.json"),selected_preclaim_reason(),reason_stage,i.run,i.ticket_sha,dev_sha(raw),i.expires);}catch(const std::exception&e){log_output(std::string("[ingress-v2] preclaim reason unavailable: ")+e.what()+"\n");}
 commit_ingress_terminal(terminal,i.stage,i.run,i.ticket_sha,dev_sha(raw),package,false);return true;
}
inline void commit_ingress_reason(const std::filesystem::path& path,const ingress_reason_v2::Recorder& reason,ingress_reason_v2::Stage stage,const std::string& run,const std::string& ticket_sha,const std::string& output_sha,uint64_t expires){
 const auto raw=ingress_reason_v2::render(reason,stage,run,ticket_sha,output_sha,expires);
 DevHandle h(CreateFile2(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,CREATE_NEW,nullptr));if(h.value==INVALID_HANDLE_VALUE)throw std::runtime_error("ingress reason CREATE_NEW refused");
 DWORD n=0;if(!WriteFile(h.value,raw.data(),DWORD(raw.size()),&n,nullptr)||n!=raw.size()||!FlushFileBuffers(h.value))throw std::runtime_error("ingress reason durable write refused");h.close();
}
struct DevRunInput {
 DevHandle held_ticket;dev_run::Ticket ticket;dev_run::Config config;dev_run::OneUse use;std::string config_sha,ticket_sha;std::filesystem::path root;
 explicit DevRunInput(std::filesystem::path base):root(std::move(base)){
  open_dev_file(held_ticket,root/L"ua50-dev-run-v1.ticket.json",true);auto ticket_bytes=read_dev_file(held_ticket,dev_run::ticket_cap);ticket_sha=dev_sha(ticket_bytes);ticket=dev_run::parse_ticket(std::move(ticket_bytes),dev_utc_ms());
  DevHandle cfg;const std::string leaf="ua50-dev-run-v1."+ticket.id+".config.json";open_dev_file(cfg,root/std::wstring(leaf.begin(),leaf.end()),false);auto bytes=read_dev_file(cfg,dev_run::config_cap);config_sha=dev_sha(bytes);config=dev_run::parse_config(std::move(bytes));cfg.close();dev_run::need(ticket.id==config.id&&ticket.config_sha==config_sha,"ticket/config binding");
 }
 DevRunInput(std::filesystem::path base,SelectedIngress&& selected):root(std::move(base)){
  dev_run::need(selected.intent.stage==ingress_v2::Stage::cpu_dev,"CPU ingress stage");verify_selected_ingress(selected);held_ticket=std::move(selected.ticket);ticket_sha=selected.ticket_sha;
  ticket=dev_run::parse_ticket_versioned(std::move(selected.ticket_raw),dev_utc_ms(),true);
  dev_run::need(ticket.id==selected.intent.run&&ticket.selection==selected.intent.selection&&ticket.expires==selected.intent.expires&&ticket.config_sha==selected.intent.config_sha,"CPU intent/ticket binding");
  dev_run::need(selected.config.value!=INVALID_HANDLE_VALUE&&selected.config_sha==selected.intent.config_sha&&dev_sha(selected.config_raw)==selected.config_sha,"CPU held config hash");config_sha=selected.config_sha;config=dev_run::parse_config(std::move(selected.config_raw));selected.config.close();dev_run::need(ticket.id==config.id&&ticket.config_sha==config_sha,"CPU selected config binding");
 }
 size_t owned_bytes()const{return sizeof(*this)+config.owned_bytes()+ticket.raw.capacity()+ticket.selection.capacity()+ticket.id.capacity()+ticket.config_sha.capacity()+config_sha.capacity()+ticket_sha.capacity()+4096;}
 std::filesystem::path path(const char*suffix)const{auto leaf=config.leaf(suffix);return root/std::wstring(leaf.begin(),leaf.end());}
 void spend(){dev_run::need(ticket.expires>dev_utc_ms(),"ticket expired before claim");use.spend([&]{
   DevHandle claim(CreateFile2(path("claim").c_str(),GENERIC_WRITE,0,CREATE_NEW,nullptr));if(claim.value==INVALID_HANDLE_VALUE)throw std::runtime_error("run identity already used/claim unavailable");
   const auto bytes=ticket_sha+"\n";DWORD n=0;if(!WriteFile(claim.value,bytes.data(),DWORD(bytes.size()),&n,nullptr)||n!=bytes.size()||!FlushFileBuffers(claim.value))throw std::runtime_error("claim durable write refused");claim.close();
  },[&]{dev_run::need(ticket.expires>dev_utc_ms(),"ticket expired before consumption");FILE_DISPOSITION_INFO disposition{};disposition.DeleteFile=TRUE;if(!SetFileInformationByHandle(held_ticket.value,FileDispositionInfo,&disposition,sizeof(disposition)))throw std::runtime_error("held ticket consumption failed");held_ticket.close();
  });dev_run::need(use.may_start_model(),"ticket not consumed");
 }
};
}
