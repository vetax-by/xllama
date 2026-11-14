#pragma once
#include "llama.h"
#include "ua50/owned_expert_load.h"
#include "windows_held_reader.h"
#include <atomic>
#include <functional>
#include <memory>
#include <vector>
#include <string>
namespace xllama {
// INTERNAL source candidate only. No UI/LAN/C ABI setting or automatic provider activation.
struct CpuExpertOptIn {
 bool enabled=false;
 llama_model_params model=llama_model_default_params();
 llama_context_params context=llama_context_default_params();
 size_t slots=2,cache_ceiling=16u*1024u*1024u,stage_ceiling=4u*1024u*1024u;
 std::function<ua50::uwp::WindowsInputCandidate()> admitted_source;
 // Mandatory app quota/context/compute admission, not guessed from file size.
 std::function<void(const ua50::ExpertLoadPlan&,const llama_context_params&)> admit_budget;
};
class CpuExpertSession final:public std::enable_shared_from_this<CpuExpertSession> {
public:
 enum class State { Opening,Ready,Decoding,Invalid,Closing,Closed };
 static std::shared_ptr<CpuExpertSession> open_if_requested(const CpuExpertOptIn*);
 ~CpuExpertSession();
 void begin_fresh_turn(); // mandatory before each full plain prompt; no saved/raw-thinking KV reuse
 int decode(llama_batch,std::atomic<bool>* turn_abort=nullptr);
 void request_stop() noexcept;
 bool close() noexcept; // false only for an active decode/close: caller must retain owner
 std::vector<llama_token> tokenize(const std::string&) const;
 std::string piece(llama_token,bool special=false) const;
 llama_token sample(llama_sampler*);
 const llama_model* model_view() const; // template/sampler vocabulary only, never context
 uint32_t context_length() const;
 uint32_t batch_length() const;
 void invalidate(const char*) noexcept;
 State state() const noexcept{return state_.load(std::memory_order_acquire);}
 const char* first_failure_after_drain() const noexcept{return first_failure_;}
 CpuExpertSession(const CpuExpertSession&)=delete;
 CpuExpertSession& operator=(const CpuExpertSession&)=delete;
private:
 CpuExpertSession()=default;
 struct ModelDelete{void operator()(llama_model*p)const noexcept{llama_model_free(p);}};
 struct ContextDelete{void operator()(llama_context*p)const noexcept{llama_free(p);}};
 std::shared_ptr<ua50::uwp::WindowsInputCandidate> input_; // held beyond context/model/Reader
 std::unique_ptr<llama_model,ModelDelete> model_;
 std::unique_ptr<llama_context,ContextDelete> context_;
 std::atomic<State> state_{State::Opening};std::atomic<bool> stop_{false};
 std::atomic<bool>* turn_abort_=nullptr; // serialized decode scope; callback only while llama_decode runs
 char first_failure_[256]{};
 static bool should_abort(void*) noexcept;
 void latch_failure(const char*) noexcept;
};
// Mandatory full-held-file SHA callback implementation, Windows BCrypt system API.
// Expected bytes/SHA must come from trusted app catalogue, not a LAN supplied claim.
std::function<ua50::uwp::WindowsInputCandidate()> make_windows_expert_source(
 const std::wstring& trusted_local_path,const ua50::uwp::ExpectedFile& trusted_expected,
 std::shared_ptr<std::atomic<bool>> stop,ua50::uwp::Clock::time_point absolute_deadline,
 size_t stage_ceiling=4u*1024u*1024u);
ua50::uwp::ValidateHeldFile make_held_sha256_validator(
 const ua50::uwp::ExpectedFile&,std::function<bool()> stop,
 ua50::uwp::Clock::time_point absolute_deadline);
}
