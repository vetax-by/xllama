# SOURCE CANDIDATE ONLY. Never invoked during this preparation.
# Uses an already provisioned, locally trusted private certificate; exports CER only.
# No key generation/import/export, no device or explicit network/deploy operation.
# Windows trust verification may perform provider-managed chain/revocation checks.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$UnsignedPackage,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-fA-F]{64}$')][string]$ExpectedUnsignedSha256,
    [Parameter(Mandatory)][string]$SourceReceipt,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-fA-F]{64}$')][string]$ExpectedSourceReceiptSha256,
    [Parameter(Mandatory)][ValidateSet('baseline','patch')][string]$ExpectedTrack,
    [Parameter(Mandatory)][string]$InstalledVersion,
    [Parameter(Mandatory)][string]$NewVersion,
    [Parameter(Mandatory)][string]$SdkBin,
    [Parameter(Mandatory)][string]$PrivateWork,
    [Parameter(Mandatory)][string]$Output,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-fA-F]{40}$')][string]$CertificateThumbprint,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-fA-F]{64}$')][string]$ExpectedPublicCertificateSha256
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
Write-SigningPhase BEGIN 'COPY_PREFLIGHT'
if ($env:OS -ne 'Windows_NT') { throw 'Windows SDK signing lane required; no installs attempted' }
function Run-Native([string]$Exe, [string[]]$Argv) {
    & $Exe @Argv
    if ($LASTEXITCODE -ne 0) { throw "native signing-copy step failed: $Exe ($LASTEXITCODE)" }
}
function Hash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
$inputPath = (Resolve-Path -LiteralPath $UnsignedPackage).Path
$sourcePath = (Resolve-Path -LiteralPath $SourceReceipt).Path
if ((Hash $inputPath) -ne $ExpectedUnsignedSha256.ToLowerInvariant()) { throw 'accepted unsigned bytes changed' }
if ((Hash $sourcePath) -ne $ExpectedSourceReceiptSha256.ToLowerInvariant()) { throw 'accepted source receipt changed' }
$source = Get-Content -LiteralPath $sourcePath -Raw | ConvertFrom-Json
if ($source.track -ne $ExpectedTrack -or $source.xllama -ne '4db51522c850e62d0bb24234dfeb48e55f4017d7' -or $source.llama -ne 'd81235049384534c167caea52b85a694f6103d14') { throw 'source track/base mismatch' }
if ($ExpectedTrack -eq 'patch') {
    if ($source.reviewed_patch_lock -ne '5dab2dab1aca23e6d914bd5304ee957f3dc741f57d19457c6ef3b3af3ecf562d' -or $source.selected_shipping_files_verified -ne 47) { throw 'first-answer 5dab/47 source receipt required' }
} elseif ($source.reviewed_patch_lock -or $source.selected_shipping_files_verified -ne 0) { throw 'baseline must remain unpatched' }
if (-not [IO.Path]::IsPathRooted($SdkBin) -or -not [IO.Path]::IsPathRooted($PrivateWork) -or -not [IO.Path]::IsPathRooted($Output)) { throw 'explicit absolute SDK/private/output paths required' }
$makeappx = Join-Path $SdkBin 'makeappx.exe'
$signtool = Join-Path $SdkBin 'signtool.exe'
foreach ($tool in @($makeappx,$signtool)) { if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw 'selected SDK tool missing; no PATH fallback/install' } }
if ((Test-Path -LiteralPath $PrivateWork) -or (Test-Path -LiteralPath $Output)) { throw 'exclusive private/output paths must not exist' }
# CurrentUser/My only; no PFX/password on disk, argv or log.
Write-SigningPhase BEGIN 'SIGNER_LOOKUP_BINDING'
$cert = Get-Item -LiteralPath "Cert:\CurrentUser\My\$CertificateThumbprint"
if (-not $cert.HasPrivateKey -or $cert.Subject -cne 'CN=xllama-dev') { throw 'exact publisher/private signing capability required' }
$now = [DateTime]::UtcNow
if ($cert.NotBefore.ToUniversalTime() -gt $now -or $cert.NotAfter.ToUniversalTime() -le $now) { throw 'signer validity interval' }
$eku = @($cert.Extensions | Where-Object { $_.Oid.Value -eq '2.5.29.37' })
if ($eku.Count -ne 1 -or -not ($eku[0].EnhancedKeyUsages.Value -contains '1.3.6.1.5.5.7.3.3')) { throw 'explicit code-signing EKU required' }
$public = $cert.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert)
$algorithm = [Security.Cryptography.SHA256]::Create()
try { $publicHash = ([BitConverter]::ToString($algorithm.ComputeHash($public))).Replace('-','').ToLowerInvariant() } finally { $algorithm.Dispose() }
if ($publicHash -ne $ExpectedPublicCertificateSha256.ToLowerInvariant()) { throw 'selected public certificate SHA mismatch' }
Write-SigningPhase END 'SIGNER_LOOKUP_BINDING'
$helper = Join-Path $PSScriptRoot 'package_copy.py'
Write-SigningPhase END 'COPY_PREFLIGHT'
Write-SigningPhase BEGIN 'COPY_PYTHON_PREPARE'
Run-Native 'python' @($helper,'prepare','--package',$inputPath,'--sha256',$ExpectedUnsignedSha256.ToLowerInvariant(),'--installed',$InstalledVersion,'--new',$NewVersion,'--work',$PrivateWork)
Write-SigningPhase END 'COPY_PYTHON_PREPARE'
# Helper changes ONLY root Identity.Version; all other payload bytes stay exact.
$tree = Join-Path $PrivateWork 'unpacked'
$copy = Join-Path $PrivateWork 'xllama-update.msix'
Write-SigningPhase BEGIN 'SDK_MAKEAPPX'
Run-Native $makeappx @('pack','/d',$tree,'/p',$copy,'/h','SHA256','/no')
Write-SigningPhase END 'SDK_MAKEAPPX'
Write-SigningPhase BEGIN 'SDK_SIGNTOOL_SIGN'
Run-Native $signtool @('sign','/fd','SHA256','/s','My','/sha1',$CertificateThumbprint,$copy)
Write-SigningPhase END 'SDK_SIGNTOOL_SIGN'
# Fail on missing local trust. This does NOT prove Xbox trust.
Write-SigningPhase BEGIN 'SDK_SIGNTOOL_VERIFY'
Run-Native $signtool @('verify','/pa','/all','/v',$copy)
Write-SigningPhase END 'SDK_SIGNTOOL_VERIFY'
Write-SigningPhase BEGIN 'AUTHENTICODE_VERIFY'
$signature = Get-AuthenticodeSignature -LiteralPath $copy
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Thumbprint -ne $cert.Thumbprint) { throw 'signature verification/signer mismatch' }
Write-SigningPhase END 'AUTHENTICODE_VERIFY'
Write-SigningPhase BEGIN 'COPY_PYTHON_VERIFY'
$verificationRaw = Run-Native 'python' @($helper,'verify','--package',$copy,'--plan',(Join-Path $PrivateWork 'COPY-PLAN.json'))
$payloadVerification = $verificationRaw | ConvertFrom-Json
Write-SigningPhase END 'COPY_PYTHON_VERIFY'
Write-SigningPhase BEGIN 'COPY_EXPORT'
if ((Hash $inputPath) -ne $ExpectedUnsignedSha256.ToLowerInvariant()) { throw 'original unsigned changed; refuse publication' }
# Explicit output whitelist ONLY signed copy, public DER CER and public receipt.
New-Item -ItemType Directory -Path $Output | Out-Null
Copy-Item -LiteralPath $copy -Destination (Join-Path $Output 'xllama-update.msix')
[IO.File]::WriteAllBytes((Join-Path $Output 'xllama-test.cer'),$public)
$record = [ordered]@{
    schema='ua50-signed-copy-v1'; track=$ExpectedTrack; source_receipt_sha256=$ExpectedSourceReceiptSha256.ToLowerInvariant();
    unsigned_sha256=$ExpectedUnsignedSha256.ToLowerInvariant(); signed_sha256=(Hash (Join-Path $Output 'xllama-update.msix'));
    public_certificate_sha256=$publicHash; signer_thumbprint=$cert.Thumbprint; publisher=$cert.Subject;
    installed_version_input=$InstalledVersion; new_version=$NewVersion; private_key_exported=$false;
    windows_signature_verified=$true; console_trust='UNKNOWN'; install_performed=$false;
    payload_verification=$payloadVerification
}
$record | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Output 'SIGNED-COPY-RECEIPT.json') -Encoding utf8
# Existing certificate belongs to its provisioning owner: do NOT delete it here.
# Failed private work is retained; no retry, device mutation or forced cleanup.

Write-SigningPhase END 'COPY_EXPORT'
} catch {
    $originalSigningError = $_
    Write-SigningFailure
    throw $originalSigningError
}
