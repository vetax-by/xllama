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

The current pilot has compiled and linked on Windows and produced an inspected
unsigned x64 package. Actual quota/buffer planning, signed recoverable
installation, real I/O and console execution are still pending. The flag is not
an instruction to bypass those checks.

## Evidence and limits

The [original pinned Windows baseline build](https://github.com/vetax-by/xllama/actions/runs/43592901046)
has completed successfully and produced an inspected unsigned x64 package. That
baseline includes the stock AppContainer guard and **none of our six patches**.
The [current CPU-pilot build](https://github.com/vetax-by/xllama/actions/runs/43594526711)
also completed successfully, applying all six locked patches and verifying all
45 selected source files. Its unsigned x64 package is 21,051,447 bytes, SHA-256
`35d0ad3b12ae203e37cfaa69577ba8cd34709d3626b4efc41e755260f08854ae`.
The actual executable imports the required BCrypt and overlapped-file APIs.
Successful compilation and linking do not prove that those APIs, native memory
planning or model decoding work on the console. Both packages are version
`1.7.0.0`; signing and an explicitly higher update version remain required
before installation.

| Area                          | What has been checked                                                                                                                         | What it does not establish                                                                                           |
| ----------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------- |
| Native CPU integration        | Actual LFM2MoE factory/context/decode on generated GGUF fixtures, resident/cache logits, routes, mixed-quant slice ledgers and fault handling | Real checkpoint parity, useful assistant quality or Xbox performance                                                 |
| Real 8B checkpoint            | One successful native prompt and next-token step using streamed experts                                                                       | Two-session reproducibility, resident-reference parity, sustained speed or context quality                           |
| Real 24B checkpoint           | Complete acquisition and held-file integrity checks                                                                                           | Native decode: no 24B model process successfully started; the latest attempt was refused by its entry pressure guard |
| Portable reader core          | Nine fake-backend test groups                                                                                                                 | Windows API behavior or real device cancellation                                                                     |
| Session hub and adapter/owner | Actual hub tests and five functional adapter/owner groups with ASan/UBSan; named native/platform boundaries are fakes                         | Native math, production Windows I/O or BCrypt execution                                                              |
| Shipping preparation          | Exact patch order, selected postimages, registration and source-admission checks                                                              | Actual installation or runtime behavior on the console                                                               |

Windows build evidence includes SDK/dependency resolution, MSBuild compilation,
linking and unsigned packaging for both tracks. It is separate from the earlier
Linux checkpoint tests and their refused attempts.

The 24B refusal snapshot at the exact pressure-trigger instant was not retained.
Later resource data cannot identify its precise ancestor/value, attribute it to
the model, or establish OOM or model fit. The refused attempt is not relabeled
as a model correctness failure.

No modified MoE build has been installed or measured on Xbox. Dense-model
D3D12 results do not prove routed expert support: external `MUL_MAT_ID` GPU
execution is not implemented by this patch. Logical reader bytes are not
physical SSD traffic; process RSS is not the full memory cost of OS page cache,
KV, activations, UI and backend buffers.

The bounded developer pilot supplies built-in checkpoint/path binding, writer
ownership, quota/planning and watchdog/status callbacks in source. Their actual
Windows behavior still requires validation. General catalogue activation and a
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

[`ua50/PATCH-LOCK.json`](../ua50/PATCH-LOCK.json) is the authority for six
ordered patches. The route on a separate clean base tree is: stock AppContainer
guard once, runtime root patch, runtime llama patch, application root patch,
application llama patch, CPU pilot root patch and CPU pilot llama patch. Do not additionally apply
intermediate development patches or apply a cumulative patch twice.

The two new `.inc` files are shipping include dependencies of `session.cpp`,
not additional translation units. Test doubles, local fixtures, Linux-only
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
