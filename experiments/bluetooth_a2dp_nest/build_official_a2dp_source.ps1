$ErrorActionPreference = "Stop"

function Require-PathEnv([string]$Name) {
    $value = [Environment]::GetEnvironmentVariable($Name, "Process")
    if ([string]::IsNullOrWhiteSpace($value)) { throw "$Name is not set in this PowerShell session." }
    if (-not (Test-Path $value)) { throw "$Name points to a missing path: $value" }
    return (Resolve-Path $value).Path
}

$sdk = Require-PathEnv "PICO_SDK_PATH"
$examples = Require-PathEnv "PICO_EXAMPLES_PATH"
$extras = Require-PathEnv "PICO_EXTRAS_PATH"

Write-Host "PICO_SDK_PATH      = $sdk"
Write-Host "PICO_EXAMPLES_PATH = $examples"
Write-Host "PICO_EXTRAS_PATH   = $extras"

$build = Join-Path $examples "build-pico2w-a2dp"
New-Item -ItemType Directory -Force -Path $build | Out-Null

& cmake -S $examples -B $build -G Ninja -DPICO_BOARD=pico2_w -DPICO_PLATFORM=rp2350 -DPICO_SDK_PATH="$sdk" -DPICO_EXTRAS_PATH="$extras" -DBTSTACK_EXAMPLES_ALL=1
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }

& cmake --build $build --target a2dp_source_demo
if ($LASTEXITCODE -ne 0) { throw "A2DP build failed with exit code $LASTEXITCODE" }

$uf2 = Get-ChildItem -Path $build -Recurse -Filter "a2dp_source_demo.uf2" | Select-Object -First 1
if ($null -eq $uf2) { throw "Build completed but a2dp_source_demo.uf2 was not found under $build" }

Write-Host ""
Write-Host "SUCCESS"
Write-Host "UF2: $($uf2.FullName)"
Write-Host ""
Write-Host "Put Nest in Bluetooth pairing mode, flash this UF2 to Pico 2 W, then capture the USB serial log."
