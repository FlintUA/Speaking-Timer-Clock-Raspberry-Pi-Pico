# Provision a Pico locally without echoing the password or putting it in shell history.
[CmdletBinding()]
param([string]$Port = 'COM8')
$ErrorActionPreference = 'Stop'
$serial = [IO.Ports.SerialPort]::new($Port, 115200)
$serial.DtrEnable = $true
$serial.RtsEnable = $true
$serial.NewLine = "`n"
$serial.WriteTimeout = 2500
function Read-Reply([string]$Command, [string]$Pattern, [int]$Seconds = 5) {
    $serial.DiscardInBuffer()
    $serial.WriteLine($Command)
    $reply = ''
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while ($watch.Elapsed.TotalSeconds -lt $Seconds) {
        Start-Sleep -Milliseconds 100
        $reply += $serial.ReadExisting()
        if ($reply -match $Pattern) { return $reply }
    }
    return $reply
}
try {
    $serial.Open()
    Start-Sleep -Milliseconds 800
    $version = Read-Reply 'version' '\[VERSION\]'
    if ($version -notmatch '\[VERSION\] 1\.') {
        throw 'Install the current C release before provisioning. No credentials were sent.'
    }
    $ssid = Read-Host 'Wi-Fi SSID (2.4 GHz, ASCII, 1..32 characters)'
    if ($ssid.Length -lt 1 -or $ssid.Length -gt 32 -or $ssid -match '[^\x20-\x7E]|\|') {
        throw 'SSID must contain 1..32 ASCII characters and cannot contain |.'
    }
    $secret = Read-Host 'Wi-Fi password (leave empty for an open network)' -AsSecureString
    $pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secret)
    try {
        $plain = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer)
        if ($plain -match '[^\x20-\x7E]' -or
            ($plain.Length -ne 0 -and ($plain.Length -lt 8 -or $plain.Length -gt 64)) -or
            ($plain.Length -eq 64 -and $plain -notmatch '^[0-9a-fA-F]{64}$')) {
            throw 'Password must be empty or contain 8..64 ASCII characters.'
        }
        $reply = Read-Reply ("wifi-config " + $ssid + "|" + $plain) '\[WIFI\]'
    } finally {
        $plain = $null
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer)
        $secret.Dispose()
    }
    if ($reply -notmatch '\[WIFI\] Configuration accepted') {
        throw 'Configuration was not accepted. Check the firmware profile and wait for idle audio.'
    }
    $saved = Read-Reply 'save' '\[FLASH\] save=|\[FLASH\] save blocked' 8
    if ($saved -notmatch '\[FLASH\] save=OK') {
        throw 'Configuration was accepted in RAM but saving was not confirmed. Wait for autosave=OK.'
    }
    Write-Host '[WIFI] Credentials saved on the Pico. Password was not displayed.'
    $status = Read-Reply 'ntp-sync' '\[NTP\]' 5
    foreach ($line in ($status -split "\r?\n")) {
        if ($line -match '^\[NTP\]') { Write-Host $line }
    }
    Write-Host 'Use ntp-status in the serial monitor to check synchronization.'
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
