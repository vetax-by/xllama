#pragma once
#include "bounded_graph_capture.h"
#include "cpu_expert_session.h"
#include "gpu_numeric_proof.h"
#include "gpu_proof_trace.h"
#include "gpu_layer_witness.h"
#include "gpu_vector_witness.h"
#include "gpu_sweep_witness.h"
#include "gpu_selected_vector_witness.h"
#include "gpu_cross_arm_witness.h"
#include "gpu_graph_witness.h"
#include "gpu_optional_observer.h"
#include "ggml-backend.h"
#include "ggml.h"
#include "first_answer_template.h"
#include <atomic>
#include <memory>
namespace xllama::gpu_proof {
// One coordinator; request_stop may be concurrent. All native calls stay in the actual owning session.
class Controller {
 std::shared_ptr<CpuExpertSession> current_;
 std::shared_ptr<ua50::ExpertGpu> gpu_;std::atomic<bool> stopped_{false};bool used_=false;
 std::shared_ptr<Proof> proof_;std::shared_ptr<Trace> trace_;std::shared_ptr<LayerWitness> witness_;
 std::shared_ptr<VectorWitness> vectors_;std::shared_ptr<SweepWitness> sweep_;std::shared_ptr<SelectedVectorWitness> selected_vectors_;
 std::shared_ptr<capture::GraphCapture> bounded_;
 std::shared_ptr<CrossArmWitness> cross_;std::shared_ptr<GraphWitness> graph_;
 struct EvalMux {std::shared_ptr<LayerWitness> legacy;std::shared_ptr<SweepWitness> sweep;std::shared_ptr<GraphWitness> graph;};std::shared_ptr<EvalMux> eval_;
 std::array<llama_token,128> prompt_{};size_t prompt_count_=0;uint32_t cpu_rows_=0,gpu_rows_=0;llama_token t0_=-1,t1_=-1,g0_=-1,g1_=-1;
 std::function<void(const char*)> progress_;
 void gate(const char*p){if(stopped_.load())throw std::runtime_error("hybrid proof stopped");if(!progress_)throw std::runtime_error("proof watchdog/phase callback absent");progress_(p);}
 void attach(std::shared_ptr<CpuExpertSession> s){if(std::atomic_load(&current_))throw std::logic_error("two simultaneous proof contexts");std::atomic_store(&current_,std::move(s));}
 void close_current(){auto s=std::atomic_load(&current_);if(!s)return;if(!s->close())throw std::logic_error("proof owner not terminal");if(s->first_failure_after_drain()[0])throw std::runtime_error(s->first_failure_after_drain());std::atomic_store(&current_,std::shared_ptr<CpuExpertSession>{});}
 static bool layer_eval(ggml_tensor*t,bool ask,void*user)noexcept{
  auto*m=static_cast<EvalMux*>(user);if(!m||!m->legacy)return false;
  try{
   if(!t){m->legacy->callback_failure();if(m->sweep)m->sweep->callback_failure();if(m->graph)m->graph->unavailable("NULL_TENSOR");return false;}
   size_t name_length=0;while(name_length<GGML_MAX_NAME&&t->name[name_length])++name_length;
   if(name_length==GGML_MAX_NAME){m->legacy->callback_failure();if(m->sweep)m->sweep->callback_failure();if(m->graph)m->graph->unavailable("NAME_EXTENT");return false;}
   const auto name=std::string_view(t->name,name_length);
   if(ask){
    const bool legacy=m->legacy->ask(name);
    bool sweep=false;
    if(m->sweep)optional_observe([&]{sweep=m->sweep->ask(name);},[&]{m->sweep->callback_failure();});
    bool graph=false;
    if(m->graph)optional_observe([&]{graph=m->graph->ask(name);},[&]{m->graph->unavailable("ASK_FAILURE");});
    return legacy||sweep||graph;
   }
   LayerWitness::Shape shape;shape.f32=t->type==GGML_TYPE_F32;shape.i32=t->type==GGML_TYPE_I32;shape.type=uint32_t(t->type);shape.op=uint32_t(t->op);shape.contiguous=ggml_is_contiguous(t);shape.buffer=(t->view_src?t->view_src->buffer:t->buffer)!=nullptr;shape.data=t->data!=nullptr;shape.bytes=ggml_nbytes(t);
   for(int i=0;i<4;++i){shape.ne[i]=uint64_t(t->ne[i]);shape.nb[i]=t->nb[i];}
   const bool legacy=m->legacy->ask(name);
   const auto reader=[t](void*dst,size_t bytes){ggml_backend_tensor_get(t,dst,0,bytes);return true;};
   const bool accepted=legacy?m->legacy->observe(name,shape,reader):true;
   if(m->sweep)optional_observe([&]{if(m->sweep->ask(name))m->sweep->observe(name,shape,reader);},
                                [&]{m->sweep->callback_failure();});
   if(m->graph)optional_observe([&]{if(m->graph->selected(name))m->graph->observe(name,shape,reader);},
                                [&]{m->graph->unavailable("OBSERVE_FAILURE");});
   return accepted;
  }catch(...){m->legacy->callback_failure();if(m->sweep)m->sweep->callback_failure();if(m->graph)m->graph->unavailable("CALLBACK_FAILURE");return false;}
 }
 CpuExpertOptIn options(const CpuExpertOptIn&base,std::shared_ptr<ua50::ExpertGpu>gpu){
  if(base.slots!=2||base.cache_ceiling!=(16u<<20)||base.context.n_ctx!=512||base.context.n_batch!=1||base.context.n_ubatch!=1||base.context.n_threads!=1||base.context.n_threads_batch!=1||base.model.n_gpu_layers!=0||base.model.main_gpu!=-1||base.context.op_offload||base.context.offload_kqv)throw std::invalid_argument("proof fixed requested envelope; no clamp");
  auto opt=base;
  if(bounded_){bounded_->context(gpu?2:1);opt.dispatch_observer=bounded_->dispatch_observer();opt.context.cb_capture=&capture::GraphCapture::callback;opt.context.cb_capture_user_data=bounded_.get();opt.capture_callback_owner=bounded_;if(bounded_->worker_enabled()){opt.worker_observer=&capture::GraphCapture::worker_callback;opt.worker_observer_data=bounded_.get();}}
  opt.hybrid_gpu=std::move(gpu);opt.numerical_proof_owned_bytes=Proof::required_owned_bytes();
  opt.slots=2;opt.cache_ceiling=16u<<20;opt.plan_profile=CpuExpertOptIn::PlanProfile::FinishedAnswer512;
  // Trace and controller allocations are additional to the already separate proof4MiB reserve.
  opt.instrumentation_owned_bytes=ua50::add(opt.instrumentation_owned_bytes,ua50::add(Trace::required_owned_bytes(),sizeof(Controller)+4096));
  opt.route_trace=[trace=trace_](uint32_t r,uint64_t e,uint64_t off,uint64_t len){trace->key(r,e,off,len);};
  opt.hybrid_catalog=[trace=trace_](uint32_t id,const ua50::Tensor&t,const char*n){trace->role(id,t,n);};
  if(opt.hybrid_gpu){if(selected_vectors_)opt.vector_observer=selected_vectors_;else if(vectors_)opt.vector_observer=vectors_;}
  if(cross_)opt.cross_arm_observer=cross_;
  if(witness_){if(!eval_)eval_=std::make_shared<EvalMux>(EvalMux{witness_,sweep_,graph_});opt.context.cb_eval=&layer_eval;opt.context.cb_eval_user_data=eval_.get();opt.eval_callback_owner=eval_;}
  return opt;
 }
 void decode(std::shared_ptr<CpuExpertSession>s,llama_token token,uint32_t position,uint32_t phase,bool prefill_token=false){
  gate("proof-before-decode");
  if(bounded_){auto io=bounded_->dispatch_observer();if(io&&io->io_timing_enabled())io->io_phase(prefill_token);}
  if(bounded_)bounded_->frame(phase,position,token);if(witness_)witness_->begin(phase,position);if(sweep_)sweep_->begin(phase,position);if(graph_)graph_->begin(phase,position,token);if(vectors_)vectors_->begin(phase,position);if(selected_vectors_)selected_vectors_->begin(phase,position);if(cross_||bounded_)s->diagnostic_frame(phase,position,token);trace_->begin(phase,position,phase==2?gpu_->counts():ua50::GpuCounts{});
  // Every proof frame must execute the final MoE layer, including prefill tokens.
  int32_t nseq=1;llama_seq_id sequence=0;llama_seq_id*sequence_ptr=&sequence;int8_t logits=1;llama_pos pos=llama_pos(position);
  llama_batch b{};b.n_tokens=1;b.token=&token;b.pos=&pos;b.n_seq_id=&nseq;b.seq_id=&sequence_ptr;b.logits=&logits;
  const int decode_status=s->decode(b);if(witness_)witness_->finish_or_throw(decode_status);if(sweep_)sweep_->finish(decode_status);if(vectors_)vectors_->finish();if(selected_vectors_)selected_vectors_->finish(phase,position);
  if(decode_status!=0)throw std::runtime_error("proof native decode failed; no continuation");
  trace_->end(phase==2?gpu_->counts():ua50::GpuCounts{});gate("proof-after-decode");
 }
 static llama_token greedy(const float*row){llama_token best=0;for(size_t i=0;i<vocab;++i){if(!std::isfinite(row[i]))throw std::runtime_error("nonfinite native row");if(row[i]>row[size_t(best)])best=llama_token(i);}return best;}
 void prefill(std::shared_ptr<CpuExpertSession>s,uint32_t phase){s->begin_fresh_turn();for(size_t i=0;i<prompt_count_;++i)decode(s,prompt_[i],uint32_t(i),phase,true);}
public:
 Controller(std::shared_ptr<Proof>proof,std::shared_ptr<Trace>trace,std::shared_ptr<ua50::ExpertGpu>gpu,std::function<void(const char*)> progress,std::shared_ptr<VectorWitness> vectors={},std::shared_ptr<SweepWitness>sweep={},std::shared_ptr<SelectedVectorWitness>selected={},std::shared_ptr<CrossArmWitness>cross={},std::shared_ptr<GraphWitness>graph={},std::shared_ptr<capture::GraphCapture>bounded={}):gpu_(std::move(gpu)),proof_(std::move(proof)),trace_(std::move(trace)),vectors_(std::move(vectors)),sweep_(std::move(sweep)),selected_vectors_(std::move(selected)),cross_(std::move(cross)),graph_(std::move(graph)),progress_(std::move(progress)),bounded_(std::move(bounded)){
  if(!gpu_||!proof_||!trace_||!progress_)throw std::invalid_argument("proof complete ownership required");
 }
 ~Controller(){request_stop();drain();}
 void request_stop()noexcept{stopped_.store(true);gpu_->request_stop();auto s=std::atomic_load(&current_);if(s)s->request_stop();}
 bool terminal()const{return !std::atomic_load(&current_)&&gpu_->terminal();}
 void drain()noexcept{auto s=std::atomic_load(&current_);if(s){s->request_stop();while(!s->close())std::this_thread::yield();std::atomic_store(&current_,std::shared_ptr<CpuExpertSession>{});}gpu_->drain();}
 // No alternate input identities or hidden re-tokenization accepted.
 void model2(const CpuExpertOptIn&base){
  if(used_)throw std::logic_error("proof controller one-use");used_=true;
  try{
   witness_=std::make_shared<LayerWitness>();
   gate("cpu-reference-open");auto cpu_options=options(base,{});auto cpu=CpuExpertSession::open_if_requested(&cpu_options);if(!cpu)throw std::runtime_error("CPU reference unavailable");attach(cpu);
   const auto*v=llama_model_get_vocab(cpu->model_view());if(!llama_vocab_get_add_bos(v))throw std::runtime_error("proof BOS policy");
   auto rendered=first_answer::render(llama_model_chat_template(cpu->model_view(),nullptr),true,llama_vocab_bos(v));
   auto ids=cpu->tokenize(rendered);if(ids.empty()||ids.size()>128||ids.size()+2>512||ids[0]!=llama_vocab_bos(v)||std::count(ids.begin(),ids.end(),llama_vocab_bos(v))!=1||llama_vocab_n_tokens(v)!=128000)throw std::runtime_error("proof prompt/state admission");prompt_count_=ids.size();witness_->configure(uint32_t(prompt_count_));if(sweep_)sweep_->configure(uint32_t(prompt_count_));if(graph_)graph_->configure(uint32_t(prompt_count_));if(cross_)cross_->configure(uint32_t(prompt_count_));std::copy(ids.begin(),ids.end(),prompt_.begin());
   std::unique_ptr<llama_sampler,decltype(&llama_sampler_free)> sampler(llama_sampler_init_greedy(),llama_sampler_free);if(!sampler)throw std::bad_alloc();
   prefill(cpu,1);cpu->capture_logits(proof_->reference(0),vocab);++cpu_rows_;t0_=cpu->sample(sampler.get());if(t0_!=greedy(proof_->reference(0)))throw std::runtime_error("native greedy/reference argmax mismatch");
   if(llama_vocab_is_eog(v,t0_))throw std::runtime_error("EARLY_TERMINATOR reference coverage1of2");
   decode(cpu,t0_,uint32_t(prompt_count_),1);cpu->capture_logits(proof_->reference(1),vocab);++cpu_rows_;t1_=cpu->sample(sampler.get());if(t1_!=greedy(proof_->reference(1)))throw std::runtime_error("native second greedy mismatch");
   close_current();cpu.reset(); // Actual model/context destruction BEFORE hybrid creation, retained rows only.
   gate("hybrid-open");auto hybrid_options=options(base,gpu_);auto hybrid=CpuExpertSession::open_if_requested(&hybrid_options);if(!hybrid)throw std::runtime_error("hybrid unavailable");attach(hybrid);
   const auto*hv=llama_model_get_vocab(hybrid->model_view());auto hrendered=first_answer::render(llama_model_chat_template(hybrid->model_view(),nullptr),llama_vocab_get_add_bos(hv),llama_vocab_bos(hv));auto hids=hybrid->tokenize(hrendered);
   if(hids!=ids||hybrid->context_length()!=512)throw std::runtime_error("reference/hybrid logical input state mismatch");
   prefill(hybrid,2);hybrid->capture_logits(proof_->working(),vocab);++gpu_rows_;proof_->compare(4,0,0,0,0,uint32_t(prompt_count_-1),proof_->reference(0),proof_->working(),vocab);g0_=hybrid->sample(sampler.get());proof_->greedy(g0_==t0_&&greedy(proof_->working())==t0_);
   decode(hybrid,t0_,uint32_t(prompt_count_),2);hybrid->capture_logits(proof_->working(),vocab);++gpu_rows_;proof_->compare(4,1,0,0,0,uint32_t(prompt_count_),proof_->reference(1),proof_->working(),vocab);g1_=hybrid->sample(sampler.get());proof_->greedy(g1_==t1_&&greedy(proof_->working())==t1_);
   if(!witness_->complete())throw std::runtime_error("model2 layer witness incomplete");
   gate("model-proof-compute-complete"); // final owner retained through postSHA+monitor join
  }catch(...){request_stop();drain();throw;}
 }
 void primitive16(const CpuExpertOptIn&base){
  if(used_)throw std::logic_error("proof controller one-use");used_=true;
  try{
   gate("primitive-open");auto opt=options(base,{});auto owner=CpuExpertSession::open_if_requested(&opt);if(!owner)throw std::runtime_error("primitive owner unavailable");attach(owner);trace_->primitive_begin();
   const char*names[]={"blk.2.ffn_gate_exps.weight","blk.2.ffn_up_exps.weight","blk.5.ffn_down_exps.weight","blk.2.ffn_down_exps.weight"};
   for(uint32_t c=0;c<4;++c)for(uint32_t e=0;e<2;++e)for(uint32_t pattern=0;pattern<2;++pattern){
    gate("primitive-vector");uint32_t k=c<2?2048:1536,n=c<2?1536:2048;
    // Actual block_q8_K layout used by CPU vec_dot and compiled shaders, no extra GPU re-quantization.
    ua50::fill_q8_pattern(proof_->q8(),2336,k,pattern);
    auto before=gpu_->counts();owner->primitive_vector(names[c],e?63:0,proof_->q8(),k/256*292,proof_->primitive_cpu(),proof_->primitive_gpu(),gpu_);auto after=gpu_->counts();
    const uint32_t ordinal=c*4+e*2+pattern,role=trace_->id_for(names[c]);proof_->compare(3,ordinal,role,e?63:0,pattern,0,proof_->primitive_cpu(),proof_->primitive_gpu(),n);trace_->primitive_end(ordinal,role,e?63:0,pattern,n,before,after);
   }
   gate("primitive-compute-complete"); // retain owner through final integrity/monitor gate
  }catch(...){request_stop();drain();throw;}
 }
 void finish_normal(){close_current();gpu_->drain();if(!terminal())throw std::runtime_error("proof terminal missing");}
 std::string layer_witness_summary()const{return witness_?witness_->summary():"null";}
 std::string vector_witness_summary()const{return vectors_?vectors_->summary():"null";}
 std::string sweep_witness_summary()const{return sweep_?sweep_->summary():"null";}
 std::string selected_vector_summary()const{return selected_vectors_?selected_vectors_->summary():"null";}
 std::string graph_summary()const{return graph_?graph_->summary():"null";}
 std::string cross_arm_summary(uint64_t max_bytes)const{return cross_?cross_->summary(max_bytes):"null";}
 std::string held_weight_summary(uint64_t max_bytes)const{return cross_?cross_->held_summary(max_bytes):"null";}
 void seal(bool normal){trace_->seal(normal&&terminal(),uint32_t(prompt_count_),t0_,t1_,proof_->passed());}
 uint32_t cpu_rows()const{return cpu_rows_;}uint32_t gpu_rows()const{return gpu_rows_;}size_t prompt_count()const{return prompt_count_;}const llama_token*prompt_ids()const{return prompt_.data();}llama_token t0()const{return t0_;}llama_token t1()const{return t1_;}llama_token g0()const{return g0_;}llama_token g1()const{return g1_;}
};
}
