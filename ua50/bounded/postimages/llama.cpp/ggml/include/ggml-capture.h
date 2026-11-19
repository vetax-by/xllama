#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
struct ggml_tensor;
struct ggml_cgraph;
enum ggml_capture_kind {
 GGML_CAPTURE_REGISTER=1, GGML_CAPTURE_WANT_BEFORE=2, GGML_CAPTURE_WANT_AFTER=3,
 GGML_CAPTURE_READY_BEFORE_WRITE=4, GGML_CAPTURE_COMPLETE=5,
 GGML_CAPTURE_CVEC=6, GGML_CAPTURE_STATE_READ=7, GGML_CAPTURE_STATE_WRITE=8,
 GGML_CAPTURE_RESET_BEGIN=9, GGML_CAPTURE_RESET_COMPLETE=10,
 GGML_CAPTURE_REMAP=11, GGML_CAPTURE_WANT_READ=12,
 GGML_CAPTURE_READY_BEFORE_READ=13, GGML_CAPTURE_GRAPH_BEGIN=14, GGML_CAPTURE_GRAPH_END=15,
 GGML_CAPTURE_LOGICAL_GRAPH=16, GGML_CAPTURE_LOGICAL_REFERENCE=17
};
/* In-process observation only. No ownership or authority is conveyed by pointers.
 * The receiver must copy synchronously, cannot retain borrowed event metadata,
 * and cannot change a tensor, graph, index, or return graph-abort authority. */
struct ggml_capture_cvec_state {
 int32_t range_start, range_end;
 uint64_t revision;
 struct ggml_tensor * direction;
 bool applied;
};
typedef bool (*ggml_capture_state_reader)(const void *, int32_t, void *, size_t, struct ggml_capture_cvec_state *);
struct ggml_capture_event {
 int kind;
 struct ggml_tensor * tensor;
 const struct ggml_cgraph * graph;
 struct ggml_tensor * related[3];
 const char * semantic;
 int32_t layer;
 int64_t values[8];
 const void * state_owner;
 ggml_capture_state_reader state_reader;
};
typedef bool (*ggml_capture_callback)(const struct ggml_capture_event *, void *);
/* Read-only state sampled on the actual CPU worker. QUERY carries no FP
 * measurement and selects a concrete tensor/worker, never graph abort authority.
 * valid_mask: 1=fenv round/exceptions, 2=MXCSR, 4=x87 control word. */
enum ggml_capture_worker_stage { GGML_CAPTURE_WORKER_QUERY=0,
 GGML_CAPTURE_WORKER_NODE_BEFORE=1, GGML_CAPTURE_WORKER_NODE_AFTER=2,
 GGML_CAPTURE_WORKER_PACK_BEFORE=3, GGML_CAPTURE_WORKER_PACK_AFTER=4,
 GGML_CAPTURE_WORKER_DOT_BEFORE=5, GGML_CAPTURE_WORKER_DOT_AFTER=6,
 GGML_CAPTURE_WORKER_GPU_HOST_BEFORE=7, GGML_CAPTURE_WORKER_GPU_HOST_AFTER=8 };
struct ggml_capture_worker_event {
 const struct ggml_tensor * tensor;
 uint32_t stage, requested_stage, worker_index, worker_count, valid_mask;
 int32_t rounding_mode, exception_flags;
 uint32_t mxcsr, x87_control, fused_following, expert;
};
typedef bool (*ggml_capture_worker_callback)(const struct ggml_capture_worker_event *, void *);
#ifdef __cplusplus
}
#endif
