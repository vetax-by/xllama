# UA50 generation successor signing input

This `workflow_dispatch` source binds the saved unsigned package from Windows
run `43918538956` (head `f22d0573bcc85523d24cdfcc8e6a0bd8da04a9ea`)
and the unchanged accepted stock baseline. It selects installed `1.7.0.1504`,
patched `1.7.0.1508`, and stock forward `1.7.0.1509`. The workflow uses a
disposable Windows runner to sign both copies with one temporary test signer.
It neither builds the app nor contacts a console. Independent signed-artifact
inspection precedes any installation decision.

The four scripts derive from `../` with only the patched source receipt guard
and diagnostic advanced from lock `d586d170...` / 69 postimages to lock
`62437bdc...` / 79 postimages. `package_copy.py` and
`sign_ephemeral_pair.ps1` are byte-identical. The helper changes only
`Identity.Version` in the application payload and checks every other member
byte-for-byte after signing, subject to its bounded SDK CodeIntegrity catalog
rule. The original identity signing files and historical signed artifacts stay
unchanged.

The patched Actions artifact has one-day retention. If it is no longer
downloadable, the workflow refuses; the saved exact unsigned archive can be
transported through a separately reviewed route. Do not rebuild to fill an
expired download or reuse an old signing invocation claim.
