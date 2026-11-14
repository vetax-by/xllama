# Signed update and forward recovery

This manual-only Windows workflow consumes the two accepted unsigned artifacts.
It does not compile native code, download model weights or contact a console.
The exact package and source-receipt SHA-256 values are pinned in the input
template. The selected installed version must come from a fresh console read;
the update version must be greater, and the stock recovery version greater
again. Invalid inputs refuse before certificate creation.

One nonexportable CurrentUser key signs both copies. Only its public certificate
is temporarily added to the disposable runner's trust store. The workflow uses
the existing Windows SDK, verifies both signatures and payloads, then checks
that its own key and trust entry have been removed before exporting the four
public files. No PFX, private key, password or private scratch directory is
uploaded. An abrupt runner termination cannot guarantee cleanup; an incomplete
operation must not be treated as successful or retried automatically.

The output is a patched update, a higher-version stock forward-recovery package,
their shared public CER and a public receipt. Recovery uses the accepted stock
baseline payload, not a reconstruction of the previously installed package.
Neither matching publisher nor successful signing proves console trust, update
success or LocalState compatibility. Keep a verified private backup and qualify
the preserving update/recovery route before installation. Never uninstall or
wipe LocalState as part of this procedure.

The job uses a standard public Windows runner, has a 20-minute job limit and
retains its bounded public output for one day. Dispatch requires a separate
selected attempt; publishing this recipe does not start it. A failed or unknown
operation requires reconciliation, not an automatic retry.

Fixed-name UTC BEGIN/END/FAIL markers identify signing phases without logging
arguments or key identities. The child-process CIM query requests a 15-second
operation timeout; this is not a proven wall-clock deadline. Unknown process or
cleanup state still prevents export. Phase markers diagnose the current attempt;
they do not establish the cause of an earlier timeout.
