param([Parameter(Mandatory)][string]$Source)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path $Source).Path
$tools = Join-Path $env:RUNNER_TEMP 'bounded-dxc-1.8.2505.1'
New-Item -ItemType Directory $tools | Out-Null
$archive = Join-Path $tools 'dxc.zip'
Invoke-WebRequest 'https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2505.1/dxc_2025_07_14.zip' -OutFile $archive
if ((Get-FileHash $archive).Hash.ToLowerInvariant() -ne '9ad895a6b039e3a8f8c22a1009f866800b840a74b50db9218d13319e215ea8a4') { throw 'Windows DXC archive digest' }
Expand-Archive $archive (Join-Path $tools 'extracted')
$dxc = Join-Path $tools 'extracted/bin/x64/dxc.exe'
$records = @()
foreach ($quant in @('q4_k','q6_k')) {
    foreach ($trace in @($false,$true)) {
        $variant = if ($trace) { 'trace64' } else { 't64' }
        $out = Join-Path $tools "$quant-$variant.dxil"
        $argv = @('-T','cs_6_4','-E','CSMain','-D','NUM_THREADS=64')
        if ($trace) { $argv += @('-D','XLLAMA_DIAGNOSTIC_TRACE=1') }
        $argv += @('-Fo',$out,(Join-Path $root "shaders/ggml_d3d12_mmv_$quant.hlsl"))
        & $dxc @argv
        if ($LASTEXITCODE -ne 0) { throw "DXC failed $quant $variant" }
        # This validation compile never rewrites the pinned shipping headers.
        $records += @{quant=$quant;variant=$variant;bytes=(Get-Item $out).Length;sha256=(Get-FileHash $out).Hash.ToLowerInvariant()}
    }
}
@{schema='bounded-windows-dxc-compile-v1';archive_sha256=(Get-FileHash $archive).Hash.ToLowerInvariant();compiler_sha256=(Get-FileHash $dxc).Hash.ToLowerInvariant();compiled=$records;shipping_headers_rewritten=$false;target_execution=$false} | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $root 'BOUNDED-SHADER-RECEIPT.json') -Encoding utf8
