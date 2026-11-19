#pragma once
#include "dev-run-input.h"
#include "protected-ui-v2.h"
#include <chrono>
#include <set>
#include <vector>

namespace xllama::bridge {
struct ProtectedUiScan {
 enum class State { none, selected, refuse } state=State::none;
 ingress_v2::UiIntent selected;
 std::string selected_sha;
 std::vector<DevHandle> held;
};
inline std::filesystem::path ui_path(const std::filesystem::path& root,const std::string& leaf){return root/std::filesystem::path(std::wstring(leaf.begin(),leaf.end()));}
inline bool ui_regular_exists(const std::filesystem::path& p){
 const DWORD a=GetFileAttributesW(p.c_str());
 if(a==INVALID_FILE_ATTRIBUTES){if(GetLastError()==ERROR_FILE_NOT_FOUND)return false;throw std::runtime_error("protected UI file presence unknown");}
 if(a&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE))throw std::runtime_error("protected UI file shape");return true;
}
inline ProtectedUiScan scan_protected_ui_v2(const std::filesystem::path& root,const std::string& package,uint64_t now){
 ProtectedUiScan out;size_t visited=0,bytes=0,active=0,intents=0;
 std::set<std::string> seen_intents,seen_claims,seen_closes,seen_reviews;
 const auto started=std::chrono::steady_clock::now();
 std::error_code ec;std::filesystem::directory_iterator it(root,ec),end;if(ec)throw std::runtime_error("protected UI directory unavailable");
 const std::string prefix="ua68-protected-ui-v2.",suffix=".intent.json";
 for(;it!=end;it.increment(ec)){
  if(ec)throw std::runtime_error("protected UI enumeration failure");
  ingress_v2::charge_scan_entry(visited);if(std::chrono::steady_clock::now()-started>std::chrono::milliseconds(ingress_v2::max_scan_ms))throw std::runtime_error("protected UI scan deadline");
  const auto name=it->path().filename().string();if(!ingress_v2::ascii_prefix_ci(name,prefix))continue;
  if(name.rfind(prefix,0)!=0)throw std::runtime_error("protected UI reserved name case variant");
  const auto classify=[&](const std::string& ending,std::set<std::string>& seen)->bool{
   if(name.size()!=prefix.size()+32+ending.size()||name.substr(name.size()-ending.size())!=ending)return false;
   const auto run=name.substr(prefix.size(),32);if(!dev_run::run_id(run)||!seen.insert(run).second)throw std::runtime_error("protected UI filename/duplicate");return true;
  };
  if(classify(".claim.json",seen_claims)||classify(".close.json",seen_closes)||classify(".review.json",seen_reviews))continue;
  if(!classify(suffix,seen_intents)||++intents>ingress_v2::ui_max_intents)throw std::runtime_error("protected UI filename/count");
  DevHandle intent;open_dev_file(intent,it->path(),false);const auto raw=read_dev_file(intent,ingress_v2::ui_intent_cap);ingress_v2::charge_scan_bytes(bytes,raw.size(),ingress_v2::ui_scan_bytes_cap);
  const auto ui=ingress_v2::parse_ui_intent(raw);if(name!=ingress_v2::ui_leaf(ui.run))throw std::runtime_error("protected UI filename binding");
  const auto sha=dev_sha(raw);
  const auto claim_path=ui_path(root,ingress_v2::ui_claim_leaf(ui.run));
  const auto close_path=ui_path(root,ingress_v2::ui_close_leaf(ui.run));
  const auto review_path=ui_path(root,ingress_v2::ui_review_leaf(ui.run));
  const bool claimed=ui_regular_exists(claim_path),closed=ui_regular_exists(close_path),reviewed=ui_regular_exists(review_path);
  std::string claim_sha;
  if(claimed){DevHandle h;open_dev_file(h,claim_path,false);const auto raw_claim=read_dev_file(h,ingress_v2::ui_marker_cap);ingress_v2::charge_scan_bytes(bytes,raw_claim.size(),ingress_v2::ui_scan_bytes_cap);const auto m=ingress_v2::parse_ui_marker(raw_claim,"xllama-protected-ui-claim-v2");if(m.run!=ui.run||m.intent_sha!=sha||m.package!=ui.package)throw std::runtime_error("protected UI claim binding");claim_sha=dev_sha(raw_claim);out.held.push_back(std::move(h));}
  if(closed){if(!claimed||!reviewed)throw std::runtime_error("protected UI close lacks claim/review");DevHandle h;open_dev_file(h,close_path,false);const auto raw_close=read_dev_file(h,ingress_v2::ui_marker_cap);ingress_v2::charge_scan_bytes(bytes,raw_close.size(),ingress_v2::ui_scan_bytes_cap);const auto c=ingress_v2::parse_ui_close(raw_close);if(c.marker.run!=ui.run||c.marker.intent_sha!=sha||c.marker.package!=ui.package||c.selection!=ui.selection||c.claim_sha!=claim_sha)throw std::runtime_error("protected UI close binding");out.held.push_back(std::move(h));DevHandle r;open_dev_file(r,review_path,false);const auto raw_review=read_dev_file(r,ingress_v2::ui_review_cap);ingress_v2::charge_scan_bytes(bytes,raw_review.size(),ingress_v2::ui_scan_bytes_cap);if(dev_sha(raw_review)!=c.post_ui_review_sha)throw std::runtime_error("protected UI review SHA/bound");ingress_v2::check_ui_review_receipt(raw_review,ui,claim_sha);out.held.push_back(std::move(r));}
  else if(reviewed)throw std::runtime_error("protected UI premature review without close");
  else if(claimed)out.state=ProtectedUiScan::State::refuse;
  else {
   ++active;if(ui.package!=package||!ingress_v2::ui_time_admitted(now,ui.expires))out.state=ProtectedUiScan::State::refuse;
   else {out.selected=ui;out.selected_sha=sha;}
  }
  out.held.push_back(std::move(intent));
 }
 if(ec)throw std::runtime_error("protected UI enumeration final error");
 if(std::chrono::steady_clock::now()-started>std::chrono::milliseconds(2000))throw std::runtime_error("protected UI final scan deadline");
 for(const auto& run:seen_claims)if(!seen_intents.count(run))throw std::runtime_error("protected UI orphan claim");
 for(const auto& run:seen_closes)if(!seen_intents.count(run))throw std::runtime_error("protected UI orphan close");
 for(const auto& run:seen_reviews)if(!seen_intents.count(run))throw std::runtime_error("protected UI orphan review");
 if(active>1)out.state=ProtectedUiScan::State::refuse;
 else if(active==1&&out.state!=ProtectedUiScan::State::refuse)out.state=ProtectedUiScan::State::selected;
 return out;
}
inline void commit_protected_ui_claim(const std::filesystem::path& root,const ProtectedUiScan& scan){
 if(scan.state!=ProtectedUiScan::State::selected)throw std::runtime_error("protected UI selection unavailable");
 if(!ingress_v2::ui_time_admitted(dev_utc_ms(),scan.selected.expires))throw std::runtime_error("protected UI expired before claim");
 const auto quote=first_answer::quote;
 const auto raw="{\"schema\":\"xllama-protected-ui-claim-v2\",\"run_id\":"+quote(scan.selected.run)+",\"intent_sha256\":"+quote(scan.selected_sha)+",\"full_package_name\":"+quote(scan.selected.package)+"}";
 if(raw.size()>1024)throw std::runtime_error("protected UI claim cap");
 DevHandle h(CreateFile2(ui_path(root,ingress_v2::ui_claim_leaf(scan.selected.run)).c_str(),GENERIC_WRITE,0,CREATE_NEW,nullptr));
 if(h.value==INVALID_HANDLE_VALUE)throw std::runtime_error("protected UI claim CREATE_NEW refused");DWORD n=0;
 if(!WriteFile(h.value,raw.data(),DWORD(raw.size()),&n,nullptr)||n!=raw.size()||!FlushFileBuffers(h.value))throw std::runtime_error("protected UI claim durable write refused");h.close();
}
}
