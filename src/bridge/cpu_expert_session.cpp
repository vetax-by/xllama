#include "xllama/cpu_expert_session.h"
#include "llama-context.h"
#include "llama-model.h"
#include "ggml-cpu.h"
#include <stdexcept>
#include <cstdio>
#include <climits>
namespace xllama {
void CpuExpertSession::latch_failure(const char*s) noexcept {
 if(!first_failure_[0])std::snprintf(first_failure_,sizeof(first_failure_),"%s",s?s:"expert failure");
}
bool CpuExpertSession::should_abort(void*p) noexcept{auto*o=static_cast<CpuExpertSession*>(p);return o->stop_.load(std::memory_order_acquire)||(o->turn_abort_&&o->turn_abort_->load());}
std::shared_ptr<CpuExpertSession> CpuExpertSession::open_if_requested(const CpuExpertOptIn*opt){
 if(!opt || !opt->enabled)return {}; // ordinary caller factory path remains untouched
 if(!opt->admitted_source || !opt->admit_budget)throw std::invalid_argument("explicit source and app budget admission required");
 if(!opt->context.n_ctx || opt->context.offload_kqv || opt->context.op_offload || opt->context.flash_attn_type!=LLAMA_FLASH_ATTN_TYPE_DISABLED || opt->context.n_threads<=0 || opt->context.n_threads_batch<=0)throw std::invalid_argument("explicit CPU/context policy required");
 auto out=std::shared_ptr<CpuExpertSession>(new CpuExpertSession());
 try{
  auto candidate=opt->admitted_source();
  if(!candidate.file || !candidate.file_anchor || !candidate.admission_anchor || !candidate.validate_binding || !candidate.control || !candidate.make_reader || !candidate.admission.bytes || candidate.admission.sha256.size()!=64 || candidate.admission.identity.empty())throw std::invalid_argument("incomplete Windows admission");
  out->input_=std::make_shared<ua50::uwp::WindowsInputCandidate>(std::move(candidate));
  auto input=out->input_;input->validate_binding(input->file);
  auto plan=ua50::inspect_expert_input(input->file,opt->slots);
  if(!plan.full_payload || plan.available_file_bytes!=input->admission.bytes || plan.cache_owned_bytes>opt->cache_ceiling || plan.stage_bytes>opt->stage_ceiling)throw std::invalid_argument("expert full payload/component budget refused");
  opt->admit_budget(plan,opt->context); // mandatory BEFORE model/context allocation
  ua50::OwnedExpertLoad owned;owned.slots=opt->slots;owned.cache_ceiling=opt->cache_ceiling;owned.stage_ceiling=opt->stage_ceiling;
  owned.input.file=input->file;owned.input.file_anchor=input->file_anchor;
  owned.input.contract=ua50::InputContract::windows_held_verified;
  owned.input.admission_anchor=input->admission_anchor;owned.input.validate_binding=input->validate_binding;
  owned.input.make_reader=[input](size_t stage)->std::shared_ptr<ua50::Reader>{return input->make_reader(stage);};
  out->model_.reset(ua50::load_owned_experts(owned,opt->model)); // common factory installs BEFORE load_tensors
  if(!out->model_)throw std::runtime_error("owned common model load refused");
  out->context_.reset(llama_init_from_model(out->model_.get(),opt->context));
  if(!out->context_)throw std::runtime_error("expert context allocation refused");
  auto sched=out->context_->get_sched();
  if(ggml_backend_sched_get_n_backends(sched)!=1 || !ggml_backend_is_cpu(ggml_backend_sched_get_backend(sched,0)))throw std::invalid_argument("one CPU backend required");
  ggml_backend_cpu_set_use_ref(ggml_backend_sched_get_backend(sched,0),true); // nonfused, not all-scalar
  llama_set_abort_callback(out->context_.get(),&CpuExpertSession::should_abort,out.get());
  out->state_.store(State::Ready,std::memory_order_release);return out;
 }catch(...){out->latch_failure("expert opening refused");out->state_.store(State::Invalid);out->close();throw;}
}
CpuExpertSession::~CpuExpertSession(){(void)close();}
void CpuExpertSession::begin_fresh_turn(){
 auto keep_alive=shared_from_this();State expected=State::Ready;
 if(!state_.compare_exchange_strong(expected,State::Decoding))throw std::logic_error("invalid expert turn owner");
 try{
  if(stop_.load())throw std::runtime_error("expert owner stopped");
  llama_synchronize(context_.get());llama_memory_clear(llama_get_memory(context_.get()),true);
  state_.store(State::Ready,std::memory_order_release);
 }catch(...){latch_failure("fresh turn reset failed");state_.store(State::Invalid);close();throw;}
}
int CpuExpertSession::decode(llama_batch batch,std::atomic<bool>* turn_abort){
 auto keep_alive=shared_from_this(); // last external owner cannot destroy an active decode
 State expected=State::Ready;
 if(!state_.compare_exchange_strong(expected,State::Decoding))throw std::logic_error("expert owner not ready; fresh admission required");
 try{
  if(stop_.load())throw std::runtime_error("expert owner stopped");
  turn_abort_=turn_abort;
  int rc=llama_decode(context_.get(),batch);llama_synchronize(context_.get());
  const bool cancelled=should_abort(this);turn_abort_=nullptr;
  auto model=dynamic_cast<llama_model_base*>(model_.get());auto provider=model?model->expert_provider():nullptr;
  if(!provider || provider->active_leases())throw std::runtime_error("expert graph lease barrier incomplete");
  if(provider->error()[0]){latch_failure(provider->error());throw std::runtime_error("expert provider terminal failure");}
  if(rc || cancelled){
   latch_failure(cancelled?"expert cancellation":"native decode nonzero; no continuation");state_.store(State::Invalid);close();return rc?rc:2;
  }
  state_.store(State::Ready,std::memory_order_release);return 0;
 }catch(...){turn_abort_=nullptr;latch_failure("expert decode exception; no continuation");state_.store(State::Invalid);close();throw;}
}
void CpuExpertSession::request_stop() noexcept{
 stop_.store(true,std::memory_order_release);
 auto input=std::atomic_load(&input_);if(input&&input->control)input->control->request_stop();
}
bool CpuExpertSession::close() noexcept{
 State s=state_.load();
 for(;;){if(s==State::Closed)return true;if(s==State::Decoding || s==State::Closing)return false;if(state_.compare_exchange_weak(s,State::Closing))break;}
 request_stop();
 if(context_)llama_synchronize(context_.get()); // wait graph workers before their buffers are released
 auto input=std::atomic_load(&input_);if(input&&input->control)input->control->drain_to_terminal(); // may block forever; never early-free pending I/O
 context_.reset();model_.reset(); // provider/Reader destroyed before admission/file anchors
 std::atomic_store(&input_,std::shared_ptr<ua50::uwp::WindowsInputCandidate>{});input.reset();
 state_.store(State::Closed,std::memory_order_release);return true;
}
const llama_model* CpuExpertSession::model_view() const{
 if(state()!=State::Ready || stop_.load())throw std::logic_error("invalid expert model view");
 return model_.get();
}
uint32_t CpuExpertSession::context_length() const{model_view();return llama_n_ctx(context_.get());}
uint32_t CpuExpertSession::batch_length() const{model_view();return llama_n_batch(context_.get());}
std::vector<llama_token> CpuExpertSession::tokenize(const std::string&s) const{
 const auto*v=llama_model_get_vocab(model_view());
 if(s.size()>INT32_MAX)throw std::length_error("prompt bytes overflow");
 int n=llama_tokenize(v,s.data(),static_cast<int32_t>(s.size()),nullptr,0,true,true);
 if(n==INT32_MIN || n>=0)throw std::runtime_error("empty or invalid tokenization");
 std::vector<llama_token> out(static_cast<size_t>(-n));
 int got=llama_tokenize(v,s.data(),static_cast<int32_t>(s.size()),out.data(),-n,true,true);
 if(got<0 || got>-n)throw std::runtime_error("tokenization changed");
 out.resize(got);return out;
}
std::string CpuExpertSession::piece(llama_token t,bool special) const{
 const auto*v=llama_model_get_vocab(model_view());char small[256];
 int n=llama_token_to_piece(v,t,small,sizeof(small),0,special);
 if(n>=0)return std::string(small,n);
 if(n==INT32_MIN)throw std::runtime_error("piece overflow");
 std::string out(static_cast<size_t>(-n),'\0');int got=llama_token_to_piece(v,t,out.data(),-n,0,special);
 if(got<0 || got>-n)throw std::runtime_error("piece changed");
 out.resize(got);return out;
}
llama_token CpuExpertSession::sample(llama_sampler*s){
 model_view();if(!s)throw std::invalid_argument("missing sampler");
 return llama_sampler_sample(s,context_.get(),-1); // raw context stays inside owning controller
}
void CpuExpertSession::invalidate(const char*why) noexcept{
 latch_failure(why);request_stop();State s=state();
 if(s==State::Ready || s==State::Opening)state_.store(State::Invalid);
 (void)close();
}
}
