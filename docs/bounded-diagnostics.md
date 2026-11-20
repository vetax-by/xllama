# Bounded diagnostic capture

## Target investigation update — 2026-11-20

**Selected captures and their independent reviews are complete; model2 remains
unaccepted.** The installed signed 1.7.0.1526 and native build are unchanged.
The original comparator remains `abs(HYBRID - CPU) <= 0.001 + 0.001 * abs(CPU)`;
no tolerance change or intervention was used.

### Same-artifact prerequisites and execution

After the owner reported setting Game for this package, fresh native identity
observed a 5,368,709,120-byte quota without opening the model. Fresh primitive16
on the same artifact passed 16 rows / 28,672 finite comparisons with zero
violations, GPU submissions/completions/returns 16/16/16 and three full held
model SHA passes. These bounded observations do not establish peak memory,
full-row offline primitive replay or full-model parity.

The final six-run diagnostic batch and the separate one-run row14 selection
each have independently checked resource, native release/Exit, process,
caller and durable-service closure. Finite service supervision uses no restart;
service success alone cannot replace native closure. Earlier UNKNOWN captures
and external closures remain historical UNKNOWN outcomes, with their evidence
and spent selections preserved. Both completed selections are exhausted.

### What the captures establish

| Observation | Supported conclusion | Still open |
| --- | --- | --- |
| L34 residual, all 2,048 coordinates in both arms | ADD matches FP32 round-to-nearest-even (RNE). A and B separately pass tolerance; R crosses at 271, 1043 and 1549. All three sums are exact within each arm. Cancellation and the smaller CPU-relative result tolerance explain these local crossings. | Origin of incoming drift and full-model failure; correct ADD is not full-model parity. |
| L34 weighted MoE chain | Actual weights, slots and operation links bind the decomposition. Expert 47 contributes the largest weighted product difference at those three coordinates. Captured MUL, slot sums and ADDs replay consistently. | Algebraic attribution is not an intervention or proof of L2-to-L34 causality. |
| Norm operands | Learned scale values match between arms; RMS output already differs before the learned-scale MUL. Captured MUL/ADD results replay consistently. | Internal RMS input reads, reduction, sqrt/inverse and an independent RMS oracle were not captured. |
| H2 minimal/rich/minimal | CPU operator_norm, ffn_norm and operator_attn_out are byte-identical across all three captures, 2,048 values per tensor. | Only selected CPU repeatability: minimal is instrumented; no global/HYBRID neutrality or fresh-state proof. |
| L2 weighted chain | Captured incoming T/S/A, producer ffn_norm and final weights match, while captured MoE down results already differ. All selected comparisons remain below tolerance and arithmetic replays consistently. | This brackets observed drift; it does not locate the first internal/global cause. |

### Complete row14: equal captured inputs, different arithmetic results

The separate L2 gate/expert37 row14 capture includes the complete eight-block
Q4 weight row. Actual F32 input, packed Q8 and complete W are byte-identical
between arms; selected GPU W/Q8 echoes equal the held inputs. CPU result C and
same-input CPU oracle H both equal `bd770820` (-0.0603104829788208).
GPU pre/post-publication G equals `bd770813` (-0.06031043455004692).
The difference is **13 ULP = 13/268435456 = 4.842877388e-8**, below the unchanged
tolerance. All 64 final reduction words match source-order RNE replay from the
actual 64 local accumulators; lane zero equals the published output.

Offline, a fixed source-derived AVX2/FMA model reproduces H exactly. An
unmodified original x86 quantization translation unit built locally with
GCC/AVX2/FMA also reproduces H. A fixed GPU arithmetic candidate reproduces
all 64 LOCAL and 64 FINAL words and G; a fully separate-operation candidate
fails on 27 LOCAL and 36 FINAL words. These are finite source-compatible
models, not unique instruction identification. The shipped DXIL dump contains
FAST fmul/fsub/fadd, not explicit FMad; local GCC results do not prove the
MSVC/Xbox instruction stream. Matching PDB/function mapping and actual GPU
driver ISA are unavailable.

