# MoE source preparation and unsigned Windows build recipe

The patch rationale, implementation and evidence limits have one home:
[Demand-loaded MoE experts](../docs/ua50-ssd-moe.md).

## Pinned inputs

- xllama: `4db51522c850e62d0bb24234dfeb48e55f4017d7`.
- llama.cpp: `d81235049384534c167caea52b85a694f6103d14`.
- Patch lock SHA-256:
  `5dab2dab1aca23e6d914bd5304ee957f3dc741f57d19457c6ef3b3af3ecf562d`.
- [`PROVENANCE.json`](PROVENANCE.json) records the source-review identities.

The first four patches retain the accepted application/runtime bytes. Two
additional patches carry the explicit console CPU smoke pilot. A seventh root
patch adds a separate bounded first-answer mode; the current lock and
source/import inventories bind all seven. The unsigned build and package-guard commands are unchanged; its public receipt
omits captured absolute tool-installation paths. The workflow enforces LF
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

The existing signing workflow retains the earlier accepted unsigned-artifact
inputs. It must not be reused for this first-answer successor. Its exact
artifact and validator bindings require a separate reviewed update after the
new unsigned artifact has been accepted.

Equivalent commands on a properly provisioned Windows host, from a directory
containing this fork checkout as `operator` and a **separate clean pinned**
upstream checkout as `product`:

```powershell
python operator/ua50/prepare_sources.py product operator/ua50 baseline
& operator/ua50/build_unsigned.ps1 -Source product -Output result-baseline -Track baseline

# Use a second CLEAN pinned product checkout for the patch track.
python operator/ua50/prepare_sources.py product operator/ua50 patch 5dab2dab1aca23e6d914bd5304ee957f3dc741f57d19457c6ef3b3af3ecf562d
& operator/ua50/build_unsigned.ps1 -Source product -Output result-patch -Track patch
```

## What preparation checks

Before git/network operations for the patch track, the operator admits the
exact lock, seven ordered patch files and source/import inventories. Old
standalone runtime locks are refused. It verifies the stock guard, applies
the pinned route once and checks all 47 selected shipping postimages,
including both `.inc` dependencies and the gated Windows CPU-pilot unit, before writing its source receipt.

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
patches and 45 selected source files verified. This is package-build evidence;
the new seven-patch first-answer source still needs its own inspected Windows
build, signed preserving update and console acceptance.

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
