# Build experimental A2DP clock on spare Pico 2 W only. Never flashes.
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$homeSdk = Join-Path $env:USERPROFILE '.pico-sdk'
$sdk = Join-Path $homeSdk 'sdk\2.3.1'
$examples = Join-Path $homeSdk 'pico-examples'
$cmake = Join-Path $homeSdk 'cmake\v4.3.4\bin\cmake.exe'
$source = Join-Path $root 'firmware\pico2w_c'
$build = Join-Path $source 'build-bt'
if (-not (Test-Path $cmake)) { throw "CMake missing: $cmake" }
if (-not (Test-Path (Join-Path $examples 'bluetooth\config\btstack_config_common.h'))) { throw "pico-examples missing from $examples" }
$options = @('-S',$source,'-B',$build,'-G','Ninja','-DPICO_BOARD=pico2_w','-DPICO_PLATFORM=rp2350-arm-s',"-DPICO_SDK_PATH=$sdk","-DPICO_EXAMPLES_PATH=$examples",'-DCLOCK_ENABLE_A2DP=ON')
& $cmake @options
if ($LASTEXITCODE -ne 0) { throw "Bluetooth configure failed: $LASTEXITCODE" }
& $cmake --build $build --target speaking_clock_c
if ($LASTEXITCODE -ne 0) { throw "Bluetooth build failed: $LASTEXITCODE" }
$uf2 = Join-Path $build 'speaking_clock_c.uf2'
if (-not (Test-Path $uf2)) { throw "UF2 missing: $uf2" }
Write-Host "[BT-BUILD] SPARE Pico 2 W only: $uf2" -ForegroundColor Green
