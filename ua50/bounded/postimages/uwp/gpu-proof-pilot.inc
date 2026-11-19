// Actual dedicated developer headless entry. No UI/LAN or CPU ticket routing.
// Future exact artifact/operator/Root selection must bind this new ticket/entry; source edits grant no launch.
void run_expert_gpu_proof_configured(){
 const std::string marker="[gpu-proof-v1]";auto compute=try_acquire_compute_job();if(!compute){log_output(marker+" REFUSED compute busy\n");return;}
 auto&hub=session_hub();std::unique_lock<std::mutex> hub_lock(hub.mtx,std::try_to_lock);if(!hub_lock||hub.session||hub.failed_expert_load){log_output(marker+" REFUSED existing owner\n");return;}
 auto state=std::make_shared<Pilot>();state->gpu_aggregate_quota=true;state->deadline=ua50::uwp::Clock::now()+std::chrono::minutes(20);
 std::shared_ptr<GpuProofInput> input;std::shared_ptr<ua50::ExpertGpu> gpu;std::shared_ptr<gpu_proof::Controller> controller;
 std::shared_ptr<gpu_proof::Proof> proof;std::shared_ptr<gpu_proof::Trace> trace;std::shared_ptr<gpu_proof::VectorWitness> vectors;
 std::shared_ptr<gpu_proof::SweepWitness> sweep;std::shared_ptr<gpu_proof::SelectedVectorWitness> selected_vectors;
 std::shared_ptr<capture::GraphCapture> bounded;
 std::shared_ptr<gpu_proof::GraphWitness> graph;std::shared_ptr<gpu_proof::CrossArmWitness> cross;
 std::optional<ua50::uwp::WindowsInputCandidate> guard;ModelWriteGate::Permit writer;CpuExpertOptIn opt;
 std::array<CpuExpertEffective,2> actual_effective{};std::array<ua50::Counters,2> host_counts{};size_t live_phase=0;uint64_t sha_calls=0,sha_requested=0,sha_returned=0,sha_elapsed_ns=0;
 bool pre_sha=false,post_sha=false,normal=false,claim=false;std::string cause,package,committed_output_sha;ua50::GpuSnapshot gpu_effective;ua50::GpuCounts gpu_counts;
 uint64_t bounded_memory=0;size_t pcount=0;uint32_t cpu_rows=0,gpu_rows=0;std::array<llama_token,128> prompt_ids{};llama_token t0=-1,t1=-1,g0=-1,g1=-1;HANDLE output=INVALID_HANDLE_VALUE,trace_file=INVALID_HANDLE_VALUE;
 auto fail=[&](const char*s){if(cause.empty())cause=s?s:"unknown proof failure";normal=false;};
 try{
  auto q=state->snapshot("before-gpu-config");state->initial_limit=q.limit;try{check(q,q.limit,probe_allowance);}catch(...){record_selected_preclaim_reason(!q.limit?ingress_reason_v2::quota_unstable:ingress_reason_v2::pressure_or_headroom,q.limit,q.usage,q.limit!=0);throw;}
  const std::filesystem::path root(winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str());
  input=std::make_shared<GpuProofInput>(root,take_selected_ingress(ingress_v2::Stage::proof));if(input->ticket.ingress_v2)state->ingress_reason=std::make_shared<ingress_reason_v2::Recorder>();state->ticket_expiry=input->ticket.expires;const auto now=dev_utc_ms();if(now>=state->ticket_expiry){record_selected_preclaim_reason(ingress_reason_v2::utc_expired);throw std::runtime_error("GPU ticket expired before admission");}state->deadline=std::min(state->deadline,ua50::uwp::Clock::now()+std::chrono::milliseconds(state->ticket_expiry-now));state->stop_path=input->path("stop").wstring();
  if(state->ingress_reason)state->phase("v2-before-claim",probe_allowance);
  if(std::filesystem::exists(input->path("output.json"))||std::filesystem::exists(input->path("trace.bin")))throw std::runtime_error("GPU old output identity exists");
  // Namespace admission and exact bytes precede claim/hash; authority is separately bound by future Ops/Root.
  input->spend();claim=true;output=CreateFile2(input->path("output.json").c_str(),GENERIC_WRITE,FILE_SHARE_READ,CREATE_NEW,nullptr);if(output==INVALID_HANDLE_VALUE)throw std::runtime_error("GPU main CREATE_NEW");trace_file=CreateFile2(input->path("trace.bin").c_str(),GENERIC_WRITE,FILE_SHARE_READ,CREATE_NEW,nullptr);if(trace_file==INVALID_HANDLE_VALUE)throw std::runtime_error("GPU trace CREATE_NEW");
  require_matching_catalogue();writer=ModelDownloader::WriterGate().try_acquire();if(!writer.owns_lock())throw std::runtime_error("GPU own-writer exclusion unavailable");
  std::weak_ptr<Pilot> weak=state;state->limit_changed=winrt::Windows::System::MemoryManager::AppMemoryUsageLimitChanging(winrt::auto_revoke,[weak](const auto&,const winrt::Windows::System::AppMemoryUsageLimitChangingEventArgs&a)noexcept{if(auto s=weak.lock()){const auto next=a.NewLimit();const bool stops=next!=s->initial_limit;s->record_ingress_reason(ingress_reason_v2::limit_event|(stops?ingress_reason_v2::limit_changed:0),stops,s->initial_limit,next,0,false,true,false);if(stops)s->request_stop();}});state->start_monitor();
  for(const auto&folder:{root,root/L"models",root/L"models"/L"lfm25-24b-a2b-q4-k-m"}){CREATEFILE2_EXTENDED_PARAMETERS p{};p.dwSize=sizeof(p);p.dwFileFlags=FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT;DevHandle d(CreateFile2(folder.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,OPEN_EXISTING,&p));FILE_ATTRIBUTE_TAG_INFO a{};if(d.value==INVALID_HANDLE_VALUE||!GetFileInformationByHandleEx(d.value,FileAttributeTagInfo,&a,sizeof(a))||!(a.FileAttributes&FILE_ATTRIBUTE_DIRECTORY)||(a.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("GPU LocalFolder model path unavailable/reparse");}
  const auto path=(root/L"models"/L"lfm25-24b-a2b-q4-k-m"/file).wstring();
  HashReadObserver hash_observer=[&](size_t want,size_t got,int64_t elapsed){if(elapsed<0)throw std::runtime_error("GPU SHA clock");sha_calls=add(sha_calls,1);sha_requested=add(sha_requested,want);sha_returned=add(sha_returned,got);sha_elapsed_ns=add(sha_elapsed_ns,uint64_t(elapsed));};
  auto source=make_windows_expert_source(path,{bytes,sha},state->abort,state->deadline,4u<<20,hash_observer);
  state->phase("outer-held-preSHA",probe_allowance);guard=source();pre_sha=true;state->held=guard->file;{std::lock_guard<std::mutex> l(state->mutex);state->control=guard->control;}
  state->phase("GPU-driver-initialization",probe_allowance);
  ExpertGpuAdmission admission{input->config.adapter_luid,input->config.shader_sha,[state](const ua50::GpuSnapshot&,uint64_t pending){state->phase("GPU-resource-admission",pending);}};
  gpu=make_expert_gpu_d3d12(admission);gpu_effective=gpu->snapshot();
  const uint64_t legacy_diagnostics_memory=input->config.diagnostics_v3?
   add(input->config.diagnostics_v3->serializer_charge,add(input->config.diagnostics_v3->base.memory_budget,add(input->config.diagnostics_v3->graph_memory,add(input->config.diagnostics_v3->cross_memory,input->config.diagnostics_v3->held_memory)))):
   input->config.diagnostics?input->config.diagnostics->memory_budget:gpu_proof::VectorWitness::charged_bytes;
  bounded_memory=input->config.bounded_diagnostics?add(add(input->config.bounded_diagnostics->limits.owner,input->config.bounded_diagnostics->gpu_memory()),add(8*add(input->config.bounded_diagnostics->limits.bundle,input->config.bounded_diagnostics->limits.index),1048576)):0;
  const uint64_t diagnostics_memory=add(legacy_diagnostics_memory,bounded_memory);
  if(input->config.bounded_diagnostics){
   state->phase("bounded-capture-owner-admission",bounded_memory);
   const capture::Hash digest=[](const void*data,size_t bytes){capture::need(bytes<=1081344,"bounded hash input cap");const auto text=dev_sha(std::string(static_cast<const char*>(data),bytes));std::array<unsigned char,32>out{};for(size_t i=0;i<32;++i){const auto nib=[](char c){return c<='9'?c-'0':c-'a'+10;};out[i]=static_cast<unsigned char>((nib(text[i*2])<<4)|nib(text[i*2+1]));}return out;};
   bounded=std::make_shared<capture::GraphCapture>(*input->config.bounded_diagnostics,digest,sha,capture::packer_source_sha);
  }
  state->phase("proof-storage-admission",add(add(4u<<20,gpu_proof::Trace::required_owned_bytes()),input->config.stage==gpu_proof::Stage::Model2?add(gpu_proof::LayerWitness::charged_bytes,diagnostics_memory):0));
  proof=std::make_shared<gpu_proof::Proof>(input->config.stage);trace=std::make_shared<gpu_proof::Trace>(GetCurrentProcessId(),input->config.stage==gpu_proof::Stage::Primitive16?3:4,input->config.id,input->config_sha,input->ticket_sha,gpu_run::engine_sha,gpu_effective.shader_sha256);
  opt.enabled=true;opt.plan_profile=CpuExpertOptIn::PlanProfile::FinishedAnswer512;opt.slots=2;opt.cache_ceiling=16u<<20;opt.stage_ceiling=4u<<20;opt.numerical_proof_owned_bytes=4u<<20;opt.instrumentation_owned_bytes=add(add(input->owned_bytes(),262144+4096),input->config.stage==gpu_proof::Stage::Model2?add(gpu_proof::LayerWitness::charged_bytes,diagnostics_memory):0);
  opt.model.main_gpu=-1;opt.model.n_gpu_layers=0;opt.model.split_mode=LLAMA_SPLIT_MODE_NONE;opt.model.load_mode=LLAMA_LOAD_MODE_NONE;opt.model.lazy_mode=LLAMA_LAZY_MODE_OFF;opt.model.use_extra_bufts=false;
  opt.context.n_ctx=512;opt.context.n_batch=1;opt.context.n_ubatch=1;opt.context.n_seq_max=1;opt.context.n_outputs_max=1;opt.context.n_outputs_max_per_seq=1;opt.context.n_threads=1;opt.context.n_threads_batch=1;opt.context.offload_kqv=false;opt.context.op_offload=false;opt.context.flash_attn_type=LLAMA_FLASH_ATTN_TYPE_DISABLED;opt.context.type_k=GGML_TYPE_F16;opt.context.type_v=GGML_TYPE_F16;
  opt.model.progress_callback=[](float,void*p)->bool{return !static_cast<Pilot*>(p)->abort->load();};opt.model.progress_callback_user_data=state.get();
  opt.admitted_source=[&,source]{
   state->gpu_progress_ns.store(0);state->phase("fresh-session-heldSHA",probe_allowance);guard->validate_binding(guard->file);auto c=source();
   if(c.admission.identity!=guard->admission.identity||c.admission.bytes!=bytes||c.admission.sha256!=sha)throw std::runtime_error("sequential reference source identity drift");return c;
  };
  // The guard FILE+writer remain across sequential contexts; each new session gets its own non-reusable control/FILE and FULL-held SHA, never resets an old control.
  opt.admit_budget=[&](const ua50::ExpertLoadPlan&plan,const llama_context_params&){auto m=plan_cpu_expert_memory(guard->file,opt,plan);uint64_t extra=gpu->required_owned_bytes()+gpu_proof::Trace::required_owned_bytes()+sizeof(gpu_proof::Controller)+4096;state->phase("hybrid-joint-native-plan",add(m.total(),extra));};
  opt.effective_observer=[&](const CpuExpertEffective&e){if(e.context!=512||e.threads!=1||e.threads_batch!=1||e.slots!=2||e.ceiling!=(16u<<20)||e.owned>e.ceiling)throw std::runtime_error("hybrid actual native effective mismatch");actual_effective[live_phase]=e;};
  opt.phase_check=[state](const char*p){state->phase(p);};opt.cache_events=[&](const char*,const ua50::Counters&c){host_counts[live_phase]=c;};
  auto progress=[&](const char*p){
   const bool reference=std::strcmp(p,"cpu-reference-open")==0||std::strcmp(p,"primitive-open")==0;
   const bool opening=reference||std::strcmp(p,"hybrid-open")==0;if(opening)state->gpu_progress_ns.store(0);
   if(std::strcmp(p,"hybrid-open")==0)live_phase=1;
   state->phase(p); // validate OLD progress before resetting it; no concealed long operation
   if(std::strcmp(p,"proof-before-decode")==0||std::strcmp(p,"proof-after-decode")==0||std::strcmp(p,"primitive-vector")==0)state->gpu_progress_ns.store(std::chrono::duration_cast<std::chrono::nanoseconds>(ua50::uwp::Clock::now().time_since_epoch()).count());
  };
  if(input->config.stage==gpu_proof::Stage::Model2&&!input->config.diagnostics)vectors=std::make_shared<gpu_proof::VectorWitness>([state](const void*p,size_t n){
   if(!p||!n||n>2580480||ua50::uwp::Clock::now()>state->deadline||state->abort->load())throw std::runtime_error("vector witness digest bound/stop");
   return dev_sha(std::string(static_cast<const char*>(p),n));
  });
  if(input->config.diagnostics){
   auto sweep_plan=*input->config.diagnostics,vector_plan=*input->config.diagnostics;
   sweep_plan.reference_work_budget=sweep_plan.sweep_work_upper();vector_plan.reference_work_budget=vector_plan.vector_work_upper();
   if(!vector_plan.vectors.empty()){sweep_plan.time_budget_ms=3*vector_plan.time_budget_ms/4;vector_plan.time_budget_ms-=sweep_plan.time_budget_ms;}
   sweep=std::make_shared<gpu_proof::SweepWitness>(std::move(sweep_plan));
   selected_vectors=std::make_shared<gpu_proof::SelectedVectorWitness>(std::move(vector_plan),[state](const void*p,size_t n){
    if(!p||!n||n>2580480||ua50::uwp::Clock::now()>state->deadline||state->abort->load())throw std::runtime_error("selected vector digest bound/stop");
    return dev_sha(std::string(static_cast<const char*>(p),n));
   });
  }
  if(input->config.diagnostics_v3){const auto&p=*input->config.diagnostics_v3;
   if(!p.graph.empty())graph=std::make_shared<gpu_proof::GraphWitness>(p.graph,p.graph_abs_ppm,p.graph_rel_ppm,p.graph_memory,p.graph_work,p.graph_time_ms,p.graph_output,p.graph_workload_axis);
   if(!p.cross_arm.empty())cross=std::make_shared<gpu_proof::CrossArmWitness>(p.cross_arm,p.cross_abs_ppm,p.cross_rel_ppm,p.cross_work,p.cross_time_ms,p.raw_private);
   if(!p.held_weight.empty())cross->select_held(p.held_weight,p.held_work,p.held_time_ms,p.held_raw_private);
  }
  controller=std::make_shared<gpu_proof::Controller>(proof,trace,gpu,progress,vectors,sweep,selected_vectors,cross,graph,bounded);{std::lock_guard<std::mutex>l(state->mutex);state->gpu_controller=controller;}
  if(input->config.stage==gpu_proof::Stage::Primitive16)controller->primitive16(opt);else controller->model2(opt);
  state->gpu_progress_ns.store(0);state->phase("outer-held-postSHA");auto validator=make_held_sha256_validator({bytes,sha},[state]{return state->abort->load();},state->deadline,hash_observer);auto verified=validator(guard->file,bytes);guard->validate_binding(guard->file);post_sha=verified.bytes_checked==bytes&&verified.sha256==sha;
  state->finish_monitor();if(state->abort->load())throw std::runtime_error("final monitor stop; retain numeric partial");
  controller->finish_normal();if(!proof->passed())fail("NUMERICAL_GATE_FAILED_OR_INCOMPLETE; inspect bounded coverage/witness");normal=proof->passed()&&controller->terminal()&&post_sha&&actual_effective[0].context==512&&(input->config.stage==gpu_proof::Stage::Primitive16||actual_effective[1].context==512);
 }catch(const std::exception&e){fail(e.what());}catch(...){fail("GPU proof unknown failure");}
 state->finish_monitor();if(state->abort->load())fail("final monitor stopped; no normal commit");
 std::string layer_witness_json="null",vector_witness_json="null",sweep_json="null",selected_json="null",graph_json="null",cross_json="null",held_json="null";if(controller){if(!normal){controller->request_stop();controller->drain();}if(input&&input->config.stage==gpu_proof::Stage::Model2){try{layer_witness_json=controller->layer_witness_summary();}catch(const std::exception&e){fail(e.what());}try{vector_witness_json=controller->vector_witness_summary();}catch(const std::exception&e){log_output(marker+" FIRST_VECTOR_WITNESS_UNAVAILABLE diagnostic_only: "+e.what()+"\n");}try{sweep_json=controller->sweep_witness_summary();selected_json=controller->selected_vector_summary();}catch(const std::exception&e){log_output(marker+" CONFIGURABLE_DIAGNOSTICS_UNAVAILABLE diagnostic_only: "+e.what()+"\n");}try{graph_json=controller->graph_summary();cross_json=controller->cross_arm_summary(input->config.diagnostics_v3?input->config.diagnostics_v3->cross_output:4096);if(input->config.diagnostics_v3&&!input->config.diagnostics_v3->held_weight.empty())held_json=controller->held_weight_summary(input->config.diagnostics_v3->held_output);}catch(const std::exception&e){log_output(marker+" V3_DIAGNOSTICS_UNAVAILABLE diagnostic_only: "+e.what()+"\n");}}cpu_rows=controller->cpu_rows();gpu_rows=controller->gpu_rows();pcount=controller->prompt_count();if(pcount>128){fail("prompt trace bound");pcount=0;}else std::copy(controller->prompt_ids(),controller->prompt_ids()+pcount,prompt_ids.begin());t0=controller->t0();t1=controller->t1();g0=controller->g0();g1=controller->g1();}
 if(gpu){gpu->drain();try{gpu_counts=gpu->counts();gpu_effective=gpu->snapshot();if(gpu->error()[0])fail(gpu->error());}catch(const std::exception&e){fail(e.what());}}
 if(normal&&input){
  const uint64_t hashes=input->config.stage==gpu_proof::Stage::Primitive16?3:4;
  if(sha_calls!=hashes*((bytes+65535)/65536+1)||sha_requested!=hashes*(bytes+1)||sha_returned!=hashes*bytes)fail("all-held-SHA aggregate coverage mismatch");
  const uint64_t frames=uint64_t(pcount)+1;const bool primitive=input->config.stage==gpu_proof::Stage::Primitive16;
  const uint64_t dispatched=primitive?16:456*frames;
  if(gpu_counts.requests!=dispatched||gpu_counts.submits!=dispatched||gpu_counts.completed!=dispatched||gpu_counts.returned!=dispatched||gpu_counts.published!=(primitive?0:dispatched)||gpu_counts.q4!=(primitive?12:384*frames)||gpu_counts.q6!=(primitive?4:72*frames)||gpu_counts.weight_bytes!=(primitive?31555584ull:865271808ull*frames)||gpu_counts.q8_bytes!=(primitive?32704ull:976448ull*frames))fail("actual GPU dispatched quant/byte coverage mismatch");
 }
 if(normal&&trace&&input&&input->config.stage==gpu_proof::Stage::Model2){auto routes=trace->compare_model_routes(uint32_t(pcount));if(!routes.available||!routes.equal)fail("CPU/hybrid ordered routing divergence or incomplete route coverage");}
 // Release session-owned GPU resources before file/writer/main commit. Process-global stock GPU resources remain explicitly reported.
 if(controller){try{controller->seal(normal);}catch(const std::exception&e){fail(e.what());}controller.reset();}
 gpu.reset();opt.admitted_source={};opt.hybrid_gpu.reset();guard.reset();writer.release();state->held=nullptr;state->limit_changed.revoke();
 try{
  if(!input||!claim)throw std::runtime_error("GPU claim/input unavailable; no numerical evidence");
  if(!proof||!trace){
   if(output!=INVALID_HANDLE_VALUE){const auto quote=first_answer::quote;const std::string refused="{\"schema\":\"ua68-gpu-proof-output-v1\",\"normal\":false,\"status\":\"REFUSED_BEFORE_NUMERICAL_PROOF\",\"run_id\":"+quote(input->config.id)+",\"config_sha256\":"+quote(input->config_sha)+",\"ticket_sha256\":"+quote(input->ticket_sha)+",\"error\":"+quote(cause)+",\"proof\":null,\"trace\":null,\"owner_file_writer_closed\":true}";DWORD written=0;if(!WriteFile(output,refused.data(),DWORD(refused.size()),&written,nullptr)||written!=refused.size()||!FlushFileBuffers(output))throw std::runtime_error("GPU refusal commit failed");if(!CloseHandle(output))throw std::runtime_error("GPU refusal close failed");output=INVALID_HANDLE_VALUE;committed_output_sha=dev_sha(refused);log_output(marker+" OUTPUT_COMMITTED partial/refused/error owner/file/writer closed\n");}
   throw std::runtime_error("numerical proof not reached; empty trace not replayable");
  }
  package=winrt::to_string(winrt::Windows::ApplicationModel::Package::Current().Id().FullName());
  if(normal&&!trace->complete())fail("complete numerical trace unavailable");
  auto summary=proof->summary(true,normal);if(summary.size()>65536)throw std::length_error("GPU summary cap");
  if(trace_file==INVALID_HANDLE_VALUE)throw std::runtime_error("GPU trace missing");DWORD n=0;if(!WriteFile(trace_file,trace->data(),DWORD(trace->file_bytes),&n,nullptr)||n!=trace->file_bytes||!FlushFileBuffers(trace_file))throw std::runtime_error("GPU trace commit failed");if(!CloseHandle(trace_file)){trace_file=INVALID_HANDLE_VALUE;throw std::runtime_error("GPU trace close failed");}trace_file=INVALID_HANDLE_VALUE;
  log_output(marker+(normal?" TRACE_COMMITTED complete replayable\n":" TRACE_COMMITTED partial not replayable\n"));
  const auto quote=first_answer::quote;std::string json;json.reserve(262144);
  json="{\"schema\":\"ua68-gpu-proof-output-v1\",\"normal\":"+std::string(normal?"true":"false")+",\"scope\":\"xbox-ua68-expert-sync-proof-v1\",\"stage\":"+quote(input->config.stage==gpu_proof::Stage::Primitive16?"primitive16":"model2")+",\"run_id\":"+quote(input->config.id)+",\"config_sha256\":"+quote(input->config_sha)+",\"ticket_sha256\":"+quote(input->ticket_sha)+",\"engine_sha256\":"+quote(gpu_run::engine_sha)+",\"native_package_full_name\":"+quote(package)+",\"pid\":"+std::to_string(GetCurrentProcessId())+",\"requested\":"+input->config.raw+",\"effective\":{\"tuple\":\"cpu_graph_d3d12_expert_sync\",\"cpu_threads\":"+std::to_string(actual_effective[live_phase].threads)+",\"context\":"+std::to_string(actual_effective[live_phase].context)+",\"host_slots\":"+std::to_string(actual_effective[live_phase].slots)+",\"GPU_slots\":1,\"GPU_outstanding_max\":1,\"adapter_luid\":"+quote(gpu_effective.adapter_luid)+",\"compiled_shader_sha256\":"+quote(gpu_effective.shader_sha256)+",\"fallback\":false},\"comparator_source_sha256\":"+quote(gpu_proof::comparator_source_sha)+",\"oracle_source_sha256\":"+quote(gpu_proof::oracle_source_sha)+",\"threshold_descriptor_sha256\":"+quote(gpu_proof::threshold_descriptor_sha)+",\"patterns_descriptor_sha256\":"+quote(gpu_proof::patterns_descriptor_sha)+",\"oracle_native_revision\":"+quote(gpu_proof::oracle_revision)+",\"oracle_symbol\":\"ggml_get_type_traits_cpu(Q4_K/Q6_K).vec_dot/Q8_K\",\"machine_code_digest_available\":false,\"external_signed_artifact_binding_required\":true,\"pre_sha_verified\":"+(pre_sha?"true":"false")+",\"post_sha_verified\":"+(post_sha?"true":"false")+",\"owner_file_writer_closed\":true,\"error\":"+quote(cause)+",\"proof\":"+std::string(summary)+",\"reference_rows_captured\":"+std::to_string(cpu_rows)+",\"GPU_rows_captured\":"+std::to_string(gpu_rows)+",\"prompt_ids\":[";
  for(size_t i=0;i<pcount;++i){if(i)json+=",";json+=std::to_string(prompt_ids[i]);}json+="],\"cpu_samples\":["+std::to_string(t0)+","+std::to_string(t1)+"],\"gpu_samples\":["+std::to_string(g0)+","+std::to_string(g1)+"],\"first_greedy_divergence_position\":"+(g0>=0&&g0!=t0?std::to_string(pcount-1):g1>=0&&g1!=t1?std::to_string(pcount):"null")+",\"trace\":{\"schema\":\"UA68TRC1\",\"bytes\":"+std::to_string(trace->file_bytes)+",\"complete\":"+(trace->complete()?"true":"false")+",\"fnv1a64\":\"";
  char fnv[17];std::snprintf(fnv,sizeof(fnv),"%016llx",static_cast<unsigned long long>(trace->whole_fnv()));json+=fnv;json+="\"},\"GPU_counts\":{\"submits\":"+std::to_string(gpu_counts.submits)+",\"completed\":"+std::to_string(gpu_counts.completed)+",\"returned_vectors\":"+std::to_string(gpu_counts.returned)+",\"graph_published\":"+std::to_string(gpu_counts.published)+",\"q4\":"+std::to_string(gpu_counts.q4)+",\"q6\":"+std::to_string(gpu_counts.q6)+",\"weight_bytes\":"+std::to_string(gpu_counts.weight_bytes)+",\"q8_bytes\":"+std::to_string(gpu_counts.q8_bytes)+",\"fence_poll_wait_ns\":"+std::to_string(gpu_counts.fence_wait_ns)+",\"copy_CPU_ns\":"+std::to_string(gpu_counts.copy_cpu_ns)+",\"GPU_compute_ns\":null,\"timestamp_status\":\"not_collected_first_slice\",\"terminal\":"+(gpu_counts.terminal?"true":"false")+"},\"resources\":[";
  for(size_t i=0;i<gpu_effective.resources.count;++i){auto&a=gpu_effective.resources.entries[i];if(i)json+=",";json+="{\"name\":"+quote(a.name)+",\"allocation_bytes\":"+std::to_string(a.bytes)+",\"process_global\":"+(a.existing?"true":"false")+"}";}
  json+="],\"GPU_init_observed_app_usage_delta\":"+std::to_string(gpu_effective.init_observed_app_delta)+",\"GPU_owned_creation_app_usage_delta\":"+std::to_string(gpu_effective.owned_creation_app_delta)+",\"init_delta_is_driver_only\":false,\"driver_probe_reserved\":268435456,\"proof_reserved\":4194304,\"reserve\":536870912,\"quota_limit\":"+std::to_string(state->initial_limit)+",\"sampled_max_app_usage\":"+std::to_string(state->max_sampled_usage.load())+",\"quota_snapshot_count\":"+std::to_string(state->gpu_quota_snapshots.load())+",\"quota_log_mode\":\"aggregate_no_per_expert_lines\",\"sampled_not_peak\":true,\"DXGI_budget\":"+std::to_string(gpu_effective.dxgi_budget)+",\"DXGI_usage\":"+std::to_string(gpu_effective.dxgi_usage)+",\"UMA_separate_RAM\":false,\"SHA_calls\":"+std::to_string(sha_calls)+",\"SHA_requested\":"+std::to_string(sha_requested)+",\"SHA_returned\":"+std::to_string(sha_returned)+",\"host_cache\":[";
  for(size_t i=0;i<2;++i){if(i)json+=",";auto&c=host_counts[i];json+="{\"requests\":"+std::to_string(c.requests)+",\"hits\":"+std::to_string(c.hits)+",\"misses\":"+std::to_string(c.misses)+",\"logical_reader_calls\":"+std::to_string(c.read_calls)+",\"logical_reader_bytes\":"+std::to_string(c.read_bytes)+",\"reader_ns\":"+std::to_string(c.reader_ns_total)+",\"acquire_ns\":"+std::to_string(c.acquire_ns_total)+"}";}
  const auto routes=trace->compare_model_routes(uint32_t(pcount));
  json+="],\"routes_equal\":"+(input->config.stage==gpu_proof::Stage::Primitive16?std::string("null"):routes.available?(routes.equal?std::string("true"):std::string("false")):std::string("null"))+",\"first_route_divergence\":";
  if(routes.available&&!routes.equal)json+="{\"frame\":"+std::to_string(routes.frame)+",\"key_in_frame\":"+std::to_string(routes.key)+",\"cpu_key\":"+std::to_string(routes.cpu_key)+",\"gpu_key\":"+std::to_string(routes.gpu_key)+"}";else json+="null";
  json+=",\"SHA_elapsed_ns\":"+std::to_string(sha_elapsed_ns)+",\"storage_coverage\":{\"provider_reader\":\"cache_success_commit_counters_only\",\"partial_reader_bytes_available\":false,\"metadata_load_file_io\":\"unobserved\",\"SHA\":\"all_instrumented_fread_EOF_and_errors\"},\"CPU_Q8_pack_ns\":null,\"physical_SSD_metrics\":null,\"raw_rows_saved\":false,\"offline_rawrow_replay\":false,\"native_consumption_epoch_exported\":false";
  json+="}";
  if(json.size()>262144||json.capacity()>(2u<<20)||output==INVALID_HANDLE_VALUE)throw std::length_error("GPU combined main cap/output");n=0;if(!WriteFile(output,json.data(),DWORD(json.size()),&n,nullptr)||n!=json.size()||!FlushFileBuffers(output))throw std::runtime_error("GPU main commit failed");if(!CloseHandle(output)){output=INVALID_HANDLE_VALUE;throw std::runtime_error("GPU main close failed");}output=INVALID_HANDLE_VALUE;committed_output_sha=dev_sha(json);
  // Critical main and trace are closed before optional immutable capture.
  if(bounded)try{
   state->phase("bounded-capture-finalize",bounded_memory);
   capture::Identity identity;identity.run=input->config.id;identity.selection=input->ticket.selection;identity.instance=input->instance_id;identity.pfn=package;identity.pid=GetCurrentProcessId();identity.source=gpu_run::engine_sha;identity.profile=input->config_sha;identity.config=input->config_sha;identity.ticket=input->ticket_sha;identity.main=committed_output_sha;identity.trace=dev_sha(std::string(reinterpret_cast<const char*>(trace->data()),trace->file_bytes));
   const capture::Hash digest=[](const void*data,size_t bytes){capture::need(bytes<=1081344,"bounded hash input cap");const auto text=dev_sha(std::string(static_cast<const char*>(data),bytes));std::array<unsigned char,32>out{};for(size_t i=0;i<32;++i){const auto nib=[](char c){return c<='9'?c-'0':c-'a'+10;};out[i]=static_cast<unsigned char>((nib(text[i*2])<<4)|nib(text[i*2+1]));}return out;};
   const auto identity_raw=identity.json();auto binary=bounded->owner().finish(digest(identity_raw.data(),identity_raw.size()));
   auto measured=bounded->owner().instrument();capture::need(bounded->owner().reserve_output(capture::mul(input->config.bounded_diagnostics->limits.index,2)),"bounded index copy/hash work reserve");const auto index=capture::index_json(identity,input->config.id+".bounded-diagnostics-v1.bin",binary,digest,bounded->owner().records(),bounded->metadata(),bounded->all_groups(),input->config.bounded_diagnostics->limits.index,bounded->sequence()+1);
   publish_bounded_capture(input->root,input->config.id,binary,index,bounded,[&]{state->phase("bounded-capture-publication",bounded_memory);capture::need(bounded->owner().within_budget(),"bounded accumulated instrumentation deadline");});
   log_output(marker+" BOUNDED_CAPTURE_COMMITTED optional raw bits, science NOT_GRANTED\n");
  }catch(const std::exception&e){log_output(marker+" BOUNDED_CAPTURE_UNAVAILABLE optional: "+e.what()+"\n");}
  // Optional model2 diagnostic sidecar. Main output bytes/schema and acceptance stay unchanged.
  // A reader must verify BOTH saved raw SHA hashes before using this diagnostic.
  if(input->config.stage==gpu_proof::Stage::Model2&&layer_witness_json!="null")try{
   state->phase("layer-witness-sidecar",2u<<20);
   const std::string raw_trace(reinterpret_cast<const char*>(trace->data()),trace->file_bytes);
   const std::string sidecar="{\"schema\":\"ua68-model2-layer-witness-sidecar-v1\",\"diagnostic_only\":true,\"run_id\":"+quote(input->config.id)+",\"config_sha256\":"+quote(input->config_sha)+",\"ticket_sha256\":"+quote(input->ticket_sha)+",\"output_sha256\":"+quote(dev_sha(json)) + ",\"trace_sha256\":"+quote(dev_sha(raw_trace)) + ",\"witness\":"+layer_witness_json+"}";
   if(sidecar.size()>131072)throw std::length_error("layer witness sidecar cap");
   DevHandle side(CreateFile2(input->path("layer-witness.json").c_str(),GENERIC_WRITE,FILE_SHARE_READ,CREATE_NEW,nullptr));
   if(side.value==INVALID_HANDLE_VALUE)throw std::runtime_error("layer witness sidecar CREATE_NEW");
   DWORD written=0;if(!WriteFile(side.value,sidecar.data(),DWORD(sidecar.size()),&written,nullptr)||written!=sidecar.size()||!FlushFileBuffers(side.value))throw std::runtime_error("layer witness sidecar commit");side.close();
   log_output(marker+" LAYER_WITNESS_COMMITTED diagnostic_only\n");
  }catch(const std::exception&e){log_output(marker+" LAYER_WITNESS_UNAVAILABLE diagnostic_only: "+e.what()+"\n");}
  if(input->config.stage==gpu_proof::Stage::Model2&&vector_witness_json!="null")try{
   state->phase("first-vector-sidecar",gpu_proof::VectorWitness::charged_bytes);
   const std::string raw_trace(reinterpret_cast<const char*>(trace->data()),trace->file_bytes);
   const std::string sidecar="{\"schema\":\"ua68-model2-first-vector-sidecar-v1\",\"diagnostic_only\":true,\"run_id\":"+quote(input->config.id)+",\"config_sha256\":"+quote(input->config_sha)+",\"ticket_sha256\":"+quote(input->ticket_sha)+",\"output_sha256\":"+quote(dev_sha(json))+",\"trace_sha256\":"+quote(dev_sha(raw_trace))+",\"witness\":"+vector_witness_json+"}";
   if(sidecar.size()>32768)throw std::length_error("first vector sidecar cap");
   DevHandle side(CreateFile2(input->path("first-vector-witness.json").c_str(),GENERIC_WRITE,FILE_SHARE_READ,CREATE_NEW,nullptr));
   if(side.value==INVALID_HANDLE_VALUE)throw std::runtime_error("first vector sidecar CREATE_NEW");
   DWORD written=0;if(!WriteFile(side.value,sidecar.data(),DWORD(sidecar.size()),&written,nullptr)||written!=sidecar.size()||!FlushFileBuffers(side.value))throw std::runtime_error("first vector sidecar commit");side.close();
   log_output(marker+" FIRST_VECTOR_WITNESS_COMMITTED diagnostic_only\n");
  }catch(const std::exception&e){log_output(marker+" FIRST_VECTOR_WITNESS_UNAVAILABLE diagnostic_only: "+e.what()+"\n");}
  if(input->config.diagnostics&&!input->config.diagnostics_v3&&sweep_json!="null"&&selected_json!="null")try{
   state->phase("configurable-diagnostics-sidecar",input->config.diagnostics->output_budget);
   const std::string raw_trace(reinterpret_cast<const char*>(trace->data()),trace->file_bytes);
   const std::string sidecar="{\"schema\":\"ua68-model2-configurable-diagnostics-sidecar-v1\",\"diagnostic_only\":true,\"external_signed_artifact_binding_required\":true,\"run_id\":"+quote(input->config.id)+",\"config_sha256\":"+quote(input->config_sha)+",\"ticket_sha256\":"+quote(input->ticket_sha)+",\"output_sha256\":"+quote(dev_sha(json))+",\"trace_sha256\":"+quote(dev_sha(raw_trace))+",\"overview\":"+sweep_json+",\"vectors\":"+selected_json+"}";
   if(sidecar.size()>input->config.diagnostics->output_budget)throw std::length_error("configurable diagnostics sidecar cap");
   DevHandle side(CreateFile2(input->path("diagnostics.json").c_str(),GENERIC_WRITE,FILE_SHARE_READ,CREATE_NEW,nullptr));
   if(side.value==INVALID_HANDLE_VALUE)throw std::runtime_error("configurable diagnostics sidecar CREATE_NEW");
   DWORD written=0;if(!WriteFile(side.value,sidecar.data(),DWORD(sidecar.size()),&written,nullptr)||written!=sidecar.size()||!FlushFileBuffers(side.value))throw std::runtime_error("configurable diagnostics sidecar commit");side.close();
   log_output(marker+" CONFIGURABLE_DIAGNOSTICS_COMMITTED diagnostic_only\n");
  }catch(const std::exception&e){log_output(marker+" CONFIGURABLE_DIAGNOSTICS_UNAVAILABLE diagnostic_only: "+e.what()+"\n");}
  if(input->config.diagnostics_v3&&sweep_json!="null"&&selected_json!="null")try{
   state->phase("consolidated-diagnostics-sidecar",input->config.diagnostics_v3->base.output_budget+input->config.diagnostics_v3->graph_output+input->config.diagnostics_v3->cross_output+input->config.diagnostics_v3->held_output);
   const std::string raw_trace(reinterpret_cast<const char*>(trace->data()),trace->file_bytes);
   const bool with_held=!input->config.diagnostics_v3->held_weight.empty();
   const std::string sidecar="{\"schema\":\""+std::string(with_held?"ua68-model2-consolidated-diagnostics-sidecar-v2":"ua68-model2-consolidated-diagnostics-sidecar-v1")+"\",\"diagnostic_only\":true,\"external_signed_artifact_binding_required\":true,\"run_id\":"+quote(input->config.id)+",\"config_sha256\":"+quote(input->config_sha)+",\"ticket_sha256\":"+quote(input->ticket_sha)+",\"output_sha256\":"+quote(dev_sha(json))+",\"trace_sha256\":"+quote(dev_sha(raw_trace))+",\"overview\":"+sweep_json+",\"vectors\":"+selected_json+",\"graph\":"+graph_json+",\"cross_arm\":"+cross_json+(with_held?",\"held_weight\":"+held_json:"")+"}";
   if(sidecar.size()>(2u<<20))throw std::length_error("consolidated diagnostics sidecar cap");
   DevHandle side(CreateFile2(input->path("diagnostics.json").c_str(),GENERIC_WRITE,FILE_SHARE_READ,CREATE_NEW,nullptr));
   if(side.value==INVALID_HANDLE_VALUE)throw std::runtime_error("consolidated diagnostics sidecar CREATE_NEW");
   DWORD written=0;if(!WriteFile(side.value,sidecar.data(),DWORD(sidecar.size()),&written,nullptr)||written!=sidecar.size()||!FlushFileBuffers(side.value))throw std::runtime_error("consolidated diagnostics sidecar commit");side.close();
   log_output(marker+" CONSOLIDATED_DIAGNOSTICS_COMMITTED diagnostic_only\n");
  }catch(const std::exception&e){log_output(marker+" CONSOLIDATED_DIAGNOSTICS_UNAVAILABLE diagnostic_only: "+e.what()+"\n");}

  log_output(marker+(normal?" OUTPUT_COMMITTED normal owner/file/writer closed\n":" OUTPUT_COMMITTED partial/refused/error owner/file/writer closed\n"));
 }catch(const std::exception&e){fail(e.what());log_output(marker+" OUTPUT_FAILED: "+e.what()+"\n");}
 if(trace_file!=INVALID_HANDLE_VALUE)CloseHandle(trace_file);if(output!=INVALID_HANDLE_VALUE)CloseHandle(output);
 if(input&&input->ticket.ingress_v2&&state->ingress_reason&&!committed_output_sha.empty())try{commit_ingress_reason(input->path("reason-v2.json"),*state->ingress_reason,ingress_reason_v2::Stage::proof,input->ticket.id,input->ticket_sha,committed_output_sha,input->ticket.expires);}catch(const std::exception&e){log_output(marker+" REASON_UNAVAILABLE diagnostic_only: "+e.what()+"\n");}
 if(input&&input->ticket.ingress_v2&&!committed_output_sha.empty())try{
  if(package.empty())package=winrt::to_string(winrt::Windows::ApplicationModel::Package::Current().Id().FullName());
  commit_ingress_terminal(input->path("terminal-v2.json"),ingress_v2::Stage::proof,input->ticket.id,input->ticket_sha,committed_output_sha,package,normal);
 }catch(const std::exception&e){log_output(marker+" TERMINAL_MARKER_UNAVAILABLE: "+e.what()+"\n");}
 log_output(marker+(normal?" terminal normal\n":" terminal stopped; no retry\n"));
}