GPU is closer to the named full-row dequant-F32 sum on this row; that reference
is not a golden native-dot oracle or proof of a CPU defect. Echo is not a
shader-load trace, and frozen shader association is not an independent executed
code measurement. Complete W/echo/reference qualification applies to current
row14 only. Full-model cause, GPU FP/ISA and observer neutrality remain open.

### Evidence index and next questions

Retained private review hashes identify provenance, not public downloads:

- Fresh identity: `1b8f8ec16bbeb3a5501172f3151458495da80d31fe6ffcc1cc0e799398edb64a`.
- Primitive16: `a8dd2bde7764380e3f96ef742cd7b127070f772da91fb5806c7e1a3d4608ccdd`.
- H2 interpretation: `5d3c6243b04c1859a539bd708b46002a89bd5574b74c0d2575be76d4230b6b2b`.
- Row14 run `b0528d0ceb0883da22c2fee26e5c2bbd`, saved origin:
  `7cbcaa960c611f18cae950975059d70c762286b2bb90d3321ed66efb3b7046d0`.
- Row14 scientific interpretation:
  `f4b3079915c8e569163e22112d7600c17cf525ecb903feed74876c2cd146798a`.
- Offline source arithmetic models:
  `be965ba1932284266668436b6825af85a1fe4886f38c9de8ba099f7c2cb951b3`.

