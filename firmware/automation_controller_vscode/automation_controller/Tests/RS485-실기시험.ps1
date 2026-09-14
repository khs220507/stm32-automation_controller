param(
    [string]$PortName = 'COM4',
    [int]$PingCount = 100,
    [string]$EvidenceDirectory = ''
)

$ErrorActionPreference = 'Stop'
if ($PingCount -lt 1 -or $PingCount -gt 10000) { throw 'PingCount 범위: 1~10000' }
if ([string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
    $repo = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
    $EvidenceDirectory = Join-Path $repo ('docs/측정/' + (Get-Date -Format 'yyyy-MM-dd') + '-RS485-WPF')
}
New-Item -ItemType Directory -Path $EvidenceDirectory -Force | Out-Null
$log = [Collections.Generic.List[string]]::new()
$serial = [IO.Ports.SerialPort]::new($PortName,115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
$serial.ReadTimeout = 800
$serial.WriteTimeout = 250
$serial.NewLine = "`r`n"

function Record([string]$Message)
{
    $line = (Get-Date -Format o) + ' ' + $Message
    $log.Add($line)
    Write-Host $line
}

function Request([string]$Command)
{
    $serial.Write($Command + "`r`n")
    $answer = $serial.ReadLine()
    if ($answer -eq $Command) { $answer = $serial.ReadLine() }
    Record "TX $Command RX $answer"
    return $answer
}

try {
    $serial.Open()
    Start-Sleep -Milliseconds 200
    $serial.DiscardInBuffer()
    Record "$PortName 115200 8N1 opened; physical wiring inspection is not inferred from this test"
    for ($i = 0; $i -lt $PingCount; $i++) {
        $answer = Request 'PING'
        if ($answer -ne 'OK,PING,PONG') { throw "PING 응답 불일치: $answer" }
        Start-Sleep -Milliseconds 5
    }
    Record "PASS $PingCount PING/PONG transactions"
    foreach ($command in @('GET_STATUS','CHECK_W5500','CHECK_MPU6050','CHECK_HCSR04','CONFIG_ACCEL')) {
        $answer = Request $command
        if (-not ($answer.StartsWith("OK,$command,") -or $answer.StartsWith("ERR,$command,"))) {
            throw "응답 형식 불일치: $answer"
        }
        # ERR와 센서 TIMEOUT은 수신 기록이며 센서 시험 성공으로 판정하지 않는다.
    }
    Start-Sleep -Milliseconds 150
    $answer = Request 'READ_ACCEL'
    if (-not ($answer.StartsWith('OK,READ_ACCEL,') -or $answer.StartsWith('ERR,READ_ACCEL,'))) {
        throw "응답 형식 불일치: $answer"
    }
}
catch {
    Record "FAIL $($_.Exception.Message)"
    throw
}
finally {
    $serial.Dispose()
    $log | Add-Content -LiteralPath (Join-Path $EvidenceDirectory 'RS485-실기시험-로그.txt') -Encoding UTF8
}
