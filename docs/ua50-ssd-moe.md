# Demand-loaded MoE experts for xllama

## Purpose

This experimental fork explores running a routed Mixture-of-Experts checkpoint
whose complete weight file is larger than the memory available to the Xbox
application. It extends xllama and its pinned llama.cpp backend; it does not
replace either engine with Strata or combine several independent dense models.

The intended split is:

- Keep the complete GGUF weight file on local storage.
- Keep shared, non-routed tensors, model state, KV/recurrent state and working
  buffers resident as required by the architecture.
- Read the expert matrices selected by the router into a bounded RAM cache.
- Execute the accepted expert path on the CPU, retaining every matrix lease
  until all native workers using it have finished.

The objectives are a larger usable model, at least 4K context with 8K as a goal,
and sustained generation of at least 5 tokens/s with 10+ as a target. These are
future Xbox acceptance goals, not results demonstrated by this fork.

## What the patch changes

### Expert loading and execution

The internal owned-input loader installs an expert provider before tensor
loading. Routed expert weights use provider-managed storage instead of eager
full allocation; metadata-derived extents and budgets are checked before use.
Optional scale tensors and other non-routed weights remain resident.

The CPU `MUL_MAT_ID` path acquires the requested expert/role slice and releases
it only after the participating native workers drain. The current small-cache
path processes one expert/role lease at a time; it does not pin every selected
gate/up/down matrix simultaneously. This establishes a bounded ownership path,
not an optimized retention policy or a throughput result.

### File ownership and Windows reader

The Windows candidate anchors the loader's synchronous `FILE*` and a separate
overlapped read handle to the same file identity. A caller-bound whole-file
SHA-256 validator hashes the held file in bounded chunks; it does not reopen a
path to validate a different file.

Reads go into a bounded staging buffer. Data reaches the cache destination only
after terminal completion, a full byte count and the identity/stop checks.
Cancellation retains the stage, event, `OVERLAPPED` and handles until the OS
actually reports terminal completion. This is not a hard deadline guarantee:
an indefinitely pending OS operation can require an indefinite drain.

This controlled held-file contract is distinct from Linux sealed-file tests.
It is not a claim of kernel immutability or protection against a hostile writer.

### Session lifetime and failure behavior

An internal, explicitly enabled CPU expert option is connected to the actual
`Session::create` path before the ordinary resident factory. The default option
is null/disabled, preserving the ordinary path. Enabled admission failures do
not silently fall back to resident loading.

The owning controller routes prompt preparation, decoding, sampling, stopping
and teardown through one session. Failed opening identity survives a null or
throwing factory, preventing automatic same-load recreation until an explicit
reset or different load identity. Decode/read failure invalidates the owner.
Native workers and pending I/O drain before their backing resources are freed.

The enabled adapter buffers token callbacks until a successful turn completes.
A later decode failure therefore does not publish a partial generated answer.
This also means this experimental path does not provide incremental token
streaming during generation. Side effects or exceptions in the caller's own
callback cannot be rolled back.

### LFM2.5 A2B thinking

The patch recognizes the LFM2.5 A2B family and preserves its source template's
always-thinking generation suffix. It handles the generated thinking opener,
plain prompts, persisted history and display postprocessing explicitly. This
is not an invented thinking-off option, a model-ID rename or a dummy-tool
workaround. KV restore/snapshots are disabled for this exact family in the
candidate until their behavior is validated.

The current expert adapter supports full plain-text turns. Tool decoding, KV
continuation, LoRA, GPU offload and prompt lookup are explicitly refused on
this path. Existing ordinary xllama routes are separate.

### Explicit console CPU pilot

A developer-only headless pilot now supplies the previously missing admission
callbacks for one compile-pinned LFM2.5-24B-A2B Q4 checkpoint. It uses a fixed
application-owned model path and exact file size/SHA-256, not a mutable catalogue
entry or a caller-provided checksum. The existing application writer gate stays
held until the reader, control and backing file have actually closed.

The pilot reads the real in-process UWP MemoryManager quota, usage and limit
changes. A metadata-only native plan estimates non-routed model buffers,
KV/recurrent state, graph/CPU workspace, output, cache and staging requirements
before weight allocation. A 512 MiB reserve and 256 MiB planning allowance are
conservative policies, not measured maxima, hard allocation caps or protection
against every allocation spike. Missing or insufficient admission refuses the
load without a resident fallback.