The next scientific question is whether the identified arithmetic structures
explain a broader set of saved differences and their propagation. Distinguishing
equivalent instruction implementations or testing causal interventions needs
additional evidence and an explicitly selected scope. No new run, binary or
numerical acceptance follows from this update. Track remaining work in
[#3](https://github.com/vetax-by/xllama/issues/3) and the
[index #7](https://github.com/vetax-by/xllama/issues/7).

## Build and source status — historical baseline

The bounded diagnostics implementation is complete at **source/local fixture**
scope. One consolidated independent review and its focused worker-endpoint
correction have no remaining findings. The selected Windows SDK/D3D12
[compile/link/unsigned-package run 44577045317](https://github.com/vetax-by/xllama/actions/runs/44577045317)
succeeded on source/build commit `0553a90b48fc71515396bdf5a947ea3412b13634`.
Independent saved-artifact QA **passed with no findings**, limited to unsigned
Windows compile/link/package and saved artifact integrity. Review result SHA-256:
`d28ea44752412a023083f9d27840f89ebf1d5cb09851a9a81e3df72bbe6c3cda`. The unsigned x64 package is 20,306,364
bytes, SHA-256 `05c9d3d14cd6ea1b674918b9d5e87489b66aa6d6c06a1ee95aa85daef4115cb4`.
The public issue status is [#7](https://github.com/vetax-by/xllama/issues/7).
This is not a target
correctness, numerical parity, observer neutrality, memory peak or performance
acceptance. Historical accepted identity/primitive results apply only to their
own signed artifact. Model2 remains numerically unaccepted.

The first acceptance scenario is the L34 residual/cvec boundary, a primary
investigation target rather than an established root cause. Earlier observations
show both incoming drift and additional small local execution differences;
neither proves the cause of the final tolerance failure. Existing spent runs and
raw evidence are unchanged.

## Signed successor and preserving installation — 2026-11-19

The [selected signing run 44581718753](https://github.com/vetax-by/xllama/actions/runs/44581718753)
succeeded. Signed-artifact review passed with no findings for successor
**1.7.0.1526** (SHA-256
`e07463b633a580fdaeab505ec45f2a8bb3d71487eb485e943697d948120bf6c7`).
The separate stock forward-recovery package **1.7.0.1527** is retained and was
not installed. Signing review result SHA-256:
`ab4226176c728fe86085717d88f9e5efeedc296a01e7e78813fc27942a385496`.

The selected in-place installation completed successfully: the sole registered
package is 1526, both prior owners remain registered, and no app process was
running afterward. Independent saved-outcome review passed with no findings,
limited to **metadata preservation**. The before/after LocalState responses are
byte-identical (230 entries), including the selected model's name, size and date.
Install review result SHA-256:
`bd511a36f6258b117e0fd2d9f458efe3eb50b32ac2a18335e3e0870ed8a24168`.

During the installation step, no app/model activation, reset, uninstall, recovery
installation or weights transfer occurred. Model contents were not read or hashed: metadata preservation
does not establish physical data integrity, all-user storage preservation,
runtime quota or numerical correctness. Historical 1524 identity/primitive
acceptance does not transfer to 1526. Subsequent same-artifact prerequisites and selected target captures are recorded
in the dated update above; model2 remains unaccepted.

## Selectable capture families

The config-v6 / bounded-plan-v2 interface admits closed, bounded profiles R0
(residual/cvec), E0 (expert dispatch), S0 (state windows) and G0 (registered graph,
arithmetic and parameter rows). Up to four common graph side-probes can accompany
R0/E0/S0 under the same owner and aggregate ledger. General registered selectors
allow other supported layers/positions without rebuilding; this is not an
arbitrary tensor/plugin interface.

| Family | Implemented observations | Limits |
| --- | --- | --- |
| N1 worker FP | Actual before/after worker FP environment for packing, dot, binary nodes and slot adds; worker index/count and source lineage | Read-only; no FP setters. Portable fixture labels do not imply actual GPU execution. MSVC x64 x87 availability is explicit. |
| N2 observer control | Logical and mapped graph signatures, split/sync/copy/instrumentation counters | Structural FNV signatures are not value equality or proof of observer neutrality. |
| N3 GPU input echo | Selected weight/Q8 echo after upload and before dispatch, exact PSO/constants/lease/fence/guard binding | Selected actual W/Q8 echoes match held inputs (see dated results above); not a shader load trace or general hardware/driver qualification. |
| N4 state | Source-linked shortconv read/write windows and current prepared F16 K/V windows, typed F32/F16 write source | Different read/write cells supported on mapped one-sequence path. Unknown backend/stride, multisequence, rollback/shift/defrag paths remain explicit unsupported gaps. |
| N5 I/O/cache | Existing Reader/cache offsets, requested/returned/error, hit/miss, generation and lease timeline | No additional weights read; logical I/O is not physical SSD traffic. |
| N6 internal graph | Registered conv projection/input/output, q/k norm and post-RoPE, nonflash attention score/probability/weighted-V windows with op/source descriptors | Bounded windows retain original shape/strides. Missing masks/position inputs prohibit a full independent attention oracle. |
| N7 shader reduction | Default-off selected Q4/Q6 trace64 variants observing local accumulator and final reduction array | Source order retained; instrumentation may affect execution. Compilation is not hardware validation or neutrality. |
| N8 observer common probes | Common registered graph side-probes with R0/E0/S0/G0 | Same owner, selector/record/raw/time limits; missing common probes make comparisons inconclusive. |
| N9 parameter rows | Actual resident output-head or conv-kernel coefficient rows bound to their real consumer | No fake expert lease, extra model read or full matrix dump; unsupported layouts stay explicit. |

R0 binds actual ADD sources before in-place overwrite, immutable A/B, computed R,
and cvec O/state. Source-proved NOOP is an explicit structural alias, not an
independent read. Applied cvec captures the actual bounded vector/revision. Raw
NaN/Inf bits remain typed `NONFINITE_OBSERVED`; no zero substitution occurs.
Lifecycle and publication evidence retain pending owners when fence closure is
UNKNOWN. Optional capture gaps cannot grant science or bypass resource closure.

## Private host analysis and authority

The private host implementation includes H1 named scalar/dequantization and
integer-rational RNE references for complete captured rows, plus local ADD/MUL
and slot-ordered sum checks. GGML repeat semantics and signed zero are retained.
Q8 all-zero packing preserves untouched/unspecified bsums; those raw bytes are
still reported and are not masked into acceptance. These references are not an
assertion that a native SIMD/FMA kernel must implement the same reduction order.

H2 binds unique saved CPU_REFERENCE A/minimal–B/rich–A/minimal runs with identical
common probe keys. The actual independently reviewed series above establishes
selected CPU repeatability only, not global or HYBRID observer neutrality. The host operators, author fixtures, keys, raw evidence,
logs, packages and weights are **not** part of this public source publication.

Exact constrained owner authority, exclusive lock, unique spend/no replay,
original per-run expiry and deterministic bindings remain mandatory. Campaign
scope is distinct from the finite run lease, which starts after child admission;
chat/owner handoff does not consume that run clock. No per-HTTP approval loop is
required. Current 1526 captures bind genuine same-artifact prerequisites. A new
artifact still requires its own source/ENGINE association and prerequisites. D1 interventions are design-only and disabled:
no state replacement, weight mutation, routing intervention or threshold change.

## Budgets and evidence boundary

The owner charges aggregate raw/copy/output/index/work/time, including shared
state indices, selected weight copies, metadata, GPU echo and trace resources.
The fixed ABI bounds are 1 MiB raw, 64 records, 64 KiB/record, 256 KiB index and
1,081,344-byte binary bundle. Shared state indices are capped at 1,024; selected
weight snapshots at 256 KiB / 16 rows / 8 spans. Unknown resources retain anchors.
Installed 1526 retains a 64-sample worker channel. Conservative stage accounting
and explicit partial/missing-stage validation apply. A configurable budget up to
256 has separate source/local review only; it is not built or installed and its
new schema must not be sent to 1526.
Arithmetic admission is not measured peak memory or proof of observer neutrality.
The original numerical thresholds and native release timing/resource gates are
unchanged. Unsupported paths and missing required observations remain gaps.

## Reproducible Windows build

[Source lock](../ua50/bounded/SOURCE-LOCK.json) binds the reviewed native snapshot,
ENGINE and all 136 postimages. The [manual build workflow](../.github/workflows/ua50-bounded.yml)
checks out a **separate clean** pinned upstream product, applies the historical
33-patch preparation once, then replaces and verifies only the exact bounded
postimages. It never patches the fork application tree a second time.

The Windows 2022 job requires SDK 22621/v143, restores pinned dependencies,
validates four base/trace shaders with digest-pinned Windows DXC, and compiles,
links and packages an unsigned app. Validation shader compilation does not
replace the shipping DXIL headers. Artifact receipts record source association,
shader compiler/output hashes and package hashes. No signing, install, model
execution or binary release is performed by this workflow.

Current build source lock:
`d8bdc76107664d11e83e2916ac7d96f38e016076137ff507d873ea6168ecee7c`.
The public fork root still contains the historical application tree; the bounded
build source is the explicit lock/postimage composition, not an implicit build
of that root. See [ua50 build notes](../ua50/README.md).

### Actual build evidence boundary

Windows MSVC/v143 and SDK 22621 compiled and linked the selected source with no
compiler errors; warnings remain recorded. Four pinned-DXC Windows validation
compiles passed. Their output bytes differ from the frozen Linux/historical
shipping DXIL and were not substituted. All four exact frozen base/trace shader
arrays are present in the packaged x64 EXE; that is a static artifact fact, not
proof of runtime PSO selection or hardware behavior.

Two preparation/transport failures were preserved: one ignored source file
stopped the first job before compilation, and a relative post-build receipt
copy prevented artifact upload after a successful second compilation/package.
The final job corrects those transport errors without changing the reviewed
native bytes. Actions was restored to its original disabled state. That unsigned-build
workflow performed no signing or installation. The separately selected signing
and preserving installation are recorded above. Later selected target execution
is recorded separately in the dated investigation update; no public binary
release is claimed.
