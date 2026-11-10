# Provision the standalone STA prototype for this boot only.
[CmdletBinding()]
param([string]$Port = 'COM8')

$ErrorActionPreference = 'Stop'
$serial = [IO.Ports.SerialPort]::new($Port, 115200)
$serial.DtrEnable = $true
$serial.NewLine = "`n"
$serial.WriteTimeout = 2500
$urlPattern = '(?m)^\[PHONE-STA\] URL (http://(?:[0-9]{1,3}\.){3}[0-9]{1,3}/)\r?\n'
$errorPattern = '(?m)^\[PHONE-STA\] ERROR ([^\r\n]+)\r?\n'

function Wait-Reply([string]$Pattern, [int]$Seconds) {
    $reply = ''
    $watch = [Diagnostics.Stopwatch]::StartNew()
    do {
        Start-Sleep -Milliseconds 100
        $reply += $serial.ReadExisting()
    } until ($reply -match $Pattern -or $watch.Elapsed.TotalSeconds -ge $Seconds)
    return $reply
}

try {
    $serial.Open()
    Start-Sleep -Milliseconds 500
    $serial.WriteLine('phone-sta-status')
    $ready = '(?m)^\[PHONE-STA\] Ready for local Wi-Fi setup\r?\n'
    $reply = Wait-Reply "$ready|$urlPattern|$errorPattern" 5
    if ($reply -match $urlPattern) {
        Write-Output "Open $($Matches[1]) from the home Wi-Fi network."
        return
    }
    if ($reply -match $errorPattern) { throw $Matches[1] }
    if ($reply -notmatch $ready) { throw 'The standalone home Wi-Fi prototype did not respond.' }

    $ssid = Read-Host 'Home Wi-Fi SSID (2.4 GHz, 1-32 ASCII characters)'
    if ($ssid -notmatch '^[\x20-\x7E]{1,32}$' -or $ssid.Contains('|')) {
        throw 'SSID must contain 1-32 printable ASCII characters without |.'
    }
    $secret = Read-Host 'WPA2 password (leave empty for an open network)' -AsSecureString
    $pointer = [IntPtr]::Zero
    try {
        $pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secret)
        $plain = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer)
        if ($plain -and ($plain -notmatch '^[\x20-\x7E]{8,63}$' -and $plain -notmatch '^[0-9a-fA-F]{64}$')) {
            throw 'Enter an 8-63 character ASCII WPA2 password or a 64-digit hexadecimal key.'
        }
        $serial.WriteLine("wifi-config $ssid|$plain")
    } finally {
        $plain = $null
        if ($pointer -ne [IntPtr]::Zero) { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer) }
        $secret.Dispose()
    }
    Write-Output 'Waiting for the router to assign an address...'
    $reply = Wait-Reply "$urlPattern|$errorPattern" 30
    if ($reply -match $errorPattern) { throw $Matches[1] }
    if ($reply -match $urlPattern) {
        Write-Output "Open $($Matches[1]) from the home Wi-Fi network."
    } else {
        throw 'No address was reported. Run the script again to check the prototype status.'
    }
} finally {
    $serial.Dispose()
}