Only an explicit local developer flag selects this path; a persistent claim
prevents automatic repeat attempts. It requests one CPU backend, a short raw
prompt, two samples and one next-token decode step. Memory/status monitoring,
a watchdog and stop forwarding remain active, with terminal I/O drain before
resource release. Ordinary UI/LAN behavior remains unchanged. The pilot does
not establish an interactive assistant, tool support, 4K–8K context or a speed
target; those require later tests.

The earlier smoke path has compiled and linked on Windows and completed a
bounded CPU prompt/next-token step on a developer console, including its
held-file integrity gates and normal teardown. This is separate from the new
first-answer mode below. It does not establish an interactive assistant,
sustained performance, long-context quality or cancellation behavior.

### Bounded first visible answer

A separate opt-in developer mode renders one fixed short Russian greeting using
the checkpoint's verified single-user/no-system chat-template subset. The
model supplies its own generated text: the seeded assistant thinking opener is
recorded separately and is never counted as generated output. The tokenizer
owns BOS, the whole prompt must fit without trimming, and the mode uses a
bounded context and at most 128 greedy samples.

In the prepared product tree, the dedicated LocalFolder leaf prefix is
`ua50-cpu-lfm24-first-answer-v1`: `.flag` selects activation, `.claim` is the
persistent exclusive one-shot marker, `.stop` requests cooperative stop and
`.output.json` is the bounded private result. Old smoke claims are retained;
the mode never deletes a claim to arrange an automatic retry. Creating a flag
is a developer operation requiring the reviewed package, admitted model and
actual quota checks, not a general inference API.

A private bounded record retains sampled token IDs, reversible piece bytes,
UTF-8 validity, true EOS/EOT or length stop reason and monotonic prefill/sample/
decode timing. A length cap inside an open thinking section is labeled as such;
no final answer or closing tag is invented. Generation timing includes expert
read waits and separates hashing, file opening and model loading.

A complete result requires successful pre/post held-file integrity checks,
normal owner/file/writer teardown, exact output write/flush/close, new log
markers and observed termination of the bound process. Partial diagnostics and
an output file by themselves do not prove normal completion. Old smoke claims
remain intact; this mode has its own exclusive activation and result namespace.

This first-answer profile has portable functional evidence using explicit fake
native/platform boundaries, an independently inspected Windows build and a
bounded console generation with normal teardown. The sample cap ended inside
thinking, so that run did not produce a completed final assistant answer. Private
responses, device telemetry and runtime identifiers are not publication inputs.

### Separate finished-answer candidate

An eighth preparation patch preserves the earlier modes and adds the separate
`ua50-cpu-lfm24-finished-answer-v2` prefix with its own flag, persistent claim,
cooperative-stop and private-output leaves. Its fixed profile allows up to 384
sampled tokens, including reasoning, with a context of at most 512. The actual
prompt must be at most 128 tokens and the complete prompt plus generation
allowance must fit without trimming. CPU-only execution, one thread, the source
chat template and the two-slot expert cache remain unchanged.

Collector and serialization memory is explicitly charged to the native joint
plan. The larger generation allowance does not weaken quota, pressure, reserve,
held-file integrity, ownership or terminal-close requirements. The same raw JSON
schema carries the new scope and limits; per-piece, aggregate-text and serialized
output byte caps still refuse overflow rather than clipping output.

A terminated final text, a length-capped text after thinking and open reasoning
without a final section are classified separately while retaining the original
record. Neither literal closing-tag presence nor normal teardown asserts semantic
answer quality. The new profile has narrow portable fake-boundary functional,
source/shipping and inspected Windows compile/link/package evidence. A later
signed preserving update and bounded console run produced an actual generated
thinking close and final text, native termination and normal teardown. This
one turn is not assistant-quality, long-context or sustained-performance
acceptance. This profile retains its two-slot cache and does not implement GPU
execution or automatic continuation beyond the sample cap.

### Cache A/B source candidate

Two additional patches preserve the eight earlier patches and add separate
two-slot and 512-slot developer profiles. Both retain the finished-answer
prompt, template, sampler, CPU thread, context and sample allowance, and share
identical tracing and timing code. Only cache capacity and its admitted owned
allocation differ. The large cache ceiling is a bound, not proof that it fits.
Actual cache metadata, padded payload arena, staging, trace object and buffer
are charged by the native plan before payload allocation; the existing reserve,
probe allowance and fresh quota guards remain.

