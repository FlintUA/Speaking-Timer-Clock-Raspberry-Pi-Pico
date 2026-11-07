# Update the Speaking Clock on a Pico 2 W without touching BOOTSEL.
# Requires Windows PowerShell 5.1+ / PowerShell 7 on Windows.
# First install firmware with the guarded "bootsel arm/confirm" feature.
[CmdletBinding()]
param(
    [string]$Port = 'COM8',
    [string]$Uf2 = '',
    [switch]$Build,
    [switch]$Bluetooth,
    [switch]$Mp3,
    [switch]$NoWifi,
    [string]$JblAddress = '',
    [switch]$VerifyOnly,
    [int]$TimeoutSeconds = 45,
    [string]$Cmake = (Join-Path $env:USERPROFILE '.pico-sdk\cmake\v4.3.4\bin\cmake.exe')
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$expectedBuild = ''
if (-not $Uf2) {
    $buildFolder = if ($Bluetooth) { 'build-bt' } else { 'build-ninja' }
    $Uf2 = Join-Path $root "firmware\pico2w_c\$buildFolder\speaking_clock_c.uf2"
}
if (-not [System.IO.Path]::IsPathRooted($Uf2)) { $Uf2 = Join-Path $root $Uf2 }
$Uf2 = [System.IO.Path]::GetFullPath($Uf2)
if ($VerifyOnly) {
    $identity = [IO.Path]::ChangeExtension($Uf2, '.build-id.txt')
    if (Test-Path -LiteralPath $identity) {
        $expectedBuild = (Get-Content -LiteralPath $identity -Raw).Trim()
    }
}
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
    # This updater targets Pico 2 W / RP2350 only, not RP2040.
    # Never choose a disk based on the volume label alone.
    $found = @(
        foreach ($disk in @(Get-CimInstance -ClassName Win32_LogicalDisk)) {
            if (-not $disk.DeviceID -or $disk.VolumeName -ne 'RP2350') { continue }
            $marker = Join-Path ($disk.DeviceID + '\') 'INFO_UF2.TXT'
            if (-not (Test-Path -LiteralPath $marker -PathType Leaf -ErrorAction SilentlyContinue)) { continue }
            try {
                $info = Get-Content -LiteralPath $marker -Raw -ErrorAction Stop
                if ($info -match '(?m)^Board-ID:\s*RP2350\s*$') { $disk.DeviceID }
            } catch { }
        }
    )
    if ($found.Count -gt 1) {
        throw ('Multiple validated RP2350 BOOTSEL drives: ' + ($found -join ', '))
    }
    if ($found.Count -eq 1) { return $found[0] }
    return $null
}

function New-PicoSerial([string]$PortName) {
    $serial = [System.IO.Ports.SerialPort]::new($PortName, 115200)
    $serial.DtrEnable = $true
    $serial.RtsEnable = $true
    $serial.ReadTimeout = 1500
    $serial.WriteTimeout = 2500
    $serial.NewLine = "`n"
    return $serial
}

function Read-PicoReply($Serial, [string]$Command, [string]$Expected, [int]$Seconds = 4) {
    $Serial.DiscardInBuffer()
    $Serial.WriteLine($Command)
    $reply = ''
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt $Seconds) {
        Start-Sleep -Milliseconds 150
        $reply += $Serial.ReadExisting()
        if ($reply -match $Expected) { break }
    }
    return $reply
}

function Test-PicoFirmware([string]$PortName, [bool]$IncludeAlarms) {
    $serial = New-PicoSerial $PortName
    try {
        $serial.Open()
        Start-Sleep -Milliseconds 800
        $flash = Read-PicoReply $serial 'flash-info' '\[FLASH\] slot1=' 4
        Write-Host '[VERIFY] flash-info'
        Write-Host $flash
        $validSlot = $flash -match '\[FLASH\] (slot[01]|legacy_slot0)=VALID'
        $blankSlots = [regex]::Matches($flash, '\[FLASH\] (slot[01]|legacy_slot0)=BLANK').Count
        if ($flash -notmatch '\[FLASH\][^\r\n]*guard=OK' -or
            (-not $validSlot -and $blankSlots -ne 3)) {
            throw 'Flash guard or configuration slot verification failed.'
        }
        if ($expectedBuild) {
            $version = Read-PicoReply $serial 'version' '\[VERSION\]' 4
            Write-Host $version
            $buildPattern = 'build=' + [regex]::Escape($expectedBuild) + '(\s|$)'
            if ($version -notmatch $buildPattern) {
                throw 'Firmware build identity does not match the selected UF2.'
            }
        }

        if ($IncludeAlarms) {
            $alarms = Read-PicoReply $serial 'alarm-status' '\[ALARM\] 5 enabled=' 4
            Write-Host '[VERIFY] alarm-status'
            Write-Host $alarms
            if ($alarms -notmatch '\[ALARM\][^\r\n]*active=') {
                throw 'Alarm status response missing.'
            }
        }
        return $true
    } finally {
        if ($serial.IsOpen) { $serial.Close() }
        $serial.Dispose()
    }
}

