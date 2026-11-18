# MoE source preparation and unsigned Windows build recipe

The patch rationale, implementation and evidence limits have one home:
[Demand-loaded MoE experts](../docs/ua50-ssd-moe.md).

## Pinned inputs

- xllama: `4db51522c850e62d0bb24234dfeb48e55f4017d7`.
- llama.cpp: `d81235049384534c167caea52b85a694f6103d14`.
- Current 31-patch runtime ingress successor source lock SHA-256:
  `147016a8c2dd08487186467548cd6e9daac0db61580082f28698885d52a0c061`.
- The prior 17-patch lock `921fc2a5d9724fc28851146266fc67b7627e54a45c8a10775f30c52eb2a20ebe`
  remains the binding for Windows run 43825061482; that build did not contain the followup fixes.
- [`PROVENANCE.json`](PROVENANCE.json) records the source-review identities.

The first four patches retain the accepted application/runtime bytes. Two
additional patches carry the explicit console CPU smoke pilot. The seventh root
patch retains the bounded first-answer mode. An eighth root patch adds a separate
384-sample finished-answer profile. Two further patches add cache A/B developer
profiles and their shared instrumentation; the current lock and inventories bind
the ten historical entries. Two further configurable-runner patches add a
strict typed developer entrypoint, explicit successor records and bounded I/O
aggregates; those twelve entries are retained. Two additional root/native GPU bridge patches
bring the historical GPU route to fourteen entries. One further root identity patch
brings that historical route to fifteen entries and 69 selected shipping files. Two generation patches brought the historical route to seventeen entries and 77 selected shipping files. Two source-only followup patches then brought the recipe to nineteen entries and 79 selected postimages: the root patch fixes the `EnsureModelAsync` return type and signed prompt-cap comparison, and the pinned llama.cpp patch fixes the unsigned layer comparison. A twentieth patch adds byte-safe trace retrieval and per-run permit storage. The twenty-first patch requests logits on every model2 proof decode so final-layer MoE routes remain observable on nonfinal prefill tokens. Signed 1510 then passed the 456-route gate but closed with native numerical and route mismatch; that run is spent and model2 remains unaccepted. A twenty-second root patch adds a bounded model2-only layer-output witness and optional SHA-bound diagnostic sidecar. It retains the accepted main result schema, the strict 456-route and numerical gates, and all earlier patch bytes. That 22-patch source produced the saved unsigned run 43988466126 and signed 1512. Signed 1512 model2 closed with a numerical refusal; its saved optional layer sidecar localized an early tolerance crossing, but did not establish a causal instruction. The run is spent and model2 remains unaccepted. Two further root/native patches add an optional first-position 12-vector diagnostic using the same held hybrid Q8 input and weight lease, a CPU vec_dot oracle, and post-fence GPU output. Diagnostic failure latches sidecar unavailable while core GPU publication and prior proof gates retain their behavior. That 24-patch source produced signed 1514; its model2 run closed with numerical refusal and its claim is spent. Patches 25 and 26 added configurable model2 diagnostics, selecting 84 postimages and 718 direct includes; they produced signed 1516, whose two model2 runs closed with numerical refusal. Patches 27 and 28 extended graph and cross-arm diagnostics, selecting 90 postimages and 760 direct includes. That 28-patch source was built and signed as 1518; its model2 runs closed with numerical refusal and spent claims. The held-weight pair added patches 29 and 30, keeping 90 postimages with 761 direct includes; that source was built and signed as 1520, whose identity run refused before model work. Patch 31 adds runtime ingress and selects 96 postimages with 801 direct include records. It has source/host evidence only, without a Windows or native result for this new source. Named cache presets resolve to complete configs on the same CPU engine. The unsigned build and
package-guard commands are unchanged; the public receipt omits captured absolute tool-installation paths. The workflow enforces LF
checkout before both source checkouts. `prepare_sources.py` explicitly selects
Git for Windows Bash instead of the WSL stub; it refuses a missing or unexpected
installation before submodule/network operations. These operator-only corrections
and their reviews are listed in `PROVENANCE.json`. No WSL installation is required.
`DEPENDENCIES.json` retains dependency versions and public source identities;
captured tool-installation-path excerpts are omitted from its public summary.

## Manual Windows workflow

[`ua50-uwp.yml`](../.github/workflows/ua50-uwp.yml) has only a manual
`workflow_dispatch` trigger, uses `windows-2022`, limits each job to 45 minutes,
and retains explicitly selected unsigned artifacts for one day. Publishing
this recipe does not dispatch a build.

