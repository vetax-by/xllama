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
answer quality. The new profile has narrow portable fake-boundary functional
and source/shipping evidence; its own Windows build, signed preserving update
and console generation remain pending. It does not implement a larger expert
cache, GPU execution or automatic continuation beyond the sample cap.

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
384-sample successor still requires its own build, update and output acceptance.
Build warnings are retained; this is not a warning-free build or proof of runtime
correctness.

| Area                          | What has been checked                                                                                                                         | What it does not establish                                                                                           |
| ----------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------- |
| Native CPU integration        | Actual LFM2MoE factory/context/decode on generated GGUF fixtures, resident/cache logits, routes, mixed-quant slice ledgers and fault handling | Real checkpoint parity, useful assistant quality or Xbox performance                                                 |
| Real 8B checkpoint            | One successful native prompt and next-token step using streamed experts                                                                       | Two-session reproducibility, resident-reference parity, sustained speed or context quality                           |
| Real 24B checkpoint           | Bounded CPU smoke and generated reasoning with source-qualified held-file integrity and normal teardown                              | Completed final assistant answer, real resident-reference parity, sustained speed, long context or assistant quality              |
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
D3D12 results do not prove routed expert support: external `MUL_MAT_ID` GPU
execution is not implemented by this patch. Logical reader bytes are not
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

[`ua50/PATCH-LOCK.json`](../ua50/PATCH-LOCK.json) is the authority for eight
ordered patches. The route on a separate clean base tree is: stock AppContainer
guard once, runtime root patch, runtime llama patch, application root patch,
application llama patch, CPU pilot root patch, CPU pilot llama patch, the
first-answer root patch and finally one finished-answer root patch. Do not additionally apply
intermediate development patches or apply a cumulative patch twice.

The two new `.inc` files are shipping include dependencies of `session.cpp`,
not additional translation units. The first-answer record/template add two
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