The existing route callback records every acquisition before lookup, including
hits, in actual expert and graph execution order. The bounded binary trace
contains role byte lengths and per-decode boundaries. Request hits and byte
hits have different denominators for mixed quantization. Reader and acquire
timers include their respective call overhead; neither measures physical SSD
traffic or pure device wait. Ordered LRU replay is accounting on that trace,
not a measurement of another run's speed. Matching token IDs or text does not
replace full-row resident numerical parity.

After integrity and final monitor checks, the owner drains and closes backing
resources before the trace is sealed and written. A complete trace must be
committed before the main normal output; missing, partial, failed, overflowed or
clock-invalid traces cannot pass the complete-pair gate. A normal, terminally
closed two-slot run and complete validated pair are prerequisites for the
512-slot run's fresh admission. New namespaces preserve all earlier claims and
outputs. There is no automatic retry or guard reduction.

Narrow source/shipping, actual small cache-byte tests and synthetic trace/replay
checks have passed. The [ten-patch cache Windows build](https://github.com/vetax-by/xllama/actions/runs/43658336759)
compiled, linked and produced an inspected unsigned package. Its signed update
and actual console cache measurements remain unperformed. Cache hit rates, speed
advantage, physical I/O reduction and large-cache fit are not established.

## Evidence and limits

The [original pinned Windows baseline build](https://github.com/vetax-by/xllama/actions/runs/43592901046)
has completed successfully and produced an inspected unsigned x64 package. That
baseline includes the stock AppContainer guard and **none of our six patches**.
The [earlier six-patch CPU-pilot build](https://github.com/vetax-by/xllama/actions/runs/43594526711)
also completed successfully, applying all six locked patches and verifying all
45 selected source files. Its unsigned x64 package is 21,051,447 bytes, SHA-256
`35d0ad3b12ae203e37cfaa69577ba8cd34709d3626b4efc41e755260f08854ae`.
The actual executable imports the required BCrypt and overlapped-file APIs.
Successful compilation and linking do not prove that those APIs, native memory
planning or model decoding work on the console. Both packages are version
`1.7.0.0` as unsigned build artifacts.

The distinct [seven-patch first-answer build](https://github.com/vetax-by/xllama/actions/runs/43627054490)
has also completed compilation, linking and package inspection, with all 47
selected source files verified. Its unsigned package is 21,072,488 bytes,
SHA-256 `808e50ba90d42b09c745eeb67221719afa4e50e80a04817ceb3724d7fe6be89f`,
also version `1.7.0.0`. That profile subsequently produced bounded console
reasoning after a signed preserving update, as qualified above. The separate
384-sample successor has since completed its own build, signed preserving
update and one bounded genuine final-output acceptance, as qualified below.
Build warnings are retained; this is not a warning-free build or proof of runtime
correctness.

The distinct [eight-patch finished-answer build](https://github.com/vetax-by/xllama/actions/runs/43645815389)
compiled, linked and produced an inspected unsigned x64 package, with all 47
selected source files verified under its historical eight-patch lock. The package is
21,075,228 bytes, SHA-256
`3bfc3b28cd4006c8913329f27a6fd598611e7761da7bbf87269da6e50ad8b615`,
version `1.7.0.0` as the unsigned artifact. Its later signed preserving update
and one bounded console result have been accepted: actual generated thinking
close and final text, native termination and normal teardown. Private text,
token IDs and telemetry are not included. This does not establish semantic
quality, resident-reference parity, a usable chat UI or sustained performance.

| Area                          | What has been checked                                                                                                                         | What it does not establish                                                                                           |
| ----------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------- |
| Native CPU integration        | Actual LFM2MoE factory/context/decode on generated GGUF fixtures, resident/cache logits, routes, mixed-quant slice ledgers and fault handling | Real checkpoint parity, useful assistant quality or Xbox performance                                                 |
| Real 8B checkpoint            | One successful native prompt and next-token step using streamed experts                                                                       | Two-session reproducibility, resident-reference parity, sustained speed or context quality                           |
| Real 24B checkpoint           | Bounded CPU smoke, generated reasoning and one genuine final section with source-qualified held-file integrity and normal teardown                              | Real resident-reference parity, sustained speed, long context or assistant quality              |
| Portable reader core          | Nine fake-backend test groups                                                                                                                 | Windows API behavior or real device cancellation                                                                     |
| Session hub and adapter/owner | Actual hub tests and five functional adapter/owner groups with ASan/UBSan; named native/platform boundaries are fakes                         | Native math, production Windows I/O or BCrypt execution                                                              |
| Shipping preparation          | Exact patch order, selected postimages, registration and source-admission checks                                                              | Actual installation or runtime behavior on the console                                                               |

Windows build evidence includes SDK/dependency resolution, MSBuild compilation,
linking and unsigned packaging for both tracks. It is separate from the earlier
Linux checkpoint tests and their refused attempts.

An earlier Linux 24B attempt was refused before native launch. Its snapshot at
the exact pressure-trigger instant was not retained.
Later resource data cannot identify its precise ancestor/value, attribute it to
the model, or establish OOM or model fit. The refused attempt is not relabeled
as a model correctness failure.

The bounded console smoke milestone is not a performance benchmark. Dense-model
Historical dense D3D12 results do not prove routed expert support. The new
explicit selected-expert bridge described below still requires native GPU proof. Logical reader bytes are not
physical SSD traffic; process RSS is not the full memory cost of OS page cache,
KV, activations, UI and backend buffers.

The bounded developer pilot supplies built-in checkpoint/path binding, writer
ownership, quota/planning and watchdog/status callbacks. The successful short
console path does not cover every fault, cancellation or allocation case. General catalogue activation and a
usable UI/LAN assistant remain separate work; missing admission callbacks refuse
the internal opt-in.

## Source layout and reproducible preparation

The reviewed base is xllama commit
`4db51522c850e62d0bb24234dfeb48e55f4017d7` and llama.cpp commit
`d81235049384534c167caea52b85a694f6103d14`.

The application foundation is visible in this branch; the additional CPU pilot
changes are carried in the locked preparation patches. The llama.cpp submodule
gitlink remains upstream-pinned; its modifications are carried as checked
patches, not committed inside the submodule. Use the preparation recipe below
instead of treating the raw fork checkout as an already prepared build tree.

[`ua50/PATCH-LOCK.json`](../ua50/PATCH-LOCK.json) is the authority for the
ordered patch route. Apply the stock AppContainer guard once, then each listed
patch in its exact order on a separate clean base tree. The configurable
CPU successor retains the ten historical entries and adds its root and native
patches. The GPU proof successor adds one root and one native patch after those
twelve entries. The identity-only successor appends one root patch after the GPU
root/native pair. Two further generation patches give the current seventeen ordered patches and 77 shipping postimages. Do not additionally apply intermediate development patches or apply a
cumulative patch twice.

The historical session `.inc` files and the additional GPU `.inc` files are
shipping include dependencies, not additional translation units. The first-answer record/template add two
shipping headers without adding a translation unit. Test doubles, local fixtures, Linux-only
reader adapters and reference overlays are excluded from the shipping set.

See [`ua50/README.md`](../ua50/README.md) for the reviewed unsigned preparation
recipe, selected source inventories and the manual-only Windows workflow.
Baseline and patched builds use separate clean copies of the pinned upstream
source. The workflow has no push/pull-request trigger. Actions is normally disabled for this fork and temporarily enabled only for a
separately selected manual attempt. Preparation failures are not successful
compiles. The successful baseline and CPU-pilot package evidence above is tied
to those exact runs and source revisions.

## License and relationship to upstream

xllama's MIT license and copyright notice are retained in [`LICENSE`](../LICENSE).
The fork's additions use the same MIT license. This is an independent
experimental fork, not an upstream xllama release or endorsement. Model weights
and third-party binary dependencies have their own licenses; no weights,
private signing keys or local device credentials are added by this publication.

## Configurable developer runner

The configurable successor uses one CPU engine with typed, immutable per-run settings. Named cache presets resolve to complete configs; changing a supported setting does not require another application build. This is a developer interface. Unsupported settings refuse explicitly and cannot enable an unimplemented backend or lifecycle capability.

The first capability envelope is one user message, greedy sampling, one CPU worker, a context of 512 tokens, up to 128 rendered prompt tokens and 1–384 new samples. The whole prompt and generation allowance must fit. Global expert-cache slots range from 2 to 512 within an explicit owned-memory ceiling; the actual native sizing and fresh application quota must still admit the complete plan. The fixed reserve, probe, integrity, template/BOS and owner-drain protections are outside the config surface. GPU fields initially admit only the neutral CPU tuple. GPU execution, history, UI streaming, repeated-session recipes and larger contexts require their own implementation and acceptance.

Raw UTF-8 config bytes are bounded to 16 KiB and parsed with a closed typed schema. Unknown or duplicate keys, nonfinite values, coercions, unsupported combinations and unsafe identities refuse. An unused run ID derives fixed local leaves. A separate bounded, expiring one-use ticket binds that ID, the exact raw config hash, a selection ID and the compiled capability-contract digest. Native code creates an exclusive persistent claim and consumes the exact held ticket before opening the model. A failed or uncertain launch does not reset the claim, renew the original expiry or permit another launch. Completion cannot delete a newer ticket by its pathname.

The capability digest identifies a reproducible compiled descriptor; it is not a self-referential binary hash or an execution permission. The operator separately binds the inspected package. Results retain the exact consumed config and ticket hashes, native package identity, immutable requested settings and values from the actual constructed engine and memory plan. Invalid pre-claim input can produce only a refusal log; missing output never means success.

The explicit successor result and binary-trace versions preserve the historical validators and records. Every cache acquisition, including hits, remains ordered and paired with decode boundaries and actual matrix lengths. Trace completeness, normal generation, a genuine final section and storage-observation coverage are independent facts. The Research validator is the single decoder and replay implementation; the controller also requires the selected package, fresh process identity, commit markers and actual owner/caller closure.

Storage aggregates distinguish covered provider Reader operations, held-file SHA reads and unobserved loader/metadata I/O. Reader and cache elapsed times are nested application measurements; terminal read bytes are logical completion bytes. SHA read time excludes hash computation and can affect the OS file cache. These values do not measure physical SSD traffic, service latency or saturation. Optional system metrics remain unavailable until explicitly supported and observed. Detail collection cannot silently exceed the charged trace, collector or JSON limits.

The historical CPU developer release completed reviewed source, inspected Windows artifacts, signing evidence and a preserving update. Independent bounded CPU configuration qualification subsequently covered the selected cases; it does not establish general model usability or GPU execution. A successful historical fixed-profile answer is not evidence that arbitrary configs, cache capacities or acceleration settings have been qualified.

The first configurable Windows compile stopped at an unresolved developer
header include. Its source-relative include route has been corrected without
changing the engine contract, computation or project/import declarations. A
replacement CPU artifact subsequently passed separate inspection, signing and
a preserving update. The original failed attempt remains a failure; it does not
establish a usable package. This historical correction is not the new GPU build.

## Selected-expert GPU correctness candidate

The additional bridge executes selected Q4_K/Q6_K matrix vectors synchronously
inside the existing CPU graph. It keeps CPU activation packing, ascending expert
routing and the two-slot host cache. A single GPU slot and one outstanding
submission hold their host inputs through fence completion, validate guards
before publication and preserve the first error. It does not claim backend
support for general `MUL_MAT_ID`, asynchronous paging or a GPU expert cache.

A new closed developer protocol separates a sixteen-vector primitive stage
from a two-row model stage. The second stage requires an actual independently
accepted primitive receipt for the same artifact, package, engine, compiled
shader and adapter. Missing bindings refuse. Historical CPU config/output/trace
formats are preserved. One-use tickets, immutable run identities and original
expiry remain mandatory; configuration is not execution authority.

The numerical envelope compares all elements online against the CPU reference,
with fixed tolerance, coverage/finite/error counts and bounded witnesses. CPU
reference context closes before the hybrid context opens. Full numeric rows
are not exported: saved summary consistency is not independent offline
recomputation of every error or full-row parity. A separate charged proof
allocation, collector, actual GPU resources and native plan must fit a fresh
application quota with unchanged reserve and probe. DXGI budget is not extra RAM.

Normal output requires actual fence/owner/file closure, integrity checks and
trace then main commit. The operator separately checks process absence and
caller closure before local proof finalization. Partial, stopped, unknown or
contradictory records cannot establish normal proof. Reader timing is not
physical SSD timing; GPU compute time remains unavailable in this first slice.

Source and saved portable author checks are accepted; actual Windows/D3D12
kernels/shaders and console primitive/model correctness remain pending. This
candidate is not a general GPU chat runner or a configurable performance suite.
Stability and practical model usability follow correctness; worker counts and
memory profiles require separately implemented capabilities before later speed
comparisons. Velocity/FSR research stays outside this implementation.


## Identity-only GPU observation successor

The fifteenth patch adds a distinct one-use headless request before model and
proof ingress. A strict request of at most 4 KiB selects a run and original expiry.
A durable exclusive claim consumes only the held request. The native route creates
a temporary device using the same adapter selection as the proof bridge, records
actual adapter LUID, architecture, memory snapshots and the digest of the compiled
Q4_K/Q6_K shader bytes, then closes its temporary resources. It creates no command
queue, PSO, staging buffers or model Reader and submits no GPU math. The output is
exclusive, bounded to 16 KiB and written, flushed and closed before normal terminal.

This successor has accepted source review, portable Request/OneUse author checks
and an independently inspected unsigned Windows package. App and pilot objects
were compiled and linked. Native COM, BCrypt and identity query execution remain
unverified; compilation does not establish those runtime outcomes. Boundary checks enforce the original expiry and a
30-second native window; blocking platform calls are not hard-cancellable. Refusal
or uncertain closure cannot establish a normal observation.

The observed compiled-bytecode digest is not driver-JIT machine code or a numerical
correctness, fit or performance result. External operator evidence must bind the
same actual signed successor artifact, package, request, process and returned caller
closure. A successor observation cannot attest an earlier installed artifact.
Actual identity acceptance must precede the separately selected primitive proof;
an accepted primitive proof must precede the model proof. No current source review
or public recipe dispatches these stages. Existing CPU/proof protocols and claims
remain separate; private requests, receipts and observations are excluded here.

Both explicit identity and GPU proof ticket branches preserve an ordinary historical
`api.flag` as inert. They reject metadata/query/close errors and nonregular files,
then return through their selected headless route before UI/LAN/legacy dispatch.
Other conflicting developer flags still refuse. No flag is deleted or reset.
The corrected App ingress has now compiled in the inspected Windows artifact.
It does not establish native identity or GPU execution.

## Identity successor signing composition

The accepted unsigned identity successor comes from the public Windows run
[43735865629](https://github.com/vetax-by/xllama/actions/runs/43735865629),
with compile/link/package acceptance only. The prepared signing recipe binds that
exact package and source receipt, current independent guards for all 69 shipping
files, and the unchanged accepted stock payload. The original stock ZIP is fetched
from its existing byte-exact release mirror; it is not rebuilt or repacked. Its
historical BUILD-RECEIPT contains hosted Windows installation paths in the
`msbuild` and `crt_directory` fields; this is retained legacy metadata, not a
claim that those fields were omitted.

Version inputs reserve patched 1.7.0.1506 and stock forward-recovery 1.7.0.1507,
above the freshly observed installed 1.7.0.1504. InstalledVersion remains
FRESH_ACTUAL_REQUIRED in the template. Preparation does not dispatch signing,
create a key, install a package or run a capability query. A later exact selection
must bind the published signing sources, accepted unsigned artifact and fresh
version evidence. Stock recovery, console trust, identity observations, GPU math,
fit and speed remain unproven.


## Configurable single-turn generation successor

Two additive patches preserve the identity and numerical-proof routes and add a dedicated generation surface to the existing opt-in LAN server. A fresh request explicitly selects CPU-only or hybrid mode. Existing chat behavior is unchanged. This surface is buffered single-turn generation; it does not implement streaming, history, tool execution, parallel generations or a new chat UI. It refuses an already resident model instead of unloading it.

The declared source envelope supports one CPU worker, contexts of 64, 128, 256 or 512 tokens, a complete rendered prompt of 1–128 tokens and 1–384 new samples within the selected context. Global expert-cache capacity is 2–512 slots under an explicit owned-memory ceiling and actual native admission. Unsupported worker counts, affinity, asynchronous paging and larger contexts refuse. The immutable reserve and metadata-probe allowance are unchanged. Hybrid mode retains one GPU matrix slot, one outstanding submission and Q4_K/Q6_K selected-expert operators. These are source capabilities, not observed memory fit or performance results.

Both modes require a bounded held permit binding the actual new package/artifact and externally authenticated same-artifact primitive and model-proof acceptances. Hash fields alone do not authenticate those acceptances. No permit or current native acceptance is provided by this source publication; older packages cannot authorize the new code. Each request uses a fresh immutable run identity, exact consumed request/permit hashes and a permanent exclusive claim. Missing, expired or mismatched prerequisites refuse before claim and model access.

The result preserves actual effective settings, sampled token IDs, raw token-piece bytes, UTF-8 status and native termination separately. Only an actual first thinking-close delimiter separates the final suffix; no final answer or closing tag is manufactured. A sample limit inside reasoning is not a completed answer. Status, cooperative stop and publication share a serialized terminal decision. Accepting a stop or losing the HTTP connection does not establish native closure. An unknown fence can retain resources indefinitely; no hard-cancel guarantee is made.

The distinct main record is bounded to 256 KiB; its UA68GEN1 trace is separately bounded to 512 KiB and charges its actual storage to the plan. Collector, serialization, context/cache and GPU allocations must all fit fresh native quota with the existing reserve. Complete trace and main publication require native owner/fence/file/permit release and monitor join. Returned caller closure and origin authentication remain external requirements. Cache and Reader timings are nested application measurements, not physical SSD I/O; GPU timestamps and peak usage are unavailable.

Saved local author checks passed owner/provider syntax and an actual portable parser, registry, collector, trace and serializer fixture with synthetic data and clock. ASan/UBSan ran with leak detection disabled. The preliminary authority chain was used during a superseding selection-message race; the final resource gate refused an existing invocation claim. The exact commands and source match the plan, but the evidence is not relabeled as final-gate admitted. No second compile was run. Windows LAN/pilot/platform code, backend math, model answers and console lifecycle remain untested for this successor.

Windows run 43825061482 later compiled, linked and packaged the 17-patch generation source as an inspected unsigned artifact. It does not contain the subsequent source fixes for the `EnsureModelAsync` return type or the CPU and pinned llama.cpp signedness comparisons. Those followups entered the later 20-patch signed 1508 artifact, after a separate unsigned Windows build and artifact review. The offline decoder and controller packages remain private review dependencies; this public source recipe itself does not provide a live transport, receipt authority or caller-closure proof.

### Trace retrieval and per-run permit successor (source candidate)

The next derived root patch adds `POST /v1/developer/generation/trace` with the existing exact generation-control JSON body. A saved `output.json`, `trace.bin` and a 293-byte `trace.binding` sidecar must agree on canonical run ID, request/config SHA-256, permit SHA-256, exact saved result SHA-256, exact trace SHA-256 and the binary header's contract SHA-256. A complete 517296-byte trace is returned as raw `application/octet-stream` with exact `Content-Length` and `X-Content-SHA256`; missing, changed or mismatched files refuse. Saved partial results may expose their trace for forensics without acquiring normal-result acceptance. The route does not parse the saved JSON with the integer-only request parser; the actual writer can emit fractional rates.

The same patch changes permit storage to the exclusive per-run LocalFolder leaf `ua68-generation-v1.<canonical lowercase 32-hex run_id>.permit.json` (maximum 4096 bytes). It does not fall back to the historical fixed `ua68-generation-v1.permit.json` leaf, overwrite, delete or reuse an old permit. The request and permit wire schemas and retained decoder contract SHA are unchanged; that SHA does **not** assert byte-identical historical storage/admission behavior. Each new artifact needs fresh owner-bound staging, absent preflight, exact GET byte readback and authenticated same-artifact receipts before dispatch. No public source, fixture or hash field issues those receipts. The derived 20-patch source was built as an inspected unsigned Windows artifact, then signed and installed as 1508. Those milestones do not establish generation native acceptance.

A fixed offline usability casepack is authored privately; every model outcome remains unrun. Later device validation must establish same-artifact correctness before usability, stability or performance conclusions. The installed 1508 model2 attempt later closed with a native refusal, so generation remains gated. Velocity/FSR integration stays deferred.

## Model2 proof route successor source candidate

The signed 1508 model2 attempt closed with a native refusal at the first CPU prefill frame: 444 of 456 expected routes; the final block’s three MoE roles were absent. The accepted 20-patch recipe remains the source of that result. The derived 21st patch requests logits on every proof decode, including nonfinal prefill tokens, so the final MoE layer participates in each traced frame. The strict 456-route gate, numerical comparisons, receipt bindings and generation path are unchanged. Host source and synthetic checks support the patch; a new Windows artifact, signed update, fresh same-artifact prerequisites and actual model2 proof are still required. The spent 1508 claim is not retried.
