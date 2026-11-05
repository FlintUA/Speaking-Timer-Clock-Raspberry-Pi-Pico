$ErrorActionPreference = "Stop"

$HomeSdk = Join-Path $env:USERPROFILE ".pico-sdk"
$Sdk = Join-Path $HomeSdk "sdk\2.3.1"
$Examples = Join-Path $HomeSdk "pico-examples"
$Project = Join-Path $PSScriptRoot "nest_autotest"
$Build = Join-Path $Project "build"

function Find-One([string]$Root, [string]$Filter) {
    $item = Get-ChildItem -Path $Root -Recurse -Filter $Filter -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $item) { return $null }
    return $item.FullName
}

if (-not (Test-Path $Sdk)) { throw "Pico SDK 2.3.1 not found: $Sdk" }
if (-not (Test-Path $Examples)) { throw "pico-examples not found: $Examples" }

$CMake = Find-One (Join-Path $HomeSdk "cmake") "cmake.exe"
$Ninja = Find-One (Join-Path $HomeSdk "ninja") "ninja.exe"
$Pioasm = Find-One (Join-Path $HomeSdk "tools") "pioasm.exe"
$Picotool = Find-One (Join-Path $HomeSdk "picotool") "picotool.exe"

if (-not $CMake) { throw "cmake.exe not found" }
if (-not $Ninja) { throw "ninja.exe not found" }
if (-not $Pioasm) { throw "pioasm.exe not found" }
if (-not $Picotool) { throw "picotool.exe not found" }

$PioasmDir = Split-Path $Pioasm -Parent
$PicotoolDir = Split-Path $Picotool -Parent

New-Item -ItemType Directory -Force -Path $Build | Out-Null

& $CMake `
    -S $Project `
    -B $Build `
    -G Ninja `
    -DPICO_BOARD=pico2_w `
    -DPICO_PLATFORM=rp2350 `
    -DPICO_SDK_PATH="$Sdk" `
    -DPICO_EXAMPLES_PATH="$Examples" `
    -Dpioasm_DIR="$PioasmDir" `
    -Dpicotool_DIR="$PicotoolDir"

if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }

& $Ninja -C $Build nest_a2dp_autotest
if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }

$Uf2 = Join-Path $Build "nest_a2dp_autotest.uf2"
if (-not (Test-Path $Uf2)) { throw "UF2 not found: $Uf2" }

Write-Host ""
Write-Host "SUCCESS"
Write-Host "UF2: $Uf2"
