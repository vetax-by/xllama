#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct ggml_tensor;
struct ggml_backend_buffer_type;
bool ggml_ua50_is_buft(const struct ggml_backend_buffer_type * buft);
bool ggml_ua50_is_external(const struct ggml_tensor * tensor);
// acquire/release are coordinator-only; data/ok readers need native barriers.
bool ggml_ua50_acquire(const struct ggml_tensor * tensor, int64_t expert);
const char * ggml_ua50_data(const struct ggml_tensor * tensor);
void ggml_ua50_release(const struct ggml_tensor * tensor);
bool ggml_ua50_has_gpu(const struct ggml_tensor * tensor);
// Already packed CPU Q8_K input. Returned rows are never published on failure.
bool ggml_ua50_gpu_vector(const struct ggml_tensor * tensor,const void * q8,uint64_t bytes,float *destination,uint64_t elements);
// Synchronous, optional diagnostic copies at the already-held expert lease.
bool ggml_ua50_has_cross_arm_observer(const struct ggml_tensor * tensor);
void ggml_ua50_diagnostic_unavailable(const struct ggml_tensor * tensor,const char * reason);
void ggml_ua50_diagnostic_binding(const struct ggml_tensor * weight,const struct ggml_tensor * input,const struct ggml_tensor * output);
void ggml_ua50_diagnostic_input(const struct ggml_tensor * tensor,uint32_t slot,uint32_t input_row,
                                const float * f32,uint64_t k,const void * q8,uint64_t q8_bytes);
void ggml_ua50_diagnostic_output(const struct ggml_tensor * tensor,uint32_t slot,uint32_t input_row,
                                 const float * output,uint64_t n);
const char * ggml_ua50_error(const struct ggml_tensor * tensor);
#ifdef __cplusplus
}
#endif
