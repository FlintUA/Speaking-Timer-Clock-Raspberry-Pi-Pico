# Update the Speaking Clock on a Pico 2 W without touching BOOTSEL.
# Requires Windows PowerShell 5.1+ / PowerShell 7 on Windows.
# First install firmware with the guarded "bootsel arm/confirm" feature.
[CmdletBinding()]
param(
    [string]$Port = 'COM8',
    [string]$Uf2 = '',
    [switch]$Build,
    [int]$TimeoutSeconds = 45,
    [string]$Cmake = 'C:\Users\Flint123\.pico-sdk\cmake\v4.3.4\bin\cmake.exe'
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
if (-not $Uf2) { $Uf2 = Join-Path $root 'firmware\pico2w_c\build-ninja\speaking_clock_c.uf2' }
if (-not [System.IO.Path]::IsPathRooted($Uf2)) { $Uf2 = Join-Path $root $Uf2 }
$Uf2 = [System.IO.Path]::GetFullPath($Uf2)
if ($TimeoutSeconds -lt 10) { throw 'TimeoutSeconds must be at least 10.' }

function Wait-Until([scriptblock]$Predicate, [int]$Seconds, [string]$Description) {
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    while ($stopwatch.Elapsed.TotalSeconds -lt $Seconds) {
        $value = & $Predicate
        if ($value) { return $value }
        Start-Sleep -Milliseconds 400
    }
    throw "Timeout waiting for $Description ($Seconds seconds)."
}

function Get-BootselDrive {
    # RP2350 (Pico 2 W) normally has the volume label RP2350.
    # Confirm the ROM's INFO_UF2.TXT to avoid copying onto another disk.
    $matches = @(
        foreach ($disk in @(Get-CimInstance -ClassName Win32_LogicalDisk)) {
            if (-not $disk.DeviceID) { continue }
            $rootPath = $disk.DeviceID + '\'
            $marker = Join-Path $rootPath 'INFO_UF2.TXT'
            if (-not (Test-Path -LiteralPath $marker -PathType Leaf -ErrorAction SilentlyContinue)) { continue }
            try {
                $info = Get-Content -LiteralPath $marker -Raw -ErrorAction Stop
                if (($info -match 'Board-ID:\s*RP2350' -and $disk.VolumeName -eq 'RP2350') -or
                    ($info -match 'Board-ID:\s*RP2' -and $disk.VolumeName -eq 'RPI-RP2')) {
                    $disk.DeviceID
                }
            } catch { }
        }
    )
    if ($matches.Count -gt 1) { throw ('Multiple Pico BOOTSEL drives: ' + ($matches -join ', ')) }
    if ($matches.Count -eq 1) { return $matches[0] }
    return $null
}
if ($Build) {
    if (-not (Test-Path -LiteralPath $Cmake -PathType Leaf)) {
        throw "CMake not found: $Cmake"
    }
    Write-Host '[UPDATE] Building speaking_clock_c ...'
    & $Cmake --build (Join-Path $root 'firmware\pico2w_c\build-ninja') --target speaking_clock_c
    if ($LASTEXITCODE -ne 0) { throw "CMake build failed ($LASTEXITCODE). Nothing was flashed." }
}

if (-not (Test-Path -LiteralPath $Uf2 -PathType Leaf)) {
    throw "UF2 not found: $Uf2. Build first or use -Build."
}
$file = Get-Item -LiteralPath $Uf2
if ($file.Length -lt 512 -or $file.Length % 512 -ne 0) {
    throw "UF2 size looks invalid ($($file.Length) bytes). Nothing was flashed."
}
if ((Get-BootselDrive)) {
    throw 'A Pico BOOTSEL drive is already mounted. Disconnect other Pico BOOTSEL devices before continuing.'
}
if ($Port -notin [System.IO.Ports.SerialPort]::GetPortNames()) {
    throw "Serial port $Port not found. Check the port name and USB connection."
}
Write-Host "[UPDATE] UF2: $Uf2 ($($file.Length) bytes)"
Write-Host "[UPDATE] Port: $Port. Close VS Code Serial Monitor before running this script."

$serial = [System.IO.Ports.SerialPort]::new($Port, 115200)
$serial.DtrEnable = $true # TinyUSB CDC stdio normally requires DTR from terminal.
$serial.RtsEnable = $true
$serial.ReadTimeout = 2500
$serial.WriteTimeout = 2500
$serial.NewLine = "`n"
try {
    $serial.Open()
    # Allow TinyUSB CDC to finish handshake. Send ARM a single time and
    # wait for an actual reply rather than assuming it arrives in 500 ms.
    Start-Sleep -Milliseconds 700
    $serial.DiscardInBuffer()
    $serial.WriteLine('bootsel arm')
    $armedReply = ''
    $ackTimer = [System.Diagnostics.Stopwatch]::StartNew()
    while ($ackTimer.Elapsed.TotalSeconds -lt 9) {
        Start-Sleep -Milliseconds 150
        $armedReply += $serial.ReadExisting()
        if ($armedReply -match '\[BOOTSEL\] ARMED for 15s') { break }
        if ($armedReply -match '\[BOOTSEL\] REFUSED') {
            throw "Pico refused BOOTSEL; do not flash. Reply: $armedReply"
        }
    }
    if ($armedReply -notmatch '\[BOOTSEL\] ARMED for 15s') {
        throw "Device did not arm BOOTSEL within 9s. Check COM/DTR and try bootsel arm manually. Reply: $armedReply"
    }
    Write-Host '[UPDATE] BOOTSEL armed; confirming ...'
    $serial.WriteLine('bootsel confirm')
    Start-Sleep -Milliseconds 350
    try { 
        $confirmReply = $serial.ReadExisting()
        if ($confirmReply) { Write-Host ("[UPDATE] " + $confirmReply.Trim()) }
    } catch { }
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}

Write-Host '[UPDATE] Waiting for Pico BOOTSEL USB drive (RP2350/RPI-RP2) ...'
$drive = Wait-Until -Seconds $TimeoutSeconds -Description 'Pico BOOTSEL volume with valid INFO_UF2.TXT' -Predicate {
    Get-BootselDrive
}
$destination = "$drive\$($file.Name)"
Write-Host "[UPDATE] Copying $($file.Name) to $drive ..."
Copy-Item -LiteralPath $Uf2 -Destination $destination -ErrorAction Stop
Write-Host '[UPDATE] UF2 copied. Waiting for firmware serial port ...'
$portBack = $false
try {
    $null = Wait-Until -Seconds $TimeoutSeconds -Description "$Port after reboot" -Predicate {
        if ($Port -in [System.IO.Ports.SerialPort]::GetPortNames()) { return $true }
        return $false
    }
    $portBack = $true
} catch {
    Write-Warning "COM port did not reappear as $Port. Windows may have assigned a new port. Inspect Device Manager."
}
if ($portBack) {
    Write-Host "[UPDATE] SUCCESS: $Port is back. Verify 'flash-info' and 'alarm-status' manually." -ForegroundColor Green
} else {
    Write-Warning 'UF2 was copied, but startup was not confirmed. Do not assume firmware is running.'
}
 -or
                    ($info -match '(?im)^Board-ID:\s*RP2[-_A-Za-z0-9]*\s*

if ($Build) {
    if (-not (Test-Path -LiteralPath $Cmake -PathType Leaf)) {
        throw "CMake not found: $Cmake"
    }
    Write-Host '[UPDATE] Building speaking_clock_c ...'
    & $Cmake --build (Join-Path $root 'firmware\pico2w_c\build-ninja') --target speaking_clock_c
    if ($LASTEXITCODE -ne 0) { throw "CMake build failed ($LASTEXITCODE). Nothing was flashed." }
}

if (-not (Test-Path -LiteralPath $Uf2 -PathType Leaf)) {
    throw "UF2 not found: $Uf2. Build first or use -Build."
}
$file = Get-Item -LiteralPath $Uf2
if ($file.Length -lt 512 -or $file.Length % 512 -ne 0) {
    throw "UF2 size looks invalid ($($file.Length) bytes). Nothing was flashed."
}
if ((Get-BootselDrive)) {
    throw 'RPI-RP2 drive already mounted. Disconnect other Pico BOOTSEL devices before continuing.'
}
if ($Port -notin [System.IO.Ports.SerialPort]::GetPortNames()) {
    throw "Serial port $Port not found. Check the port name and USB connection."
}
Write-Host "[UPDATE] UF2: $Uf2 ($($file.Length) bytes)"
Write-Host "[UPDATE] Port: $Port. Close VS Code Serial Monitor before running this script."

$serial = [System.IO.Ports.SerialPort]::new($Port, 115200)
$serial.DtrEnable = $true # TinyUSB CDC stdio normally requires DTR from terminal.
$serial.RtsEnable = $true
$serial.ReadTimeout = 2500
$serial.WriteTimeout = 2500
$serial.NewLine = "`n"
try {
    $serial.Open()
    # Allow TinyUSB CDC to finish handshake. Send ARM a single time and
    # wait for an actual reply rather than assuming it arrives in 500 ms.
    Start-Sleep -Milliseconds 700
    $serial.DiscardInBuffer()
    $serial.WriteLine('bootsel arm')
    $armedReply = ''
    $ackTimer = [System.Diagnostics.Stopwatch]::StartNew()
    while ($ackTimer.Elapsed.TotalSeconds -lt 9) {
        Start-Sleep -Milliseconds 150
        $armedReply += $serial.ReadExisting()
        if ($armedReply -match '\[BOOTSEL\] ARMED for 15s') { break }
        if ($armedReply -match '\[BOOTSEL\] REFUSED') {
            throw "Pico refused BOOTSEL; do not flash. Reply: $armedReply"
        }
    }
    if ($armedReply -notmatch '\[BOOTSEL\] ARMED for 15s') {
        throw "Device did not arm BOOTSEL within 9s. Check COM/DTR and try bootsel arm manually. Reply: $armedReply"
    }
    Write-Host '[UPDATE] BOOTSEL armed; confirming ...'
    $serial.WriteLine('bootsel confirm')
    Start-Sleep -Milliseconds 350
    try { 
        $confirmReply = $serial.ReadExisting()
        if ($confirmReply) { Write-Host ("[UPDATE] " + $confirmReply.Trim()) }
    } catch { }
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}

Write-Host '[UPDATE] Waiting for RPI-RP2 USB drive ...'
$drive = Wait-Until -Seconds $TimeoutSeconds -Description 'RPI-RP2 volume' -Predicate {
    Get-BootselDrive
}
$destination = "$drive\$($file.Name)"
Write-Host "[UPDATE] Copying $($file.Name) to $drive ..."
Copy-Item -LiteralPath $Uf2 -Destination $destination -ErrorAction Stop
Write-Host '[UPDATE] UF2 copied. Waiting for firmware serial port ...'
$portBack = $false
try {
    $null = Wait-Until -Seconds $TimeoutSeconds -Description "$Port after reboot" -Predicate {
        if ($Port -in [System.IO.Ports.SerialPort]::GetPortNames()) { return $true }
        return $false
    }
    $portBack = $true
} catch {
    Write-Warning "COM port did not reappear as $Port. Windows may have assigned a new port. Inspect Device Manager."
}
if ($portBack) {
    Write-Host "[UPDATE] SUCCESS: $Port is back. Verify 'flash-info' and 'alarm-status' manually." -ForegroundColor Green
} else {
    Write-Warning 'UF2 was copied, but startup was not confirmed. Do not assume firmware is running.'
}
 -and
                     $disk.VolumeName -eq 'RPI-RP2')) {
                    $disk.DeviceID
                }
            } catch { }
        }
    )
    if ($matches.Count -gt 1) {
        throw "Multiple validated Pico BOOTSEL drives detected: $($matches -join ', '). Disconnect the other devices."
    }
    if ($matches.Count -eq 1) { return $matches[0] }
    return $null
}

