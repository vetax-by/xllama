# SOURCE ONLY: no invocation, trust change or key operation performed in preparation.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$InputPlan,
    [Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{64}$')][string]$InputPlanSha256,
    [Parameter(Mandatory)][string]$ArtifactRoot,
    [Parameter(Mandatory)][string]$PrivateRoot,
    [Parameter(Mandatory)][string]$Output,
    [Parameter(Mandatory)][string]$SdkBin,
    [ValidateSet('LocalMachine')][string]$PublicTrustScope
)
$ErrorActionPreference = 'Stop'
# Diagnostics are best-effort: no validation/host/stack exception escapes into cleanup.
$script:PhaseStack = $null
try { $script:PhaseStack = [Collections.Generic.Stack[string]]::new() } catch { }
function Write-SigningPhase([string]$State, [string]$Phase) {
    try {
        if ($State -notin @('BEGIN','END') -or $Phase -cnotmatch '^[A-Za-z0-9_.-]{1,80}$') { return }
        if ($State -eq 'BEGIN' -and $null -ne $script:PhaseStack) { $script:PhaseStack.Push($Phase) }
        Write-Host ('[UA50_SIGN_PHASE] {0} {1} {2}' -f [DateTime]::UtcNow.ToString('o'),$State,$Phase)
        if ($State -eq 'END' -and $null -ne $script:PhaseStack -and $script:PhaseStack.Count -and $script:PhaseStack.Peek() -eq $Phase) { [void]$script:PhaseStack.Pop() }
    } catch { } # Logging failure must not skip cleanup or change operational outcome.
}
function Write-SigningFailure {
    try {
        $label = if ($null -ne $script:PhaseStack -and $script:PhaseStack.Count) { $script:PhaseStack.Peek() } else { 'UNSCOPED' }
        Write-Host ('[UA50_SIGN_PHASE] {0} FAIL {1}' -f [DateTime]::UtcNow.ToString('o'),$label)
        if ($null -ne $script:PhaseStack) { $script:PhaseStack.Clear() }
    } catch { } # No exception/error text is logged, including a logger's own error.
}
try {
Write-SigningPhase BEGIN 'PAIR_PREFLIGHT'
if ($env:OS -ne 'Windows_NT') { throw 'selected disposable Windows lane required' }
Write-SigningPhase BEGIN 'HOSTED_ADMIN_SCOPE'
if ($PublicTrustScope -cne 'LocalMachine') { throw 'explicit LocalMachine PUBLIC-only test trust selection required' }
if ($env:GITHUB_ACTIONS -cne 'true' -or $env:RUNNER_ENVIRONMENT -cne 'github-hosted' -or $env:RUNNER_OS -cne 'Windows') { throw 'selected disposable hosted Windows runner required; no local machine mutation' }
$windowsIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
try {
    $principal = [Security.Principal.WindowsPrincipal]::new($windowsIdentity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'already-elevated runner required; no UAC/elevation/retry' }
} finally { $windowsIdentity.Dispose() }
Write-SigningPhase END 'HOSTED_ADMIN_SCOPE'
function Hash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Public-Hash($Cert) {
    $h = [Security.Cryptography.SHA256]::Create()
    try { ([BitConverter]::ToString($h.ComputeHash($Cert.RawData))).Replace('-','').ToLowerInvariant() } finally { $h.Dispose() }
}
function Assert-NoChildTools {
    # Calls below are synchronous console invocations; no background process is started.
    # A remaining direct child, or inability to query, prevents cleanup/export.
    Write-SigningPhase BEGIN 'CHILD_OBSERVATION'
    $children = @(Get-CimInstance Win32_Process -Filter "ParentProcessId = $PID" -Property ProcessId -OperationTimeoutSec 15 -ErrorAction Stop)
    if ($children.Count -ne 0) { throw 'owned signing call may still be active; retain key/trust, no export' }
Write-SigningPhase END 'CHILD_OBSERVATION'
}
foreach ($path in @($InputPlan,$ArtifactRoot,$PrivateRoot,$Output,$SdkBin)) {
    if (-not [IO.Path]::IsPathRooted($path)) { throw 'explicit absolute private-lane paths required' }
}
if ((Get-Item -LiteralPath $InputPlan).Length -gt 65536) { throw 'input-plan64KiB cap before hash' }
if ((Hash $InputPlan) -ne $InputPlanSha256) { throw 'exact accepted input-plan bytes required' }
if ((Test-Path -LiteralPath $PrivateRoot) -or (Test-Path -LiteralPath $Output)) { throw 'exclusive private/output roots required' }
foreach ($name in @('makeappx.exe','signtool.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $SdkBin $name) -PathType Leaf)) { throw 'existing selected SDK required; no installation/fallback' }
}
$validator = Join-Path $PSScriptRoot 'validate_pair_inputs.py'
Write-SigningPhase BEGIN 'PAIR_PYTHON_PREFLIGHT'
$raw = & python $validator --plan $InputPlan --input-root $ArtifactRoot
if ($LASTEXITCODE -ne 0) { throw 'pair preflight refused BEFORE key/trust generation' }
Write-SigningPhase END 'PAIR_PYTHON_PREFLIGHT'
$plan = $raw | ConvertFrom-Json
New-Item -ItemType Directory -Path $PrivateRoot | Out-Null
$helper = Join-Path $PSScriptRoot 'sign_copy.ps1'
$tag = 'ua50-owned-sign-' + [Guid]::NewGuid().ToString('N')
Write-SigningPhase BEGIN 'CERT_STORE_SNAPSHOT'
$preMy = @(Get-ChildItem Cert:\CurrentUser\My | ForEach-Object { $_.Thumbprint })
Write-SigningPhase END 'CERT_STORE_SNAPSHOT'
$owned = $null; $thumb = $null; $publicHash = $null; $keyName = $null; $keyProvider = $null
$createAttempted = $false; $trustAttempted = $false; $operationError = $null
$cleanupErrors = @(); $callsReturned = $false; $cleanupProved = $false
Write-SigningPhase END 'PAIR_PREFLIGHT'
try {
    Assert-NoChildTools
    $createAttempted = $true
    # Exactly ONE key/certificate, CurrentUser scope, nonexportable CNG software key.
Write-SigningPhase BEGIN 'KEY_CREATE'
    $owned = New-SelfSignedCertificate -Type Custom -Subject 'CN=xllama-dev' `
        -FriendlyName $tag -CertStoreLocation 'Cert:\CurrentUser\My' `
        -Provider 'Microsoft Software Key Storage Provider' -KeyAlgorithm RSA -KeyLength 2048 `
        -HashAlgorithm SHA256 -KeyUsage DigitalSignature -KeyExportPolicy NonExportable `
        -NotAfter ([DateTime]::UtcNow.AddDays(7)) `
        -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3','2.5.29.19={text}')
Write-SigningPhase END 'KEY_CREATE'
Write-SigningPhase BEGIN 'KEY_BINDING'
    if ($preMy -contains $owned.Thumbprint) { throw 'owned certificate unexpectedly pre-existed; no deletion authority' }
    $thumb = $owned.Thumbprint
    $publicHash = Public-Hash $owned
    if ($owned.Subject -cne 'CN=xllama-dev' -or $owned.FriendlyName -ne $tag -or -not $owned.HasPrivateKey) { throw 'new signer binding failed' }
    $rsa = [Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPrivateKey($owned)
    try {
        if ($rsa -isnot [Security.Cryptography.RSACng] -or $rsa.Key.IsMachineKey -or [int]$rsa.Key.ExportPolicy -ne 0) { throw 'owned CurrentUser nonexportable CNG key required' }
        $keyName = $rsa.Key.KeyName; $keyProvider = $rsa.Key.Provider
    } finally { if ($rsa) { $rsa.Dispose() } }
Write-SigningPhase END 'KEY_BINDING'
    # Export PUBLIC DER only, stripping any private-key association before trust add.
Write-SigningPhase BEGIN 'PUBLIC_TRUST_ADD'
Write-SigningPhase BEGIN 'PUBLIC_DER_EXPORT'
    $publicDer = $owned.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert)
Write-SigningPhase END 'PUBLIC_DER_EXPORT'
Write-SigningPhase BEGIN 'PUBLIC_CERT_CONSTRUCT'
    $public = [Security.Cryptography.X509Certificates.X509Certificate2]::new($publicDer)
Write-SigningPhase END 'PUBLIC_CERT_CONSTRUCT'
    try {
Write-SigningPhase BEGIN 'PUBLIC_CERT_BINDING'
        if ($public.HasPrivateKey -or (Public-Hash $public) -ne $publicHash) { throw 'public trust object binding failed' }
Write-SigningPhase END 'PUBLIC_CERT_BINDING'
Write-SigningPhase BEGIN 'MACHINE_ROOT_CONSTRUCT'
        $store = [Security.Cryptography.X509Certificates.X509Store]::new('Root','LocalMachine')
Write-SigningPhase END 'MACHINE_ROOT_CONSTRUCT'
        try {
Write-SigningPhase BEGIN 'MACHINE_ROOT_OPEN'
            $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite -bor [Security.Cryptography.X509Certificates.OpenFlags]::OpenExistingOnly)
Write-SigningPhase END 'MACHINE_ROOT_OPEN'
Write-SigningPhase BEGIN 'MACHINE_ROOT_ABSENCE'
            if (@($store.Certificates.Find('FindByThumbprint',$thumb,$false)).Count -ne 0) { throw 'machine trust entry pre-existed; refuse ownership' }
Write-SigningPhase END 'MACHINE_ROOT_ABSENCE'
            # Mark only this exact PUBLIC certificate add attempt after absence proof.
            $trustAttempted = $true
Write-SigningPhase BEGIN 'MACHINE_ROOT_ADD'
            $store.Add($public)
Write-SigningPhase END 'MACHINE_ROOT_ADD'
Write-SigningPhase BEGIN 'MACHINE_ROOT_READBACK'
            $added = @($store.Certificates.Find('FindByThumbprint',$thumb,$false))
            if ($added.Count -ne 1 -or $added[0].HasPrivateKey -or (Public-Hash $added[0]) -ne $publicHash) { throw 'exact PUBLIC machine trust readback failed' }
Write-SigningPhase END 'MACHINE_ROOT_READBACK'
        } finally {
Write-SigningPhase BEGIN 'MACHINE_ROOT_CLOSE'
            $store.Close()
Write-SigningPhase END 'MACHINE_ROOT_CLOSE'
        }
    } finally {
Write-SigningPhase BEGIN 'PUBLIC_CERT_DISPOSE'
        $public.Dispose()
Write-SigningPhase END 'PUBLIC_CERT_DISPOSE'
    }
Write-SigningPhase END 'PUBLIC_TRUST_ADD'
    foreach ($arm in @('Patched','Stock')) {
        $entry = $plan.$arm
        $installed = if ($arm -eq 'Patched') { $plan.InstalledVersion } else { $plan.PatchedVersion }
        $newVersion = if ($arm -eq 'Patched') { $plan.PatchedVersion } else { $plan.RecoveryVersion }
        $buildArgs = @{}
        if ($arm -eq 'Patched') {
            $buildArgs.BuildReceipt = $entry.BuildReceipt
            $buildArgs.ExpectedBuildReceiptSha256 = $entry.BuildReceiptSHA256
        }
        # Direct synchronous invocation; helper waits on every Python/SDK console process.
Write-SigningPhase BEGIN ('SIGN_COPY_' + $arm)
        & $helper -UnsignedPackage $entry.Package -ExpectedUnsignedSha256 $entry.PackageSHA256 `
            -SourceReceipt $entry.SourceReceipt -ExpectedSourceReceiptSha256 $entry.SourceReceiptSHA256 `
            -ExpectedTrack $entry.Track -InstalledVersion $installed -NewVersion $newVersion `
            -SdkBin $SdkBin -PrivateWork (Join-Path $PrivateRoot ($arm + '-work')) `
            -Output (Join-Path $PrivateRoot ($arm + '-signed')) `
            -CertificateThumbprint $thumb -ExpectedPublicCertificateSha256 $publicHash @buildArgs
Write-SigningPhase END ('SIGN_COPY_' + $arm)
        Assert-NoChildTools
    }
    $callsReturned = $true
} catch {
    $operationError = $_
    Write-SigningFailure
} finally {
    # Cleanup only after synchronous calls unwind AND no owned child remains.
    # Abrupt worker/job kill cannot promise finally: no cleanup/export success inferred.
Write-SigningPhase BEGIN 'CLEANUP'
    $terminal = $false
    try { Assert-NoChildTools; $terminal = $true } catch { $cleanupErrors += 'TOOL_TERMINAL_UNKNOWN'; Write-SigningFailure }
    if ($terminal -and $thumb) {
        if ($trustAttempted) {
            try {
Write-SigningPhase BEGIN 'OWNED_TRUST_REMOVE'
Write-SigningPhase BEGIN 'CLEAN_MACHINE_ROOT_CONSTRUCT'
                $store = [Security.Cryptography.X509Certificates.X509Store]::new('Root','LocalMachine')
Write-SigningPhase END 'CLEAN_MACHINE_ROOT_CONSTRUCT'
                try {
Write-SigningPhase BEGIN 'CLEAN_MACHINE_ROOT_OPEN'
                    $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite -bor [Security.Cryptography.X509Certificates.OpenFlags]::OpenExistingOnly)
Write-SigningPhase END 'CLEAN_MACHINE_ROOT_OPEN'
Write-SigningPhase BEGIN 'CLEAN_MACHINE_ROOT_FIND'
                    $matches = @($store.Certificates.Find('FindByThumbprint',$thumb,$false))
Write-SigningPhase END 'CLEAN_MACHINE_ROOT_FIND'
                    foreach ($item in $matches) {
Write-SigningPhase BEGIN 'CLEAN_MACHINE_ROOT_BINDING'
                        if ((Public-Hash $item) -ne $publicHash -or $item.HasPrivateKey) { throw 'foreign machine trust entry; refuse removal' }
Write-SigningPhase END 'CLEAN_MACHINE_ROOT_BINDING'
Write-SigningPhase BEGIN 'CLEAN_MACHINE_ROOT_REMOVE'
                        $store.Remove($item)
Write-SigningPhase END 'CLEAN_MACHINE_ROOT_REMOVE'
                    }
Write-SigningPhase BEGIN 'CLEAN_MACHINE_ROOT_ABSENCE'
                    if (@($store.Certificates.Find('FindByThumbprint',$thumb,$false)).Count -ne 0) { throw 'owned machine trust removal unproven' }
Write-SigningPhase END 'CLEAN_MACHINE_ROOT_ABSENCE'
                } finally {
Write-SigningPhase BEGIN 'CLEAN_MACHINE_ROOT_CLOSE'
                    $store.Close()
Write-SigningPhase END 'CLEAN_MACHINE_ROOT_CLOSE'
                }
                # Reopen both views READ ONLY after removal. Never delete CurrentUser roots.
                foreach ($view in @('LocalMachine','CurrentUser')) {
Write-SigningPhase BEGIN ('CLEAN_ROOT_VERIFY_CONSTRUCT_' + $view)
                    $verifyStore = [Security.Cryptography.X509Certificates.X509Store]::new('Root',$view)
Write-SigningPhase END ('CLEAN_ROOT_VERIFY_CONSTRUCT_' + $view)
                    try {
Write-SigningPhase BEGIN ('CLEAN_ROOT_VERIFY_OPEN_' + $view)
                        $verifyStore.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadOnly -bor [Security.Cryptography.X509Certificates.OpenFlags]::OpenExistingOnly)
Write-SigningPhase END ('CLEAN_ROOT_VERIFY_OPEN_' + $view)
Write-SigningPhase BEGIN ('CLEAN_ROOT_VERIFY_ABSENCE_' + $view)
                        if (@($verifyStore.Certificates.Find('FindByThumbprint',$thumb,$false)).Count -ne 0) { throw 'owned public test trust still visible; no export' }
Write-SigningPhase END ('CLEAN_ROOT_VERIFY_ABSENCE_' + $view)
                    } finally {
Write-SigningPhase BEGIN ('CLEAN_ROOT_VERIFY_CLOSE_' + $view)
                        $verifyStore.Close()
Write-SigningPhase END ('CLEAN_ROOT_VERIFY_CLOSE_' + $view)
                    }
                }
Write-SigningPhase END 'OWNED_TRUST_REMOVE'
            } catch { $cleanupErrors += 'OWNED_PUBLIC_TRUST_CLEANUP_UNPROVEN'; Write-SigningFailure }
        }
        try {
Write-SigningPhase BEGIN 'OWNED_KEY_REMOVE'
            $ownedPath = "Cert:\CurrentUser\My\$thumb"
            if (Test-Path -LiteralPath $ownedPath) {
                $current = Get-Item -LiteralPath $ownedPath
                if ((Public-Hash $current) -ne $publicHash -or $current.FriendlyName -ne $tag -or $preMy -contains $thumb) { throw 'foreign signer entry; refuse removal' }
                Remove-Item -LiteralPath $ownedPath -DeleteKey -Force -Confirm:$false -ErrorAction Stop
            }
            if (Test-Path -LiteralPath $ownedPath) { throw 'owned signer entry remains' }
            if (-not $keyName -or -not $keyProvider -or [Security.Cryptography.CngKey]::Exists($keyName,$keyProvider)) { throw 'owned private key deletion unproven' }
Write-SigningPhase END 'OWNED_KEY_REMOVE'
        } catch { $cleanupErrors += 'OWNED_PRIVATE_KEY_CLEANUP_UNPROVEN'; Write-SigningFailure }
    } elseif ($createAttempted) { $cleanupErrors += 'KEY_CREATION_OR_TERMINAL_STATE_UNKNOWN_RETAIN' }
    if ($owned) { $owned.Dispose() }
    $cleanupProved = $terminal -and $thumb -and $cleanupErrors.Count -eq 0
    [ordered]@{ schema='ua50-owned-signing-cleanup-v1'; signer_thumbprint=$thumb;
        public_trust_scope='LocalMachine/Root'; private_key_scope='CurrentUser/My';
        calls_returned=$callsReturned; terminal_children_absent=$terminal; cleanup_proved=$cleanupProved;
        errors=$cleanupErrors; private_key_exported=$false; device_mutations=0
    } | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $PrivateRoot 'OWNED-CLEANUP.json') -Encoding utf8
