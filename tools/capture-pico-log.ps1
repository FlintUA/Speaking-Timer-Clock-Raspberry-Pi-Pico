# Read-only USB diagnostics. Close other serial monitors before starting.
[CmdletBinding()]
param(
    [string]$Port = 'COM8',
    [ValidateRange(1,120)][int]$Minutes = 30,
    [Parameter(Mandatory=$true)][string]$LogPath,
    [string]$StopFile = ''
)
$ErrorActionPreference='Stop'
$LogPath=[IO.Path]::GetFullPath($LogPath)
[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($LogPath))
$serial=[IO.Ports.SerialPort]::new($Port,115200)
$serial.DtrEnable=$true
$serial.RtsEnable=$true
$serial.WriteTimeout=2500
$serial.NewLine="`n"
$writer=$null
$pending=''
$lines=0
$events=0
function Record-Line([string]$Line) {
    $stamp=[DateTime]::UtcNow.ToString('o')
    $writer.WriteLine("[$stamp] $Line")
    $script:lines++
    if($Line -match '\[TONE\]|\[BT\]|Stream started|Stream suspended|Stream released|Signaling released|A2DP Source: Connected|\[VERSION\]|\[NTP\]|\[AUDIO-SETTINGS\]') {
        $script:events++
        Write-Output "[$stamp] $Line"
    }
}
try {
    $serial.Open()
    $writer=[IO.StreamWriter]::new($LogPath,$false,[Text.UTF8Encoding]::new($false))
    $writer.AutoFlush=$true
    Record-Line "[CAPTURE] port=$Port duration=${Minutes}min; PC timestamps are UTC"
    $watch=[Diagnostics.Stopwatch]::StartNew()
    $commands=@('version','bt-status','audio-settings','ntp-status')
    $commandIndex=0
    $nextCommand=1
    $nextProgress=60
    while($watch.Elapsed.TotalMinutes -lt $Minutes) {
        if($StopFile -and (Test-Path -LiteralPath $StopFile)) { break }
        if($commandIndex -lt $commands.Count -and $watch.Elapsed.TotalSeconds -ge $nextCommand) {
            $command=$commands[$commandIndex++]
            Record-Line "[CAPTURE] query=$command"
            $serial.WriteLine($command)
            $nextCommand+=1
        }
        $pending+=$serial.ReadExisting()
        while(($newline=$pending.IndexOf("`n")) -ge 0) {
            Record-Line ($pending.Substring(0,$newline).TrimEnd("`r"))
            $pending=$pending.Substring($newline+1)
        }
        if($watch.Elapsed.TotalSeconds -ge $nextProgress) {
            Write-Output "[CAPTURE] elapsed=$([int]$watch.Elapsed.TotalSeconds)s lines=$lines events=$events"
            $nextProgress+=60
        }
        Start-Sleep -Milliseconds 100
    }
    if($pending) { Record-Line $pending }
    Record-Line "[CAPTURE] completed; elapsed=$([int]$watch.Elapsed.TotalSeconds)s"
} finally {
    if($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
    if($null -ne $writer) { $writer.Dispose() }
}
Write-Output "[CAPTURE] saved=$LogPath"