if ($Build) {
    if (-not (Test-Path -LiteralPath $Cmake -PathType Leaf)) {
        throw "CMake not found: $Cmake"
    }
    Write-Host '[UPDATE] Building speaking_clock_c ...'
    & $Cmake --build (Join-Path $root 'firmware\pico2w_c\build-ninja') --target speaking_clock_c
    if ($LASTEXITCODE -ne 0) { throw "CMake build failed ($LASTEXITCODE). Nothing was flashed." }
}

if (-not (Test-Path -LiteralPath $Uf2 -PathType Leaf)) {
    throw "UF2 not found: $Uf2. Build first or use -Build."
}
$file = Get-Item -LiteralPath $Uf2
if ($file.Length -lt 512 -or $file.Length % 512 -ne 0) {
    throw "UF2 size looks invalid ($($file.Length) bytes). Nothing was flashed."
}
if ((Get-BootselDrive)) {
    throw 'RPI-RP2 drive already mounted. Disconnect other Pico BOOTSEL devices before continuing.'
}
if ($Port -notin [System.IO.Ports.SerialPort]::GetPortNames()) {
    throw "Serial port $Port not found. Check the port name and USB connection."
}
Write-Host "[UPDATE] UF2: $Uf2 ($($file.Length) bytes)"
Write-Host "[UPDATE] Port: $Port. Close VS Code Serial Monitor before running this script."

