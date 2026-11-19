# Bounded diagnostic successor signing

Selected input: installed 1.7.0.1524, successor 1.7.0.1526, stock forward recovery
1.7.0.1527. Historical 1525 is reserved and is not reused. Fresh exact installed
package/owner/process metadata is bound in the private selection evidence;
metadata does not prove model contents or runtime ownership.

This manual-only workflow consumes accepted unsigned build 44577045317 on source
commit 0553a90b48fc71515396bdf5a947ea3412b13634. Exact package/source/build/shader
receipt hashes are pinned before any key operation. The input guard requires
bounded lock d8bd/136, native manifest48c3/ENGINE3ff AND the historical ee2b/102
baseline composition. Independent unsigned-artifact QA result is
`d28ea44752412a023083f9d27840f89ebf1d5cb09851a9a81e3df72bbe6c3cda`.
The accepted stock archive/payload is reused byte-exactly, without rebuilding.

Packaging, nonexportable CurrentUser key creation, temporary public-only runner
trust, signature verification and exact cleanup/export implementation are reused
unchanged from the established successor recipe. Only exact source/artifact
inputs and version allocation change. No PFX/private key/password is exported.
Missing input, partial/unknown signing or cleanup refuses public output and is
not retried automatically. Forward recovery is prepared, not automatically
installed. Signing performs no console access or automatic diagnostic/model run.

One independent signed-pair artifact QA must check signatures, payload invariance,
version/certificate/package hashes and cleanup before the selected preserving
in-place install through the sole device provider. No uninstall/reset/data or
weights overwrite/transfer is selected. Old 1524 identity/primitive/model2 receipts
do not become new-artifact acceptance. Source association is not runtime binary
measurement. Windows compile success and signing are separate from target FP,
GPU/PSO, active cvec, peak memory, observer neutrality and science. Interventions
remain disabled. See [diagnostic scope](../../../docs/bounded-diagnostics.md).
