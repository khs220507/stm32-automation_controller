param([string]$PortName = 'COM3')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$evidence = Join-Path $repo 'docs/측정/2026-09-10-UART-TCP'
New-Item -ItemType Directory -Path $evidence -Force | Out-Null
$log = [Collections.Generic.List[string]]::new()
function Record($message) { $line = (Get-Date -Format o) + ' ' + $message; $log.Add($line); Write-Output $line }
$serial = [IO.Ports.SerialPort]::new($PortName,115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
$serial.ReadTimeout=1000; $serial.WriteTimeout=500; $serial.NewLine="`r`n"
$client = [Net.Sockets.TcpClient]::new()
try {
    $serial.Open(); $serial.DiscardInBuffer()
    $serial.Write("PING`r`n")
    $answer=$serial.ReadLine()
    if($answer -ne 'OK,PING,PONG'){throw "UART alone: $answer"}
    Record "UART without TCP connection: $PortName 115200 8N1 -> $answer"
    $serial.Write("CHECK_W5500`r`n")
    $answer=$serial.ReadLine()
    if($answer -ne 'ERR,UART,INVALID_COMMAND'){throw "UART unexpected sensor command response: $answer"}
    Record "UART rejects sensor command: $answer"
    if(!$client.ConnectAsync('169.254.100.2',5000).Wait(3000)){throw 'TCP connect timeout'}
    $stream=$client.GetStream(); $stream.ReadTimeout=1000; $stream.WriteTimeout=500
    $reader=[IO.StreamReader]::new($stream,[Text.Encoding]::ASCII)
    $bytes=[Text.Encoding]::ASCII.GetBytes("CONFIG_ACCEL`r`n"); $stream.Write($bytes,0,$bytes.Length)
    $answer=$reader.ReadLine(); Record "TCP CONFIG_ACCEL: $answer"
    if($answer -ne 'OK,CONFIG_ACCEL,2G'){throw 'TCP config failed'}
    Start-Sleep -Milliseconds 150
    for($i=0;$i -lt 100;$i++){
        $bytes=[Text.Encoding]::ASCII.GetBytes("READ_ACCEL`r`n"); $stream.Write($bytes,0,$bytes.Length)
        $serial.Write("PING`r`n")
        $uart=$serial.ReadLine(); $tcp=$reader.ReadLine()
        if($uart -ne 'OK,PING,PONG' -or !$tcp.StartsWith('OK,READ_ACCEL,')){throw "Cycle $i UART=$uart TCP=$tcp"}
    }
    Record "PASS 100 paired requests: UART PING/PONG + TCP READ_ACCEL; last=$tcp"
    $client.Dispose(); Start-Sleep -Milliseconds 100
    $serial.Write("PING`r`n"); $answer=$serial.ReadLine()
    if($answer -ne 'OK,PING,PONG'){throw 'UART after TCP close failed'}
    Record "UART after TCP disconnect: $answer"
}
finally {
    $serial.Dispose(); $client.Dispose()
    $log | Set-Content -LiteralPath (Join-Path $evidence 'UART-TCP-실측-로그.txt') -Encoding UTF8
}
