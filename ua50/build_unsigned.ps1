param([Parameter(Mandatory)][string]$Source,
      [Parameter(Mandatory)][string]$Output,
      [ValidateSet('baseline','patch')][string]$Track)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path $Source).Path
$out = [IO.Path]::GetFullPath($Output)
if (Test-Path $out) { throw 'exclusive output exists' }
New-Item -ItemType Directory $out | Out-Null
Set-Location $root
function Run-Native([string]$Exe, [string[]]$Argv) {
    & $Exe @Argv
    if ($LASTEXITCODE -ne 0) { throw "native command failed: $Exe ($LASTEXITCODE)" }
}
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild unavailable' }
$sdk = "${env:ProgramFiles(x86)}\Windows Kits\10\Include\10.0.22621.0"
if (-not (Test-Path $sdk)) { throw 'exact SDK22621 absent: no retarget/install' }
$expectedPkgs = @{
    'Microsoft.Windows.CppWinRT'='2.0.240405.15'
    'Microsoft.AI.DirectML'='1.15.4'
    'Microsoft.ML.OnnxRuntime.DirectML'='1.24.4'
    'Microsoft.ML.OnnxRuntimeGenAI.DirectML'='0.14.1'
}
$pkgs = ([xml](Get-Content uwp/packages.config)).packages.package
if ($pkgs.Count -ne 4) { throw 'package count changed' }
foreach ($p in $pkgs) { if ($expectedPkgs[$p.id] -ne $p.version) { throw 'package version changed' } }
# No source build script: that script creates/exports a test PFX automatically.
Run-Native nuget @('restore','uwp/packages.config','-PackagesDirectory','uwp/packages','-NonInteractive','-Source','https://api.nuget.org/v3/index.json')
$vendorPins = @(
    @{name='onnxruntime.dll'; sha='f3c2e1cc9a61f67db2dbf45bef626e6e299490548fae5f6f22f89d441e77391e'; dir='vendor/onnxruntime-patched/win-x64'; pkg='Microsoft.ML.OnnxRuntime.DirectML.1.24.4'},
    @{name='onnxruntime-genai.dll'; sha='ec034349a28e41f6bfdce52731533caf7e590bab4e8dc7002bb48b92481f334d'; dir='vendor/onnxruntime-genai-patched/win-x64'; pkg='Microsoft.ML.OnnxRuntimeGenAI.DirectML.0.14.1'}
)
foreach ($v in $vendorPins) {
    New-Item -ItemType Directory -Path $v.dir -Force | Out-Null
    Run-Native gh @('release','download','vendor-dlls-v1','--repo','gianlucamazza/xllama','--pattern',$v.name,'--dir',$v.dir)
    $file = Join-Path $v.dir $v.name
    if ((Get-FileHash $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $v.sha) { throw 'vendor SHA mismatch' }
    Copy-Item $file "uwp/packages/$($v.pkg)/runtimes/win-x64/native/$($v.name)" -Force
}
$env:GH_TOKEN = $null
$crt = Get-ChildItem -Directory -Path "${env:ProgramFiles}\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT" |
    Sort-Object FullName -Descending | Select-Object -First 1
if (-not $crt) { throw 'desktop CRT redist missing' }
foreach ($name in @('MSVCP140.dll','MSVCP140_1.dll','VCRUNTIME140.dll','VCRUNTIME140_1.dll')) {
    Copy-Item (Join-Path $crt.FullName $name) (Join-Path 'uwp' $name)
}
$common = @('/p:Configuration=Release','/p:Platform=x64','/p:PlatformToolset=v143',
    '/p:WindowsTargetPlatformVersion=10.0.22621.0','/p:XllamaBackend=unified',
    '/p:AppxPackageSigningEnabled=false','/p:AppxSymbolPackageEnabled=false','/p:AppxBundle=Never','/m:2','/nologo')
$watch = [Diagnostics.Stopwatch]::StartNew()
foreach ($project in @('uwp/ggml-uwp.vcxproj','uwp/llama-common-uwp.vcxproj','uwp/xllama.sln')) {
    Run-Native $msbuild (@($project) + $common + @('/p:GenerateAppxPackageOnBuild=true'))
}
$watch.Stop()
$packages = @(Get-ChildItem 'uwp/AppPackages' -Recurse -File | Where-Object {
    $_.Extension -in '.msix','.appx' -and $_.FullName -notmatch '[\\/]Dependencies[\\/]'
})
if ($packages.Count -ne 1) { throw 'expected exactly one unsigned package' }
if ($packages[0].Length -gt 400MB) { throw 'package size cap400MiB' }
$zip = [IO.Compression.ZipFile]::OpenRead($packages[0].FullName)
try {
    foreach ($entry in $zip.Entries) {
        if ([IO.Path]::GetExtension($entry.FullName).ToLowerInvariant() -in @('.pfx','.p12','.pem','.key','.gguf','.safetensors')) {
            throw 'forbidden private-key-like/weight package member'
        }
        if ($entry.FullName -eq 'AppxSignature.p7x') { throw 'unexpected signed package' }
    }
} finally { $zip.Dispose() }
Copy-Item $packages[0].FullName $out
Copy-Item 'UA50-SOURCE-RECEIPT.json' $out
$record = [ordered]@{
    schema='ua50-windows-unsigned-v1'; track=$Track; xllama=(git rev-parse HEAD);
    llama=(git -C llama.cpp rev-parse HEAD); sdk='10.0.22621.0'; toolset='v143';
    runner_image=$env:ImageVersion; msbuild=$msbuild; crt_directory=$crt.FullName;
    build_seconds=$watch.Elapsed.TotalSeconds; signed=$false; deploy=$false; model_runs=0;
    package=@{name=$packages[0].Name;bytes=$packages[0].Length;sha256=(Get-FileHash $packages[0].FullName).Hash.ToLowerInvariant()};
    dependency_files=@(Get-ChildItem uwp/packages -Recurse -File -Filter '*.nupkg' | ForEach-Object {
        @{name=$_.Name;bytes=$_.Length;sha256=(Get-FileHash $_.FullName).Hash.ToLowerInvariant()}
    });
    vc_tools=@(Get-ChildItem "${env:ProgramFiles}\Microsoft Visual Studio\2022\*\VC\Tools\MSVC" -Directory | Select-Object -ExpandProperty Name)
}
$record | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $out 'BUILD-RECEIPT.json') -Encoding utf8
# Explicit output contains package + public receipt only; never whole worktree/.git/PFX.
Write-Host "UNSIGNED_PACKAGE_BUILD_COMPLETE $Track"
