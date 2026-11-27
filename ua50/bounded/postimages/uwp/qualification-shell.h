#pragma once
// Developer qualification entry. No MainPage, Session, API listener, model
// downloader or legacy scanner is constructed by this shell.
#include "dev-run-input.h"
#include "xllama/bounded_publication.h"
#include "gpu-identity-input.h"
#include "gpu-proof-input.h"
#include "ingress-route-budget.h"
#include "xllama/qualification_profiles.h"
#include "xllama/qualification_receipts.h"
#include "xllama/qualification_release.h"
#include "xllama/qualification_release_window.h"
#include "xllama/generation_runner.h"
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Core.h>
#include <thread>

namespace xllama::bridge::qualification_shell {
using namespace qualification;
#if defined(XLLAMA_USE_LLAMA)
inline constexpr bool stage_handlers=true;
#else
inline constexpr bool stage_handlers=false;
#endif
inline std::filesystem::path path(const std::filesystem::path&r,const std::string&name){return r/std::filesystem::path(std::wstring(name.begin(),name.end()));}
inline std::string mailbox_leaf(const std::filesystem::path& path) {
 const auto name = path.filename().u8string();
 return std::string(name.begin(), name.end());
}
inline std::runtime_error mailbox_error(const char* operation, const std::filesystem::path& path, DWORD code) {
 return std::runtime_error(std::string(operation) + " win32_error=" + std::to_string(code) +
                           " file=" + mailbox_leaf(path));
}
inline bool present(const std::filesystem::path& path) {
 const auto attributes = GetFileAttributesW(path.c_str());
 if (attributes == INVALID_FILE_ATTRIBUTES) {
  const DWORD code = GetLastError();
  if (code == ERROR_FILE_NOT_FOUND) return false;
  throw mailbox_error("GetFileAttributesW", path, code);
 }
 dev_run::need(!(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)),
               "mailbox file shape");
 return true;
}
inline void absent(const std::filesystem::path&p){dev_run::need(!present(p),"spent namespace retained; use new run");}
inline std::string read(DevHandle&h,const std::filesystem::path&p,size_t cap,DevOpenContext context=DevOpenContext::ordinary){open_dev_file(h,p,false,context);auto raw=read_dev_file(h,cap);
 alignas(FILE_NAME_INFO) std::array<unsigned char,4096> storage{};
 dev_run::need(GetFileInformationByHandleEx(h.value,FileNameInfo,storage.data(),DWORD(storage.size())),"mailbox canonical held name unavailable");
 const auto*info=reinterpret_cast<const FILE_NAME_INFO*>(storage.data());dev_run::need(info->FileNameLength%sizeof(wchar_t)==0&&info->FileNameLength<=storage.size()-offsetof(FILE_NAME_INFO,FileName),"mailbox held name length");
 const std::wstring final_name(info->FileName,info->FileNameLength/sizeof(wchar_t));dev_run::need(std::filesystem::path(final_name).filename().native()==p.filename().native(),"mailbox noncanonical case/name");return raw;}