Write-SigningPhase END 'CLEANUP'
}
if ($operationError) { throw $operationError }
if (-not $callsReturned -or -not $cleanupProved) { throw 'pair or exact owned cleanup incomplete; no export/retry' }
# Both independently verified signed packages must exist before PUBLIC-only export.
Write-SigningPhase BEGIN 'PUBLIC_PAIR_EXPORT'
$patched = Join-Path $PrivateRoot 'Patched-signed'
$stock = Join-Path $PrivateRoot 'Stock-signed'
if ((Hash (Join-Path $patched 'xllama-test.cer')) -ne $publicHash -or (Hash (Join-Path $stock 'xllama-test.cer')) -ne $publicHash) { throw 'pair signer differs' }
$sum = (Get-Item -LiteralPath (Join-Path $patched 'xllama-update.msix')).Length + (Get-Item -LiteralPath (Join-Path $stock 'xllama-update.msix')).Length
if ($sum -gt (400MB - 1MB)) { throw 'combined public output400MiB cap' }
New-Item -ItemType Directory -Path $Output | Out-Null
Copy-Item -LiteralPath (Join-Path $patched 'xllama-update.msix') -Destination (Join-Path $Output 'xllama-patched-update.msix')
Copy-Item -LiteralPath (Join-Path $stock 'xllama-update.msix') -Destination (Join-Path $Output 'xllama-stock-forward-recovery.msix')
Copy-Item -LiteralPath (Join-Path $patched 'xllama-test.cer') -Destination (Join-Path $Output 'xllama-test.cer')
[ordered]@{ schema='ua50-signed-forward-recovery-pair-v1'; input_plan_sha256=$InputPlanSha256;
    public_trust_scope='LocalMachine/Root'; private_key_scope='CurrentUser/My';
    patched=(Get-Content -LiteralPath (Join-Path $patched 'SIGNED-COPY-RECEIPT.json') -Raw | ConvertFrom-Json);
    recovery=(Get-Content -LiteralPath (Join-Path $stock 'SIGNED-COPY-RECEIPT.json') -Raw | ConvertFrom-Json);
    cleanup_proved=$cleanupProved; recovery_payload='accepted STOCK baseline, NOT old1495';
    console_trust='UNKNOWN'; LocalState_schema_compatibility='UNPROVEN'; install_performed=$false
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $Output 'PAIR-RECEIPT.json') -Encoding utf8

Write-SigningPhase END 'PUBLIC_PAIR_EXPORT'
} catch {
    $originalSigningError = $_
    Write-SigningFailure
    throw $originalSigningError
}