The workflow checks out this fork as the operator and a separate clean pinned
upstream tree as the product. This separation is important: the application
sources visible on this fork branch already include our root changes and must
not receive those patches again.

A successful, inspected pinned baseline must precede a patched build. The
unchanged accepted baseline artifact can be reused; the new patched source
requires its own build and inspection with the exact lock above. Do not
automatically retry, retarget a missing SDK, sign or install a package.

Existing successor signing receipts bind earlier artifacts, including signed 1520. This new 31-patch source cannot pass those old exact receipt guards. A future signed successor requires a new unsigned artifact, independent artifact QA and a derived signing input. This preparation does not select or sign a package.

Equivalent commands on a properly provisioned Windows host, from a directory
containing this fork checkout as `operator` and a **separate clean pinned**
upstream checkout as `product`:

```powershell
python operator/ua50/prepare_sources.py product operator/ua50 baseline
& operator/ua50/build_unsigned.ps1 -Source product -Output result-baseline -Track baseline

# Use a second CLEAN pinned product checkout for the patch track.
python operator/ua50/prepare_sources.py product operator/ua50 patch 8769d6d7337fd15baf0793fbf25dba12a7af889751e8056c3b86230b8d45ed4e
& operator/ua50/build_unsigned.ps1 -Source product -Output result-patch -Track patch
```

## What preparation checks

Before git/network operations for the patch track, the operator admits the
exact lock, thirty ordered patch files and source/import inventories. Old
standalone runtime locks are refused. It verifies the stock guard, applies
the pinned route once and checks all 90 selected shipping postimages,
including shipping `.inc` dependencies and the gated Windows CPU-pilot unit, before
writing its source receipt.

The helper rejects weight and private-key-like files in the product tree,
except for nineteen exact tensor-free tokenizer fixtures and one exact Keynote
document tracked by the pinned upstream llama.cpp revision. Those source-only
exceptions require matching HEAD, tree, index, path, file type, size, Git blob
and full-content hash; modified or additional files are refused. The unsigned
package guard still forbids all model weights and private-key-like members.
Path probes and retained content handles use the same metadata API, preserving
exact identity and timestamps; refusal diagnostics report each comparison phase.
Dependency versions and vendor DLL hashes are pinned in the recipe. Runner
images remain rolling, and restored transitive SDK/CRT/package bytes are not a
pre-existing cryptographic dependency closure.

Source inventories and mocked preparation tests do not prove Windows import
availability, a successful compile/link, installability, Xbox memory fit or
assistant quality. An unsigned package is not an installed or trusted package.
No model weights, test signing keys, local telemetry or device secrets are
needed in this repository.

