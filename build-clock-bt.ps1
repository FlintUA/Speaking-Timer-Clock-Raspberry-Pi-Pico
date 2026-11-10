# Build the Pico 2 W clock. This script never flashes a board.
param(
    [string]$JblAddress = '',
    [switch]$Mp3,
    [switch]$NoWifi,
    [string]$BuildDirectory = '',
    [string]$HelixPath = '',
    [switch]$PhoneWeb
)
$ErrorActionPreference = 'Stop'
if ($PhoneWeb -and $NoWifi) { throw '-PhoneWeb requires Wi-Fi.' }
if ($JblAddress -ne '' -and $JblAddress -notmatch '^([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}$') {
    throw 'JblAddress must have format XX:XX:XX:XX:XX:XX'
}
$root = $PSScriptRoot
if (-not $HelixPath) { $HelixPath = Join-Path $root 'third_party\Helix-MP3-Decoder' }
$homeSdk = Join-Path $env:USERPROFILE '.pico-sdk'
$sdk = Join-Path $homeSdk 'sdk\2.3.1'
$examples = Join-Path $homeSdk 'pico-examples'
$cmake = Join-Path $homeSdk 'cmake\v4.3.4\bin\cmake.exe'
$source = Join-Path $root 'firmware\pico2w_c'
$build = if ($BuildDirectory) { [IO.Path]::GetFullPath($BuildDirectory) } else { Join-Path $source 'build-bt' }
function Find-PicoTool([string]$relativeRoot, [string]$fileName) {
    $directory = Join-Path $homeSdk $relativeRoot
    if (-not (Test-Path $directory)) { throw "Pico SDK tool directory missing: $directory" }
    $tool = Get-ChildItem -Path $directory -Filter $fileName -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $tool) { throw "Cannot find $fileName in $directory" }
    return $tool.FullName
}
if (-not (Test-Path $cmake)) { throw "CMake missing: $cmake" }
if (-not (Test-Path $sdk)) { throw "Pico SDK missing: $sdk" }
$gcc = Find-PicoTool 'toolchain' 'arm-none-eabi-gcc.exe'
$gxx = Join-Path (Split-Path $gcc -Parent) 'arm-none-eabi-g++.exe'
$ninja = Find-PicoTool 'ninja' 'ninja.exe'
$pioasm = Find-PicoTool 'tools' 'pioasm.exe'
$picotool = Find-PicoTool 'picotool' 'picotool.exe'
if (-not (Test-Path $gxx)) { throw "C++ compiler missing: $gxx" }
$toolBin = Split-Path $gcc -Parent
$ninjaBin = Split-Path $ninja -Parent
$env:PATH = "$toolBin;$ninjaBin;$env:PATH"
Write-Host "[BT-BUILD] GCC: $gcc"
Write-Host "[BT-BUILD] Ninja: $ninja"
Write-Host "[BT-BUILD] pioasm: $pioasm"
Write-Host "[BT-BUILD] picotool: $picotool"
if (-not (Test-Path (Join-Path $examples 'bluetooth\config\btstack_config_common.h'))) { throw "pico-examples missing from $examples" }
$options = @(
    '-S',$source,'-B',$build,'-G','Ninja',
    '-DPICO_BOARD=pico2_w','-DPICO_PLATFORM=rp2350-arm-s',
    "-DPICO_SDK_PATH=$sdk","-DPICO_EXAMPLES_PATH=$examples",
    '-DCLOCK_ENABLE_A2DP=ON',
    ("-DCLOCK_ENABLE_WIFI=" + $(if ($NoWifi) { 'OFF' } else { 'ON' })),
    ("-DCLOCK_ENABLE_PHONE_WEB=" + $(if ($PhoneWeb) { 'ON' } else { 'OFF' })),
    ("-DCLOCK_ENABLE_MP3=" + $(if ($Mp3) { 'ON' } else { 'OFF' })),
    ("-DCLOCK_HELIX_PATH=" + $(if ($Mp3) { $HelixPath } else { '' })),
    "-DCLOCK_A2DP_TARGET_ADDR=$JblAddress",
    "-DCMAKE_MAKE_PROGRAM=$ninja",
    "-DCMAKE_C_COMPILER=$gcc",
    "-DCMAKE_CXX_COMPILER=$gxx",
    "-DCMAKE_ASM_COMPILER=$gcc",
    "-Dpioasm_DIR=$(Split-Path $pioasm -Parent)",
    "-Dpicotool_DIR=$(Split-Path $picotool -Parent)"
)
if ($Mp3) {
    $helix = $HelixPath
    if (-not (Test-Path (Join-Path $helix 'src\helix_mp3.h'))) {
        throw "Helix library missing. Clone pinned revision into $helix before -Mp3."
    }
}
& $cmake @options
if ($LASTEXITCODE -ne 0) { throw "Bluetooth configure failed: $LASTEXITCODE" }
& $cmake --build $build --target speaking_clock_c
if ($LASTEXITCODE -ne 0) { throw "Bluetooth build failed: $LASTEXITCODE" }
$uf2 = Join-Path $build 'speaking_clock_c.uf2'
if (-not (Test-Path $uf2)) { throw "UF2 missing: $uf2" }
$buildId = (Get-Content (Join-Path $build 'speaking_clock_c.build-id.txt') -Raw).Trim()
Write-Host "[BT-BUILD] Firmware: $uf2 (build=$buildId)" -ForegroundColor Green
