# MoE source preparation and unsigned Windows build recipe

The patch rationale, implementation and evidence limits have one home:
[Demand-loaded MoE experts](../docs/ua50-ssd-moe.md).

## Pinned inputs

- xllama: `4db51522c850e62d0bb24234dfeb48e55f4017d7`.
- llama.cpp: `d81235049384534c167caea52b85a694f6103d14`.
- Patch lock SHA-256:
  `f11dd6bb7234fd62d3a12e773f8369a81f66f5412016e3516e603caedc99123e`.
- [`PROVENANCE.json`](PROVENANCE.json) records the source-review identities.

The first four patches retain the accepted application/runtime bytes. Two
additional patches carry the explicit console CPU smoke pilot. The seventh root
patch retains the bounded first-answer mode. An eighth root patch adds a separate
384-sample finished-answer profile. Two further patches add cache A/B developer
profiles and their shared instrumentation; the current lock and inventories bind
the ten historical entries. Two further configurable-runner patches add a
strict typed developer entrypoint, explicit successor records and bounded I/O
aggregates; the current twelve-entry lock and inventories bind 51 selected
files. Named cache presets resolve to complete configs on the same CPU engine. The unsigned build and
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
and retains explicitly selected unsigned artifacts for one day. Actions is
disabled at initial publication; publishing this recipe does not run it.

The workflow checks out this fork as the operator and a separate clean pinned
upstream tree as the product. This separation is important: the application
sources visible on this fork branch already include our root changes and must
not receive those patches again.

A successful, inspected pinned baseline must precede a patched build. The
unchanged accepted baseline artifact can be reused; the new patched source
requires its own build and inspection with the exact lock above. Do not
automatically retry, retarget a missing SDK, sign or install a package.

The signing recipe binds the inspected configurable-runner artifact and the
reusable stock baseline. Both the Python preflight and independent PowerShell
copy helper require the current twelve-patch lock and 51 selected files.
The workflow pins both helpers and the exact artifact template; it retains the
manual-only trigger, ephemeral non-exportable signing identity and owned cleanup.
A signing attempt still needs an exact separately selected artifact/version-bound
disposition, fresh installed-version facts, a higher patched version and a still
higher stock forward-recovery version. Signing does not install or launch the
application, and a recovery package alone does not prove compatibility.

Equivalent commands on a properly provisioned Windows host, from a directory
containing this fork checkout as `operator` and a **separate clean pinned**
upstream checkout as `product`:

```powershell
python operator/ua50/prepare_sources.py product operator/ua50 baseline
& operator/ua50/build_unsigned.ps1 -Source product -Output result-baseline -Track baseline

# Use a second CLEAN pinned product checkout for the patch track.
python operator/ua50/prepare_sources.py product operator/ua50 patch f11dd6bb7234fd62d3a12e773f8369a81f66f5412016e3516e603caedc99123e
& operator/ua50/build_unsigned.ps1 -Source product -Output result-patch -Track patch
```

## What preparation checks

Before git/network operations for the patch track, the operator admits the
exact lock, twelve ordered patch files and source/import inventories. Old
standalone runtime locks are refused. It verifies the stock guard, applies
the pinned route once and checks all 51 selected shipping postimages,
including both `.inc` dependencies and the gated Windows CPU-pilot unit, before
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
console A/B measurements. The [configurable replacement Windows build](https://github.com/vetax-by/xllama/actions/runs/43680931110)
now produced its own inspected unsigned package after a narrow header-resolution
correction. Signing and preserving update remain pending; native CPU configuration
qualification follows as a separate task. No private response or telemetry is included here.

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
