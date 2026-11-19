#pragma once
#include "llama.h"
#include "native_provider.h"
#include <functional>
#include <memory>
namespace ua50 {
enum class InputContract { sealed_local, controlled_disk, windows_held_verified };
struct ExpertLoadPlan {
 uint64_t required_file_bytes=0,available_file_bytes=0,routed_tensor_bytes=0;
 uint32_t layers=0,experts=0;
 size_t records=0,max_matrix=0,arena_bytes=0,cache_owned_bytes=0,stage_bytes=0;
 bool full_payload=false;
};
// Internal C++ seam only. Admission/integrity proof remains owned by the caller;
// these explicit contracts are NOT interchangeable platform immutability proofs.
struct OwnedExpertInput {
 FILE *file=nullptr;std::shared_ptr<void> file_anchor;
 InputContract contract=InputContract::sealed_local;
 std::function<std::shared_ptr<Reader>(size_t required_stage)> make_reader;
 // Required only for the distinct Windows app-held contract; never a Linux upgrade.
 std::shared_ptr<void> admission_anchor;
 std::function<void(FILE *)> validate_binding;
};
struct OwnedExpertLoad {
 std::function<void(const ExpertLoadPlan&)> admit_hybrid; // mandatory fresh native/app joint guard before tensor allocation
 std::shared_ptr<ExpertGpu> gpu; // explicit hybrid only; null preserves CPU
 OwnedExpertInput input;size_t slots=2,cache_ceiling=16*1024*1024,stage_ceiling=4*1024*1024;
};
// Metadata-only inspection never pretends a prefix contains the full payload.
ExpertLoadPlan inspect_expert_input(FILE *,size_t slots);
llama_model *load_owned_experts(const OwnedExpertLoad &,llama_model_params);
}