if ($Mp3 -and -not $Bluetooth) {
    throw '-Mp3 requires -Bluetooth for the experimental A2DP clock build.'
}
if ($Bluetooth -and -not $Build -and -not $VerifyOnly) {
    Write-Host '[UPDATE] Bluetooth selected: using existing build-bt UF2 (no build requested).'
}
if ($VerifyOnly) {
    if ($Build) { throw 'Cannot combine -VerifyOnly with -Build.' }
    if ($Port -notin [System.IO.Ports.SerialPort]::GetPortNames()) {
        throw "Serial port $Port not found."
    }
    Write-Host "[VERIFY] Connecting to $Port ..."
    $null = Test-PicoFirmware $Port $true
    Write-Host '[VERIFY] SUCCESS: Flash and alarm diagnostics passed.' -ForegroundColor Green
    return
}

if ($Build) {
    if ($Bluetooth) {
        $buildBt = Join-Path $root 'build-clock-bt.ps1'
        if (-not (Test-Path -LiteralPath $buildBt -PathType Leaf)) {
            throw "Bluetooth build script missing: $buildBt"
        }
        Write-Host '[UPDATE] Building Bluetooth A2DP firmware (build-bt) ...'
        & $buildBt -JblAddress $JblAddress -Mp3:$Mp3 -NoWifi:$NoWifi
        if (-not $?) { throw 'Bluetooth build failed. Nothing was flashed.' }
    } else {
        if (-not (Test-Path -LiteralPath $Cmake -PathType Leaf)) {
            throw "CMake not found: $Cmake"
        }
        Write-Host '[UPDATE] Building speaking_clock_c (no Bluetooth) ...'
        & $Cmake --build (Join-Path $root 'firmware\pico2w_c\build-ninja') --target speaking_clock_c
        if ($LASTEXITCODE -ne 0) { throw "CMake build failed ($LASTEXITCODE). Nothing was flashed." }
    }
}

if (-not (Test-Path -LiteralPath $Uf2 -PathType Leaf)) {
    throw "UF2 not found: $Uf2. Build first or use -Build."
}
$file = Get-Item -LiteralPath $Uf2
$buildIdentity = [IO.Path]::ChangeExtension($Uf2, '.build-id.txt')
if (Test-Path -LiteralPath $buildIdentity) {
    $expectedBuild = (Get-Content -LiteralPath $buildIdentity -Raw).Trim()
}
if ($file.Length -lt 512 -or $file.Length % 512 -ne 0) {
    throw "UF2 size looks invalid ($($file.Length) bytes). Nothing was flashed."
}
Write-Host "[UPDATE] UF2: $Uf2 ($($file.Length) bytes)"

$drive = Get-BootselDrive
if ($drive) {
    Write-Host "[UPDATE] RP2350 already in BOOTSEL on $drive; skipping serial commands."
} else {
    if ($Port -notin [System.IO.Ports.SerialPort]::GetPortNames()) {
        throw "RP2350 BOOTSEL drive not found and serial port $Port is unavailable."
    }
    Write-Host "[UPDATE] Connecting to $Port. Close other serial monitors first."
    $serial = New-PicoSerial $Port
    try {
        $serial.Open()
        Start-Sleep -Milliseconds 700
        $armed = Read-PicoReply $serial 'bootsel arm' '\[BOOTSEL\] (ARMED for 15s|REFUSED)' 9
        if ($armed -match '\[BOOTSEL\] REFUSED') {
            throw "Pico refused BOOTSEL: $armed"
        }
        if ($armed -notmatch '\[BOOTSEL\] ARMED for 15s') {
            throw "BOOTSEL ARM was not confirmed: $armed"
        }
        Write-Host '[UPDATE] BOOTSEL armed; confirming ...'
        $serial.WriteLine('bootsel confirm')
        Start-Sleep -Milliseconds 350
    } finally {
        if ($serial.IsOpen) { $serial.Close() }
        $serial.Dispose()
    }
    Write-Host '[UPDATE] Waiting for RP2350 BOOTSEL drive ...'
    $drive = Wait-Until -Seconds $TimeoutSeconds -Description 'validated RP2350 BOOTSEL drive' -Predicate {
        Get-BootselDrive
    }
}

# Recheck the exact identity immediately before copying the UF2.
if ((Get-BootselDrive) -ne $drive) {
    throw "RP2350 BOOTSEL drive $drive disappeared or changed."
}
$destination = Join-Path ($drive + '\') $file.Name
Write-Host "[UPDATE] Copying $($file.Name) to $drive ..."
Copy-Item -LiteralPath $Uf2 -Destination $destination -ErrorAction Stop
Write-Host '[UPDATE] UF2 copied. Waiting for firmware verification ...'

$verified = $false
$lastFailure = ''
$timer = [System.Diagnostics.Stopwatch]::StartNew()
while ($timer.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
    Start-Sleep -Milliseconds 500
    if ($Port -notin [System.IO.Ports.SerialPort]::GetPortNames()) { continue }
    try {
        $null = Test-PicoFirmware $Port $true
        $verified = $true
        break
    } catch {
        $lastFailure = $_.Exception.Message
    }
}
if (-not $verified) {
    if ($lastFailure) { Write-Warning $lastFailure }
    throw "UF2 was copied, but firmware startup on $Port was not verified."
}
Write-Host '[UPDATE] SUCCESS: Firmware responds and Flash guard is OK.' -ForegroundColor Green