inline DevHandle write_new(const std::filesystem::path&p,const std::string&b,size_t cap,bool ticket=false){
 dev_run::need(!b.empty()&&b.size()<=cap,"native mailbox receipt cap");
 DevHandle h;
 h.value = CreateFile2(p.c_str(), GENERIC_READ|GENERIC_WRITE|(ticket?DELETE:0), 0, CREATE_NEW, nullptr);
 if (h.value == INVALID_HANDLE_VALUE) {
  const DWORD code = GetLastError();
  throw mailbox_error("CreateFile2 CREATE_NEW", p, code);
 }
 DWORD written = 0;
 if (!WriteFile(h.value, b.data(), DWORD(b.size()), &written, nullptr)) {
  const DWORD code = GetLastError();
  throw mailbox_error("WriteFile", p, code);
 }
 if (written != b.size()) {
  throw std::runtime_error("WriteFile short write expected=" + std::to_string(b.size()) +
                           " written=" + std::to_string(written) + " file=" + mailbox_leaf(p));
 }
 if (!FlushFileBuffers(h.value)) {
  const DWORD code = GetLastError();
  throw mailbox_error("FlushFileBuffers", p, code);
 }
 LARGE_INTEGER zero{};
 if (!SetFilePointerEx(h.value, zero, nullptr, FILE_BEGIN)) {
  const DWORD code = GetLastError();
  throw mailbox_error("SetFilePointerEx", p, code);
 }
 return h;
}
inline std::string random_hex(size_t n){dev_run::need(n<=32,"entropy cap");std::array<unsigned char,32>b{};dev_run::need(BCryptGenRandom(nullptr,b.data(),ULONG(n),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"native entropy unavailable");std::string out;for(size_t i=0;i<n;++i){out+="0123456789abcdef"[b[i]>>4];out+="0123456789abcdef"[b[i]&15];}return out;}
struct QuotaBracket {
 qualification::Session& session;uint64_t initial=0;
 static uint64_t sample(qualification::Session&s){using M=winrt::Windows::System::MemoryManager;auto&q=s.quota;q.available=0;
  q.first=M::AppMemoryUsageLimit();q.available|=1;q.usage=M::AppMemoryUsage();q.available|=2;const auto level=M::AppMemoryUsageLevel();q.level=uint32_t(level);q.available|=4;q.current=M::AppMemoryUsageLimit();q.available|=8;
  dev_run::need(ingress_v2::route_quota_admitted(q.first,q.current,q.usage,level==winrt::Windows::System::AppMemoryUsageLevel::Low||level==winrt::Windows::System::AppMemoryUsageLevel::Medium,retained_charge),"qualification sampled quota/headroom refused");return q.current;
 }
 explicit QuotaBracket(qualification::Session&s):session(s){session.quota.bracket_first=0;initial=sample(s);session.quota.bracket_first=initial;}
 void finish(){const auto after=sample(session);dev_run::need(initial==after,"qualification quota changed across bracket");}
};
struct Runtime:std::enable_shared_from_this<Runtime> {
 qualification::Session session;std::filesystem::path root;std::string actual_package,profile_raw,profile_sha,command_sha,completion_nonce;
 DevHandle held_prepare,held_profile;winrt::Windows::UI::Xaml::DispatcherTimer timer{nullptr};
 winrt::Windows::UI::Xaml::Controls::TextBlock label{nullptr};std::atomic<bool> started{false},work_done{false};qualification::ReleaseWindow release_window;
 static std::shared_ptr<Runtime>& owner(){static std::shared_ptr<Runtime> p;return p;}
 void status(const std::wstring&text){label.Text(winrt::hstring(text));}
 void receipt(const char*suffix,const std::string&status,const std::string&detail=""){
  session.last_clock=qualification_now();
  const auto b=qualification::control_body(session,GetCurrentProcessId(),status,command_sha,completion_nonce,qualification::run_guard.reasons.stop_observed_mask.load(),detail);
  auto h=write_new(path(root,leaf(session.selected.run,suffix)),b,receipt_cap);h.close();
 }
 // Preserve both errors if writing the one-shot refusal itself fails.
 std::string refusal_detail(const char* suffix, const char* state, const char* primary) {
  try { receipt(suffix, state, primary); }
  catch (const std::exception& secondary) {
   return bounded_detail(primary) + " | receipt: " + bounded_detail(secondary.what());
  }
  return bounded_detail(primary);
 }
 void show_failure(const std::string& detail) {
  status(L"Qualification stopped. " + std::wstring(winrt::to_hstring(detail).c_str()));
 }
 std::string worker_refusal_detail;
 // Completion/UNKNOWN is a control receipt. Existing native stage output,
 // trace, SHA/numerical/closure gates remain the acceptance authority.
 void start(){
  actual_package=winrt::to_string(winrt::Windows::ApplicationModel::Package::Current().Id().FullName());
  root=winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str();
  session.instance=random_hex(16);session.nonce=random_hex(32);session.selected.run=session.instance;session.selected.package=actual_package;
  const auto id=session.instance,nonce=session.nonce;session.instance.clear();
  Prepare p;p.run=id;p.package=actual_package;std::string raw;bool has_prepare=false;uint32_t pending_reason=io_unknown;
  try{
   if(present(path(root,prepare_leaf))){has_prepare=true;pending_reason=invalid_prepare;raw=read(held_prepare,path(root,prepare_leaf),prepare_cap);p=qualification::prepare(raw);}
   session.initialize(p,actual_package,id,nonce,raw.empty()?std::string(64,'0'):dev_sha(raw),qualification_now());
   if(!has_prepare){session.prework_mask=absent_prepare;session.prework_detail="no prepare; immutable unselected manual";}
   if(session.state==Phase::ready){
    pending_reason=invalid_prepare;dev_run::need(stage_handlers,"stage handlers unavailable in this build");
    pending_reason=quota_refused;QuotaBracket quota(session);
    pending_reason=profile_refused;profile_raw=read(held_profile,path(root,leaf(p.run,"profile.json")),profile_cap);profile_sha=dev_sha(profile_raw);
    dev_run::need(profile_sha==p.profile_sha,"prepare/profile SHA mismatch");validate_profile(p.work,profile_raw,p.run);if(p.work==Stage::proof){const auto parsed=gpu_run::parse(profile_raw);if(parsed.stage==gpu_proof::Stage::Model2)dev_run::need(parsed.primitive_acceptance_sha==p.prerequisite_sha,"model2 prerequisite/profile mismatch");}pending_reason=quota_refused;quota.finish();
    pending_reason=retained_namespace;for(const char*s:{"claim.json","receipt.json","refusal.json","command.json","abandon.json","release-window-query.json","release-window.json","release-window-refusal.json","release.json","closure.json","review.json","release-claim.json","release-refusal.json"})absent(path(root,leaf(p.run,s)));
   }
   if(session.state==Phase::ready){pending_reason=0;dev_run::need(session.poll(qualification_now()),"readiness expired during native setup");}
  }catch(const std::exception&e){
   // Preserve known prework masks, including native clock/wait failures.
   // Rejected correlation cannot retain diagnostic stage/expiry/authority.
   session.instance=id;session.nonce=nonce;session.actual_package=actual_package;session.requested_package=p.package;session.prepare_sha=raw.empty()?std::string(64,'0'):dev_sha(raw);
   session.manual_fallback(session.prework_mask?0:(pending_reason?pending_reason:io_unknown),e.what());
  }
  try{const auto b=ready_body(session,GetCurrentProcessId(),stage_handlers);auto h=write_new(path(root,leaf(session.selected.run,"ready.json")),b,receipt_cap);h.close();session.ready_sha=dev_sha(b);}
  catch(const std::exception& e){session.note(receipt_unavailable,e.what());session.state=Phase::manual;show_failure(session.prework_detail);return;}
  if(session.state!=Phase::ready){status(L"Qualification shell — immutable manual mode. No model or diagnostics.");return;}
  status(L"Qualification shell — diagnostic READY. Waiting for one exact instance command.");
  timer=winrt::Windows::UI::Xaml::DispatcherTimer();timer.Interval(std::chrono::milliseconds(poll_ms));
  std::weak_ptr<Runtime> weak=shared_from_this();timer.Tick([weak](auto const&,auto const&){if(auto self=weak.lock())self->tick();});timer.Start();
 }
 void tick(){
  try{
   if(qualification::run_guard.abort.load()){session.refuse();throw std::runtime_error("qualification instance hidden/suspended");}
   if(!session.poll(qualification_now())){timer.Stop();status(L"Readiness expired — no work. Use a new selection.");show_failure(refusal_detail("refusal.json","READY_EXPIRED","Readiness expired"));return;}
   if(present(path(root,leaf(session.selected.run,"abandon.json")))){
    DevHandle h;const auto raw=read(h,path(root,leaf(session.selected.run,"abandon.json")),command_cap);const auto j=dev_run::Parser(raw).parse(command_cap);
    j.keys({"schema","run_id","instance_id","ready_sha256","selection_receipt_sha256"});j.at("schema").eq("xllama-qualification-abandon-v1");j.at("run_id").eq(session.selected.run.c_str());j.at("instance_id").eq(session.instance.c_str());j.at("ready_sha256").eq(session.ready_sha.c_str());j.at("selection_receipt_sha256").eq(session.selected.selection_sha.c_str());
    session.abandon();timer.Stop();receipt("receipt.json","ABANDONED_NO_ACCEPTANCE");status(L"Selection administratively abandoned. No native closure or replay granted.");return;
   }
   const auto cp=path(root,leaf(session.selected.run,"command.json"));if(!present(cp))return;
   // First visible command is acquired ONCE. Any sharing/partial/parse/SHA
   // error spends this instance; no poll retry of an uncertain publication.
   timer.Stop();DevHandle held;const auto raw=read(held,cp,command_cap,DevOpenContext::first_command);const auto c=qualification::command(raw);session.validate(c,qualification_now());command_sha=dev_sha(raw);
   DevHandle config;const auto config_raw=read(config,path(root,leaf(c.run,"config.json")),profile_cap);dev_run::need(dev_sha(config_raw)==c.config_sha,"command final CONFIG SHA");
   DevHandle permit;std::string permit_raw;
   if(c.work==Stage::generation){
    const auto template_request=generation::parse(profile_raw),final_request=generation::parse(config_raw);dev_run::need(same_generation_profile(template_request,final_request),"generation profile changed");
    const auto permit_name="ua68-generation-v1."+c.run+".permit.json";permit_raw=read(permit,path(root,permit_name),generation::permit_cap);
    dev_run::need(c.permit_sha==dev_sha(permit_raw)&&final_request.permit_sha==c.permit_sha,"generation permit SHA binding");const auto p=generation::permit(permit_raw,dev_utc_ms(),actual_package);dev_run::need(p.expires==c.expires&&p.artifact==c.artifact_sha&&p.model2==session.selected.prerequisite_sha,"generation native expiry/artifact/prerequisite binding");
   }else dev_run::need(c.config_sha==profile_sha&&config_raw==profile_raw&&c.permit_sha==std::string(64,'0'),"command exact profile/config");
   qualification::validate_final(session.selected,c,profile_raw,config_raw,permit_raw,dev_utc_ms());
   QuotaBracket quota(session);quota.finish();
   // Worker ownership is retained by shared_ptr through return/UNKNOWN. No
   // MainPage callback, second CoreApplication::Run, or automatic app exit.
   auto self=shared_from_this();const auto clock=qualification_now();
   session.validate(c,clock);started.store(true);status(L"Selected command accepted for admission. Work remains gated.");
   std::thread([self,c,raw,config_raw,clock,held=std::move(held),config=std::move(config),permit=std::move(permit)]()mutable{
    qualification::worker_boundary([]{winrt::init_apartment();},[&]{
     self->session.execute(c,qualification_now(),[&]{self->receipt("claim.json","CLAIMED_BEFORE_WORK");},[&]{
      qualification::run_guard.start(self->session.clock);
      self->dispatch(c,raw,config_raw,std::move(held),std::move(config));
     });self->completion_nonce=random_hex(32);self->receipt("receipt.json","WORK_RETURNED_NOT_ACCEPTED");
    },[&](const char*error){if(!self->session.claim_attempted)self->session.note(worker_setup,error);self->session.refuse();self->worker_refusal_detail=self->refusal_detail("refusal.json","UNKNOWN_NO_RETRY",error);});
    self->work_done.store(true,std::memory_order_release);
    try{self->label.Dispatcher().RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,[self]{self->begin_release_wait();});}catch(...){}
    // No further work command is accepted in this process, even after return.
   }).detach();
  }catch(const std::exception&e){session.refuse();if(timer)timer.Stop();show_failure(refusal_detail("refusal.json","UNKNOWN_NO_RETRY",e.what()));}
 }
 void release_failure(const char*detail,const qualification::ReleaseObservation*observation=nullptr){
  if(timer)timer.Stop();
  // Structured release-local stop is critical and never hidden in bounded detail.
  if(observation)try{const auto raw=qualification::release_stop_body(session,GetCurrentProcessId(),*observation,command_sha,completion_nonce);auto h=write_new(path(root,leaf(session.selected.run,"release-window-refusal.json")),raw,release_window_cap);h.close();}catch(...){}
  show_failure(refusal_detail("release-refusal.json","RELEASE_UNKNOWN_NO_RETRY",detail));
 }
 void begin_release_wait(){
  if(work_done.load(std::memory_order_acquire) && !worker_refusal_detail.empty()){show_failure(worker_refusal_detail);return;}
  if(!capture::publication_resources_closed()||!work_done.load(std::memory_order_acquire)||session.state!=Phase::returned){status(L"Worker unavailable/UNKNOWN — preserve evidence; no automatic exit.");return;}
  status(L"Work returned. Collect native evidence; explicit reviewed release required for process exit.");
  try{release_window.start(qualification_now());timer=winrt::Windows::UI::Xaml::DispatcherTimer();timer.Interval(std::chrono::milliseconds(1000));
  std::weak_ptr<Runtime> weak=shared_from_this();timer.Tick([weak](auto const&,auto const&){if(auto self=weak.lock())self->release_tick();});timer.Start();}
  catch(const qualification::ReleaseStop&e){release_failure(e.what(),&e.observation);}catch(const std::exception&e){release_failure(e.what());}
 }
 void release_tick(){
  try{
   dev_run::need(capture::publication_resources_closed(),"bounded capture ownership UNKNOWN");
   release_window.tick(qualification_now(),qualification::run_guard.abort.load());
   const auto query_path=path(root,leaf(session.selected.run,"release-window-query.json"));
   if(!release_window.query_seen&&present(query_path)){
    // First visible query spends this run's ONE timing request even if partial.
    release_window.acquire_query();DevHandle qh,control;
    const auto qr=read(qh,query_path,release_query_cap);const auto q=qualification::release_query(qr);
    const auto cr=read(control,path(root,leaf(session.selected.run,"receipt.json")),receipt_cap);
    qualification::validate_release_query(q,session,command_sha,dev_sha(cr),completion_nonce);
    const auto response=release_window.response(q,session,GetCurrentProcessId(),dev_sha(qr),qualification_now(),qualification::run_guard.abort.load());
    auto published=write_new(path(root,leaf(session.selected.run,"release-window.json")),response,release_window_cap);published.close();
    release_window.published(dev_sha(response),q.challenge);
    release_window.check(qualification_now(),qualification::run_guard.abort.load());
   }
   const auto selected=path(root,leaf(session.selected.run,"release.json"));if(!present(selected))return;timer.Stop();
   DevHandle rh,ch,review,receipt_file,output_file;const auto raw=read(rh,selected,command_cap);const auto timed=qualification::timed_release(raw);const auto&r=timed.value;qualification::validate_timed_release(timed,release_window,qualification_now(),qualification::run_guard.abort.load());dev_run::need(r.run==session.selected.run&&r.instance==session.instance&&r.selection==session.selected.selection&&r.package==actual_package,"foreign release before related reads");
   const auto cr=read(ch,path(root,leaf(r.run,"closure.json")),closure_cap);dev_run::need(dev_sha(cr)==r.closure_sha,"release closure SHA");const auto closure=qualification::closure(cr);
   const auto reviewed=read(review,path(root,leaf(r.run,"review.json")),review_cap);dev_run::need(dev_sha(reviewed)==closure.review_sha,"release raw independent review SHA");
   const auto rr=read(receipt_file,path(root,leaf(r.run,"receipt.json")),receipt_cap);
   const auto science=session.selected.work==Stage::generation?"ua68-generation-v1."+r.run+".output.json":ingress_v2::result_prefix(session.selected.work==Stage::identity?ingress_v2::Stage::identity:session.selected.work==Stage::proof?ingress_v2::Stage::proof:ingress_v2::Stage::cpu_dev,r.run)+"output.json";
   const auto output=read(output_file,path(root,science),session.selected.work==Stage::identity?gpu_identity::result_cap:262144);
   qualification::validate_release(session,r,closure,dev_sha(rr),dev_sha(output),command_sha,completion_nonce);
   qualification::validate_resource_review(reviewed,session,closure,GetCurrentProcessId());
   // These staged authority/chronology assertions are explicitly external.
   // No new normal/closed result or acceptance is emitted by this exit gate.
   qualification::validate_timed_release(timed,release_window,qualification_now(),qualification::run_guard.abort.load());
   receipt("release-claim.json","EXIT_AUTHORIZED_BY_EXTERNAL_REVIEW");
   // Release claim persistence may block. Expiry here retains a claimed shell,
   // never grants Exit/closure, never refreshes the original release timer.
   qualification::validate_timed_release(timed,release_window,qualification_now(),qualification::run_guard.abort.load());
   dev_run::need(capture::publication_resources_closed(),"bounded capture ownership UNKNOWN before Exit");
   timer.Stop();winrt::Windows::UI::Xaml::Application::Current().Exit();
  }catch(const qualification::ReleaseStop&e){release_failure(e.what(),&e.observation);}catch(const std::exception&e){release_failure(e.what());}
 }
 void dispatch(const Command&c,const std::string&raw,const std::string&config_raw,DevHandle command_file,DevHandle config_file){
  qualification_guard_check();
#if !defined(XLLAMA_USE_LLAMA)
  throw std::runtime_error("stage handlers unavailable");
#else
  if(c.work==Stage::generation){
   for(const char*s:{"claim","output.json","config.json","trace.bin","trace.binding"})absent(path(root,"ua68-generation-v1."+c.run+"."+s));
   // Existing synchronous runner still requires its held, unconsumed permit
   // and original primitive/model2/artifact prerequisite fields. No LAN.
   const auto result=run_configured_generation(config_raw);dev_run::need(result.http_status==200||result.http_status==422,"generation admission/unknown refusal");return;
  }
  const auto st=c.work==Stage::identity?ingress_v2::Stage::identity:c.work==Stage::proof?ingress_v2::Stage::proof:ingress_v2::Stage::cpu_dev;
  const auto result_prefix=ingress_v2::result_prefix(st,c.run);
  for(const char*s:{"claim","output.json","terminal-v2.json","attempt-v2.json","reason-v2.json","trace.bin","diagnostics.json"})absent(path(root,result_prefix+s));
  SelectedIngress selected;selected.native_command=true;selected.command_raw=raw;selected.command_sha=dev_sha(raw);selected.command_file=std::move(command_file);
  auto&i=selected.intent;i.stage=st;i.run=c.run;i.selection=c.selection;i.package=c.package;i.expires=c.expires;i.config_sha=st==ingress_v2::Stage::identity?"":c.config_sha;
  selected.config=std::move(config_file);selected.config_raw=config_raw;selected.config_sha=c.config_sha;
  selected.ticket_raw=qualification::issued_ticket(c);
  selected.ticket_sha=dev_sha(selected.ticket_raw);i.ticket_sha=selected.ticket_sha;
  // Native CREATE_NEW ticket is consumed only by the unchanged stage OneUse
  // claim/consume boundary. Mailbox command/profile/history are preserved.
  selected.ticket=write_new(path(root,ingress_v2::ticket_leaf(st,c.run)),selected.ticket_raw,4096,true);
  const auto intent=i;commit_ingress_attempt(path(root,result_prefix+"attempt-v2.json"),intent);install_selected_ingress(std::move(selected));
  if(st==ingress_v2::Stage::identity)run_gpu_capability_identity();else if(st==ingress_v2::Stage::proof)run_expert_gpu_proof_configured();else run_cpu_expert_configured();
  // Existing finalizer can close exact unclaimed native ticket refusals. It
  // never manufactures normal or closes a claimed/orphan native resource.
  (void)finalize_selected_preclaim_refusal(root,intent);
#endif
 }
};
inline void show(){
 using namespace winrt::Windows::UI::Xaml;using namespace winrt::Windows::UI::Xaml::Controls;
 if(Runtime::owner())return;auto runtime=std::make_shared<Runtime>();Runtime::owner()=runtime;
 runtime->label=TextBlock();runtime->label.FontSize(24);runtime->label.TextWrapping(TextWrapping::Wrap);runtime->label.Margin(Thickness{48,48,48,48});runtime->label.Text(L"Qualification shell — safe manual mode. No model loaded.");
 Window::Current().Content(runtime->label);Window::Current().Activate();
 // A suspension/visibility transition can only request stop. It never marks
 // native drain/closure, resets timers, or resumes a command.
 winrt::Windows::UI::Xaml::Application::Current().Suspending([](auto const&,auto const&){qualification::run_guard.stop();});
 Window::Current().VisibilityChanged([](auto const&,auto const&e){if(!e.Visible())qualification::run_guard.stop();});
 try{runtime->start();}catch(...){runtime->session.state=Phase::manual;runtime->status(L"Qualification shell — manual. Native readiness unavailable.");}
}
}
