# UA50: demand-loaded MoE experts for xllama

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

## Evidence and limits

| Area                          | What has been checked                                                                                                                         | What it does not establish                                                                                           |
| ----------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------- |
| Native CPU integration        | Actual LFM2MoE factory/context/decode on generated GGUF fixtures, resident/cache logits, routes, mixed-quant slice ledgers and fault handling | Real checkpoint parity, useful assistant quality or Xbox performance                                                 |
| Real 8B checkpoint            | One successful native prompt and next-token step using streamed experts                                                                       | Two-session reproducibility, resident-reference parity, sustained speed or context quality                           |
| Real 24B checkpoint           | Complete acquisition and held-file integrity checks                                                                                           | Native decode: no 24B model process successfully started; the latest attempt was refused by its entry pressure guard |
| Portable reader core          | Nine fake-backend test groups                                                                                                                 | Windows API/import compatibility or real device cancellation                                                         |
| Session hub and adapter/owner | Actual hub tests and five functional adapter/owner groups with ASan/UBSan; named native/platform boundaries are fakes                         | Native math, production Windows I/O, BCrypt execution or a complete Windows application build                        |
| Shipping preparation          | Exact patch order, selected postimages, registration and source-admission checks                                                              | Resolved Windows SDK/dependencies, successful MSBuild/link/package or installation                                   |

The 24B refusal snapshot at the exact pressure-trigger instant was not retained.
Later resource data cannot identify its precise ancestor/value, attribute it to
the model, or establish OOM or model fit. The refused attempt is not relabeled
as a model correctness failure.

No modified MoE build has been installed or measured on Xbox. Dense-model
D3D12 results do not prove routed expert support: external `MUL_MAT_ID` GPU
execution is not implemented by this patch. Logical reader bytes are not
physical SSD traffic; process RSS is not the full memory cost of OS page cache,
KV, activations, UI and backend buffers.

Trusted catalogue/model-path binding, application writer policy, actual joint
memory/quota admission and watchdog/status/progress integration remain required
before usable application activation. Missing admission callbacks refuse the
internal opt-in. There is no new public UI/LAN toggle enabling this mode.

## Source layout and reproducible preparation

The reviewed base is xllama commit
`4db51522c850e62d0bb24234dfeb48e55f4017d7` and llama.cpp commit
`d81235049384534c167caea52b85a694f6103d14`.

The application changes are visible in this branch. The llama.cpp submodule
gitlink remains upstream-pinned; its modifications are carried as checked
patches, not committed inside the submodule. Use the preparation recipe below
instead of treating the raw fork checkout as an already prepared build tree.

[`ua50/PATCH-LOCK.json`](../ua50/PATCH-LOCK.json) is the authority for the four
ordered runtime/application patches. The route on a separate clean base tree
is: stock AppContainer guard once, runtime root patch, runtime llama patch,
application root patch, application llama patch. Do not additionally apply
intermediate development patches or apply a cumulative patch twice.

The two new `.inc` files are shipping include dependencies of `session.cpp`,
not additional translation units. Test doubles, local fixtures, Linux-only
reader adapters and reference overlays are excluded from the shipping set.

See [`ua50/README.md`](../ua50/README.md) for the reviewed unsigned preparation
recipe, selected source inventories and the manual-only Windows workflow.
Baseline and patched builds use separate clean copies of the pinned upstream
source. The workflow has no push/pull-request trigger. At initial publication,
Actions is disabled for this fork and no build has been dispatched.

## License and relationship to upstream

xllama's MIT license and copyright notice are retained in [`LICENSE`](../LICENSE).
The fork's additions use the same MIT license. This is an independent
experimental fork, not an upstream xllama release or endorsement. Model weights
and third-party binary dependencies have their own licenses; no weights,
private signing keys or local device credentials are added by this publication.
