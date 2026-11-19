#pragma once
#include "dev-run-input.h"
#include "xllama/gpu_run_config.h"
namespace xllama::bridge {
struct GpuProofInput {
 DevHandle held_ticket;gpu_run::Config config;gpu_run::Ticket ticket;dev_run::OneUse use;std::string config_sha,ticket_sha,instance_id;std::filesystem::path root;
 explicit GpuProofInput(std::filesystem::path base):root(std::move(base)){
  open_dev_file(held_ticket,root/L"ua68-gpu-proof-v1.ticket.json",true);auto traw=read_dev_file(held_ticket,4096);ticket_sha=dev_sha(traw);ticket=gpu_run::ticket(std::move(traw),dev_utc_ms());
  DevHandle cfg;const auto leaf="ua68-gpu-proof-v1."+ticket.id+".config.json";open_dev_file(cfg,root/std::wstring(leaf.begin(),leaf.end()),false);auto raw=read_dev_file(cfg,16384);config_sha=dev_sha(raw);config=gpu_run::parse(std::move(raw));cfg.close();dev_run::need(config.id==ticket.id&&config_sha==ticket.config_sha,"GPU ticket consumed bytes");dev_run::need(!config.bounded_diagnostics,"bounded capture requires native command instance");
 }
 GpuProofInput(std::filesystem::path base,SelectedIngress&& selected):root(std::move(base)){
  dev_run::need(selected.intent.stage==ingress_v2::Stage::proof,"GPU ingress stage");verify_selected_ingress(selected);held_ticket=std::move(selected.ticket);ticket_sha=selected.ticket_sha;
  ticket=gpu_run::ticket_versioned(std::move(selected.ticket_raw),dev_utc_ms(),true);
  dev_run::need(ticket.id==selected.intent.run&&ticket.selection==selected.intent.selection&&ticket.expires==selected.intent.expires&&ticket.config_sha==selected.intent.config_sha,"GPU intent/ticket binding");
  dev_run::need(selected.config.value!=INVALID_HANDLE_VALUE&&selected.config_sha==selected.intent.config_sha&&dev_sha(selected.config_raw)==selected.config_sha,"GPU held config hash");config_sha=selected.config_sha;config=gpu_run::parse(std::move(selected.config_raw));selected.config.close();dev_run::need(config.id==ticket.id&&config_sha==ticket.config_sha,"GPU selected config binding");if(config.bounded_diagnostics){dev_run::need(selected.native_command,"bounded native command required");const auto command=qualification::command(selected.command_raw);instance_id=command.instance;}
 }
 std::filesystem::path path(const char*suffix)const{auto leaf=config.leaf(suffix);return root/std::wstring(leaf.begin(),leaf.end());}
 size_t owned_bytes()const{return sizeof(*this)+config.owned_bytes()+ticket.raw.capacity()+ticket.selection.capacity()+ticket.id.capacity()+ticket.config_sha.capacity()+ticket.parser_charge+config_sha.capacity()+ticket_sha.capacity()+4096;}
 void spend(){dev_run::need(ticket.expires>dev_utc_ms(),"GPU expiry before claim");use.spend([&]{DevHandle c(CreateFile2(path("claim").c_str(),GENERIC_WRITE,0,CREATE_NEW,nullptr));if(c.value==INVALID_HANDLE_VALUE)throw std::runtime_error("GPU claim already used/unavailable");auto s=ticket_sha+"\n";DWORD n=0;if(!WriteFile(c.value,s.data(),DWORD(s.size()),&n,nullptr)||n!=s.size()||!FlushFileBuffers(c.value))throw std::runtime_error("GPU claim durable write");c.close();},[&]{dev_run::need(ticket.expires>dev_utc_ms(),"GPU expiry before consume");FILE_DISPOSITION_INFO d{};d.DeleteFile=TRUE;if(!SetFileInformationByHandle(held_ticket.value,FileDispositionInfo,&d,sizeof(d)))throw std::runtime_error("GPU held ticket disposition failed");held_ticket.close();});dev_run::need(use.may_start_model(),"GPU ticket not consumed");}
};
}