$serial = [System.IO.Ports.SerialPort]::new($Port, 115200)
$serial.DtrEnable = $true # TinyUSB CDC stdio normally requires DTR from terminal.
$serial.RtsEnable = $true
$serial.ReadTimeout = 2500
$serial.WriteTimeout = 2500
$serial.NewLine = "`n"
try {
    $serial.Open()
    # Allow TinyUSB CDC to finish handshake. Send ARM a single time and
    # wait for an actual reply rather than assuming it arrives in 500 ms.
    Start-Sleep -Milliseconds 700
    $serial.DiscardInBuffer()
    $serial.WriteLine('bootsel arm')
    $armedReply = ''
    $ackTimer = [System.Diagnostics.Stopwatch]::StartNew()
    while ($ackTimer.Elapsed.TotalSeconds -lt 9) {
        Start-Sleep -Milliseconds 150
        $armedReply += $serial.ReadExisting()
        if ($armedReply -match '\[BOOTSEL\] ARMED for 15s') { break }
        if ($armedReply -match '\[BOOTSEL\] REFUSED') {
            throw "Pico refused BOOTSEL; do not flash. Reply: $armedReply"
        }
    }
    if ($armedReply -notmatch '\[BOOTSEL\] ARMED for 15s') {
        throw "Device did not arm BOOTSEL within 9s. Check COM/DTR and try bootsel arm manually. Reply: $armedReply"
    }
    Write-Host '[UPDATE] BOOTSEL armed; confirming ...'
    $serial.WriteLine('bootsel confirm')
    Start-Sleep -Milliseconds 350
    try { 
        $confirmReply = $serial.ReadExisting()
        if ($confirmReply) { Write-Host ("[UPDATE] " + $confirmReply.Trim()) }
    } catch { }
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}

Write-Host '[UPDATE] Waiting for RPI-RP2 USB drive ...'
$drive = Wait-Until -Seconds $TimeoutSeconds -Description 'RPI-RP2 volume' -Predicate {
    Get-BootselDrive
}
$destination = "$drive\$($file.Name)"
Write-Host "[UPDATE] Copying $($file.Name) to $drive ..."
Copy-Item -LiteralPath $Uf2 -Destination $destination -ErrorAction Stop
Write-Host '[UPDATE] UF2 copied. Waiting for firmware serial port ...'
$portBack = $false
try {
    $null = Wait-Until -Seconds $TimeoutSeconds -Description "$Port after reboot" -Predicate {
        if ($Port -in [System.IO.Ports.SerialPort]::GetPortNames()) { return $true }
        return $false
    }
    $portBack = $true
} catch {
    Write-Warning "COM port did not reappear as $Port. Windows may have assigned a new port. Inspect Device Manager."
}
if ($portBack) {
    Write-Host "[UPDATE] SUCCESS: $Port is back. Verify 'flash-info' and 'alarm-status' manually." -ForegroundColor Green
} else {
    Write-Warning 'UF2 was copied, but startup was not confirmed. Do not assume firmware is running.'
}
