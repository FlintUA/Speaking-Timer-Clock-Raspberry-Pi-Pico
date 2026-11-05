$ErrorActionPreference = "Stop"

$HomeSdk = Join-Path $env:USERPROFILE ".pico-sdk"
$Sdk = Join-Path $HomeSdk "sdk\2.3.1"
$Examples = Join-Path $HomeSdk "pico-examples"
$Build = Join-Path $Examples "build-a2dp"

function Find-One([string]$Root, [string]$Filter) {
    $item = Get-ChildItem -Path $Root -Recurse -Filter $Filter -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $item) { return $null }
    return $item.FullName
}

if (-not (Test-Path $Sdk)) {
    throw "Pico SDK 2.3.1 was not found at $Sdk. Install it with the Raspberry Pi Pico VS Code extension or adjust this script."
}

if (-not (Test-Path $Examples)) {
    Write-Host "pico-examples not found. Cloning..."
    & git clone --depth 1 https://github.com/raspberrypi/pico-examples.git $Examples
    if ($LASTEXITCODE -ne 0) { throw "git clone pico-examples failed" }
}

$CMake = Find-One (Join-Path $HomeSdk "cmake") "cmake.exe"
$Ninja = Find-One (Join-Path $HomeSdk "ninja") "ninja.exe"
$Pioasm = Find-One (Join-Path $HomeSdk "tools") "pioasm.exe"
$Picotool = Find-One (Join-Path $HomeSdk "picotool") "picotool.exe"

foreach ($pair in @(
    @("cmake.exe", $CMake),
    @("ninja.exe", $Ninja),
    @("pioasm.exe", $Pioasm),
    @("picotool.exe", $Picotool)
)) {
    if ([string]::IsNullOrWhiteSpace($pair[1])) {
        throw "$($pair[0]) was not found under $HomeSdk"
    }
}

$A2dpDir = Join-Path $Examples "bluetooth\btstack_examples\a2dp_source_demo"
$A2dpCMake = Join-Path $A2dpDir "CMakeLists.txt"
if (-not (Test-Path $A2dpCMake)) { throw "Official A2DP source CMakeLists.txt not found: $A2dpCMake" }

$cmakeText = Get-Content $A2dpCMake -Raw
if ($cmakeText -notmatch "pico_enable_stdio_usb\(a2dp_source_demo 1\)") {
    Write-Host "Enabling USB stdio for a2dp_source_demo..."
    Add-Content $A2dpCMake ""
    Add-Content $A2dpCMake "pico_enable_stdio_usb(a2dp_source_demo 1)"
    Add-Content $A2dpCMake "pico_enable_stdio_uart(a2dp_source_demo 0)"
}

$PioasmDir = Split-Path $Pioasm -Parent
$PicotoolDir = Split-Path $Picotool -Parent

Write-Host "SDK      : $Sdk"
Write-Host "Examples : $Examples"
Write-Host "CMake    : $CMake"
Write-Host "Ninja    : $Ninja"
Write-Host "pioasm   : $Pioasm"
Write-Host "picotool : $Picotool"

& $CMake -S $Examples -B $Build -G Ninja -DPICO_BOARD=pico2_w -DPICO_PLATFORM=rp2350 -DPICO_SDK_PATH="$Sdk" -Dpioasm_DIR="$PioasmDir" -Dpicotool_DIR="$PicotoolDir"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }

& $Ninja -C $Build a2dp_source_demo
if ($LASTEXITCODE -ne 0) { throw "A2DP build failed with exit code $LASTEXITCODE" }

$Uf2 = Join-Path $Build "bluetooth\btstack_examples\a2dp_source_demo\a2dp_source_demo.uf2"
if (-not (Test-Path $Uf2)) { throw "Build finished but UF2 was not found: $Uf2" }

Write-Host ""
Write-Host "SUCCESS"
Write-Host "UF2: $Uf2"