The earlier six-patch [CPU-pilot Windows build](https://github.com/vetax-by/xllama/actions/runs/43594526711)
compiled, linked and produced an inspected unsigned x64 package with all six
patches and 45 selected source files verified. The separate
[first-answer Windows build](https://github.com/vetax-by/xllama/actions/runs/43627054490)
also produced an inspected unsigned x64 package with all seven patches and 47
selected source files verified. That earlier profile subsequently produced a
bounded console generation and normal teardown; its sample cap ended inside
thinking, so it did not provide a completed final answer. The distinct
[eight-patch finished-answer Windows build](https://github.com/vetax-by/xllama/actions/runs/43645815389)
has now compiled, linked and produced an inspected unsigned package with all
47 selected source files verified. That profile subsequently completed a
signed preserving update and one bounded console generation with a genuine
final section, native termination and normal teardown. This is a single-turn
milestone, not assistant-quality, long-context or sustained-performance
acceptance. The [ten-patch cache Windows build](https://github.com/vetax-by/xllama/actions/runs/43658336759)
also produced an inspected unsigned package, without a cache-profile update or
console A/B measurements. The configurable CPU successor subsequently completed a separately reviewed
Windows artifact, signed preserving update and bounded CPU configuration cases.
Those outcomes belong to the historical CPU revision. The historical GPU bridge
subsequently completed compile, signing and a preserving update; no GPU execution
was established. The identity successor subsequently produced a separately inspected Windows artifact and signed pair; preserving update and actual identity observation remain incomplete before native correctness checks. No private response or telemetry is included here.

## Bounded first-answer developer mode

The additional first-answer patch keeps the earlier smoke path and default UI/LAN
behavior. A separate explicit local flag selects one fixed short Russian greeting,
source-faithful LFM24 chat rendering and greedy generation with a maximum of 128
samples in a context of at most 512 tokens. The whole prompt must fit without
trimming. This is a developer check, not a catalogue or general assistant release.

Private bounded output retains sampled IDs and raw token-piece bytes separately
from the seeded assistant prefix, UTF-8 validity, stop reason and monotonic timing.
EOS/EOT and length limits are distinct; a cap inside thinking is not a completed
final answer. Successful output additionally requires held-file integrity,
normal owner/file teardown, exact output commit and observed process termination.
Hash/open/load timing is separate from the short native generation trace.

New native claim/output files are exclusive; earlier smoke claims and results
remain untouched. Installation must preserve application state and model files.
No private console logs, generated responses, device credentials or signing keys
are publication inputs. See the main patch document for evidence boundaries.

## Separate finished-answer candidate

The eighth patch preserves the existing 128-sample mode and adds a distinct
`ua50-cpu-lfm24-finished-answer-v2` LocalFolder prefix with `.flag`, `.claim`,
`.stop` and `.output.json` leaves. The new fixed profile allows up to 384 sampled
tokens, including reasoning, in a context of at most 512. The tokenized prompt is
limited to 128 and the entire prompt plus generation allowance must fit the
resolved context; no prompt trimming or automatic extension is allowed.

The same single-user prompt, checkpoint template, tokenizer-owned BOS, CPU-only
thread, nonfused operations and two-slot expert cache are retained. Collector and
serialization bookkeeping is explicitly charged to the joint memory plan; the
runtime reserve and metadata-probe allowance remain unchanged. This profile does
not implement the separately considered larger-cache optimization.

The private record retains the same schema and raw-piece/timing rules with the
new scope and limits. A terminated final text, a length-capped text after
thinking and reasoning that never reaches a final section are different
outcomes. No closing tag or final answer is manufactured. Existing per-piece,
aggregate-text and JSON byte caps remain strict; oversized serialization refuses
before writing rather than clipping output. Normal runtime completion alone is
not assistant-quality acceptance, and 384 samples do not guarantee a final
answer. Old claims, outputs and ordinary application behavior remain intact.

## Cache A/B developer candidate

The ninth root and tenth native patches preserve all previous modes and add
separate `ua50-cpu-lfm24-cache-a2-v1` and `ua50-cpu-lfm24-cache-b512-v1`
LocalFolder prefixes. Each has its own flag, persistent claim, cooperative stop,
private JSON output and binary trace. Both use the same checkpoint, prompt,
template, sampler, CPU-only thread, context and sample cap. They use respectively
two and 512 global expert-matrix slots; the larger capacity remains conditional
on actual native sizing, the unchanged memory reserve and fresh quota admission.

Both profiles record every cache acquisition, including hits, in a bounded
ordered trace with decode boundaries, actual role lengths and separate request
and byte counters. Cache-acquire and Reader-call elapsed times are nested
measurements, not physical SSD traffic or pure OS wait. Trace memory is included
in the joint plan. Complete trace write/flush/close precedes the main output;
partial, failed or overflowed records cannot establish a complete comparison.
A normal, closed A run with a complete validated pair must precede fresh B
admission. No automatic retry, claim reset or cache-performance benefit is
asserted. Source and synthetic checks do not prove the new Windows build or
console behavior.

## Configurable successor

The developer config, one-use ticket, capabilities, requested/effective results
and storage-measurement boundaries are described in the canonical
[configurable runner section](../docs/ua50-ssd-moe.md#configurable-developer-runner).
The historical flags remain separate. Only a supported typed config is
accepted; no config can enable an unimplemented GPU, UI or history path.

## Synchronous selected-expert GPU proof candidate

The current addition is an explicit CPU-graph/D3D12 expert bridge for Q4_K and
Q6_K matrices. Routing and activation packing stay in the CPU graph. One GPU
matrix slot and one outstanding submission retain their host leases until the
actual fence completes. It does not advertise general routed-operator support
or silently fall back to CPU. Current proof configs retain one CPU worker and
two host-cache slots; worker/cache performance sweeps are not implemented here.

Two separate developer stages cover sixteen primitive vectors and then two
model output rows. The latter requires an independently accepted primitive
result for the same inspected artifact, shader and adapter. Actual package,
shader, adapter, quota and prerequisite bindings are required before a run.
The GPU config, ticket, result and trace use explicit successor versions;
historical CPU records and validators remain unchanged.

Numerical checks compare all selected elements online with a fixed tolerance
and bounded summary/witnesses. Full rows are not retained, so the saved summary
cannot independently reproduce every numerical error. The proof allocation is
charged separately from the collector, native plan and GPU resources. Shared
UMA is not additional memory; reserve and probe protections remain unchanged.
Unknown fences retain resources until a real terminal state. Unavailable GPU
compute timestamps remain null.

Source review and portable synthetic author checks passed. Windows/pilot/native
quant kernels, shaders and console execution have not yet been accepted for
this revision. There is no GPU correctness, fit, speed or model-quality claim.
Correctness and lifecycle checks precede stability/usability and later performance
work. No Velocity/DirectStorage or FSR integration is included.


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

A separate selected signing run produced patched 1.7.0.1506 and stock forward-recovery 1.7.0.1507; the saved pair passed independent artifact inspection. Those packages remain retained and uninstalled. Their templates and accepted historical source bindings apply to that exact identity revision. The generation source has a separate inspected unsigned artifact from run 43825061482; any later signing must bind that artifact independently. Stock recovery, console trust, native identity observations, GPU math, fit and speed remain unproven.


## Configurable single-turn generation successor

The historical two-patch generation addition exposes a separate bounded developer surface in the existing optional LAN server. The normal chat route is unchanged. See [the generation contract and evidence limits](../docs/ua50-ssd-moe.md#configurable-single-turn-generation-successor) before using it. Supported source branches do not establish a working console release.

The historical saved local author check completed two syntax checks and one portable fixture compile/run under ASan and UBSan. It did not compile the Windows LAN, pilot or native GPU branches. The executed commands match the Root plan, but a selection-message race meant the preliminary authority chain was used; the superseding final resource gate refused the existing claim. This is qualified saved author evidence, with no repeat run or retroactive final-gate admission. The 17-patch source review passed. [Windows run 43825061482](https://github.com/vetax-by/xllama/actions/runs/43825061482) subsequently passed compile, link and unsigned packaging; independent saved-artifact review passed for that exact 17-patch package. The two followup patches were later built in the signed 1508 and 1510 lineages; those artifacts do not include the later model2 witness patches. Neither artifact review nor source verification establishes native generation, GPU correctness, model usability, memory fit or speed.

The earlier identity revision did produce an inspected unsigned Windows artifact and a separately inspected signed 1506/1507 pair. Those retained packages were not installed; no native identity query or GPU numerical proof was accepted. That historical artifact does not contain these generation changes.

The derived twentieth root patch provides bounded binary trace retrieval and a fresh per-run permit leaf. It changes storage/admission behavior while retaining the generation wire schema and decoder contract hash; see [trace and permit successor](../docs/ua50-ssd-moe.md#trace-retrieval-and-per-run-permit-successor-source-candidate). It is absent from historical run 43825061482, but was later included in signed 1508 and 1510. A private receipt verifier, owner-bound permit stager and caller closure are required for later device runs; they are not part of this public recipe.

## Bounded model2 layer-output witness source candidate

The twenty-second patch adds an opt-in model2-only callback for the first three positions and forty named `l_out` layers. It retains a fixed CPU reference buffer, compares hybrid outputs immediately, and fails closed on wrong order, shape, stride, missing rows or callback failure even if the scheduler reports success. Memory is separately charged to the model2 plan. The existing primitive16 and generation paths, held SHA, 456-route, numerical and terminal gates are unchanged.

The accepted `ua68-gpu-proof-output-v1` main JSON remains byte-compatible. An optional `layer-witness.json` diagnostic sidecar carries run/config/ticket identity plus SHA-256 of the exact saved main and trace bytes. Missing or invalid diagnostic bytes cannot become a proof receipt. No raw activations or weights are written to the sidecar. The 22-patch source was built and signed as 1512. Its model2 attempt closed with native numerical refusal; the saved diagnostic sidecar localized observed differences without proving their cause. That run is spent. The later 24-patch source was built and signed as 1514; its model2 run also closed with native numerical refusal and is spent.

## Bounded first vector diagnostic source candidate

Patches 23 and 24 observe only the first twelve ordered model2 hybrid expert vectors at position zero. Gate/up use Q4_K with k=2048,n=1536; down uses Q6_K with k=1536,n=2048. The live route supplies expert IDs. The CPU oracle reads the same held hybrid Q8 input and weight lease; the comparison reads the completed GPU output after its terminal fence. The optional sidecar contains bounded metrics and digests, not raw tensors or weights, and binds the exact saved main and trace bytes. Its native identifiers are claims; signed artifact and source-lock authority still require independent external evidence. An absent or invalid sidecar never becomes a model2 receipt. The existing main schema, held SHA, 456-route, numerical and terminal gates remain unchanged. Host synthetic checks do not prove the shipping DXIL, actual model parity, or future device eligibility.

## Configurable model2 diagnostic source candidate

Patches 25 and 26 add a strict versioned model2 CONFIG with one embedded `diagnostics` object. Legacy CONFIG v1 runs with diagnostics off. Broad-first `l_out` CPU/hybrid overview, deterministic workload shards and selected same-held hybrid expert-vector oracles share this source. The available controls, hard ceilings, coverage semantics and unsupported hooks are listed in [the diagnostic contract](../docs/ua50-ssd-moe.md#configurable-model2-diagnostics-source-candidate). The optional sidecar binds raw main/trace bytes but is not artifact authority or proof acceptance. The 26-patch source was built and signed as 1516; its two model2 runs closed with native numerical refusal and spent claims. Any future installed successor remains blocked from app launch until exact installed version and full PFN GET readback, then a later explicit Stan Game confirmation for that pair.

## Consolidated model2 diagnostic successor source candidate

Patches 27 and 28 add strict CONFIG v3 selectors for graph and cross-arm observations while retaining the v1/v2 paths. Graph observation covers a source-bound catalog of router, expert, MoE combination, residual, layer and final-output boundaries. Cross-arm observation copies selected CPU-only and hybrid F32 prequant, full packed Q8_K blocks and returned expert values at the common CPU pack barrier. Same-run semantic pairing uses token, position, layer, role, expert, selected slot and row identity. Raw private replay is bounded; no weights or raw activation arrays enter public artifacts. A missing point, incompatible shape, unmatched arm or budget limit is reported as a gap. Hashes and finite summaries are indicators, not numerical parity.

Six selectable host profile intents (B0, B1, D0, D1, D2 and R0) use the same source bytes. B1 requires explicit layer shards under the hard CONFIG, work, memory and sidecar limits. The optional diagnostic path catches observer faults separately from required legacy l_out and GPU proof work. The main held SHA, 456-route, numerical, normal and closure gates remain unchanged. Signed 1516's broad and p5 detail model2 runs both closed with numerical refusal; their claims are spent. The 28-patch successor was built and signed as 1518; its model2 runs refused and claims are spent. Any future installed package requires exact version and full PFN GET readback, followed by Stan's later explicit Game confirmation for that pair before any app launch.

## Held-weight cross-input successor source candidate

Patches 29 and 30 add a strict CONFIG v4 `held_weight` choice on top of the accepted graph and cross-arm controls. The exact semantic selection is runtime-configurable and requires a selected cross-arm row. Under the authentic hybrid lease, optional CPU dots compare one held weight with copied CPU and hybrid Q8 inputs; C/X/H/G diagnostic summaries and explicit gaps are bound to raw CONFIG, ticket, main and trace. No source tuple or weight bytes are hardcoded into public artifacts. Up to eight summary rows or one private raw row share the fixed diagnostic and native headroom ceilings; two dots cost 6,291,456 terms per row, so the proposed 4M increment is not admitted. Unknown or unsupported controls refuse. Legacy CONFIG and main proof gates stay unchanged.

The 30-patch source was built and signed as 1520. Its identity attempt closed with a preclaim refusal of unknown internal deadline cause; that run and fixed ticket are spent. No primitive or model2 receipt transfers from 1518.

## Runtime ingress successor source candidate

Patch 31 adds one bounded per-run headless scanner for identity, proof and CPU dev. Exact intent/window/ticket bytes bind a fresh finite selection. Attempt/output/terminal evidence distinguishes closed spent from orphan; uncertain state refuses and is never replayed or automatically cleaned. Identity v2 ticket and result filenames have separate canonical prefixes. Historical fixed tickets remain intact, and LAN generation keeps explicit POST and retained permit.

A bounded reason envelope records stage-specific stop predicates, first observed stop, clock and quota availability without changing native proof gates. A separately selected protected UI intent is claimed and latched before XAML and suppresses late experimental flag consumers. Its close needs external independent issuer, selected-run and chronology checks; staged file contents alone do not establish those facts or exclude another writer. Source/host checks do not establish Windows handle sharing, activation races, native memory peak or runtime acceptance. A future artifact needs Windows build and independent QA, signing, preserving install, exact version/full-PFN GET, then Stan's later Game/manual launch-and-exit report before app/native launch. Identity, primitive and model2 remain fresh same-artifact stages.
