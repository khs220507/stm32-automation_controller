using System.Diagnostics;
using System.IO;
using System.IO.Ports;
using System.Net;
using System.Net.Sockets;
using System.Text;
using UltrasonicMonitor.Protocol;

namespace UltrasonicMonitor;

internal static class PingProbe
{
    internal static async Task TcpAsync(IPAddress address, int port, Action<string> log,
        CancellationToken cancellationToken)
    {
        using var client = new TcpClient();
        using var connectionDeadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        connectionDeadline.CancelAfter(3000);
        await client.ConnectAsync(address, port, connectionDeadline.Token);
        log("연결됨");
        using var responseDeadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        responseDeadline.CancelAfter(800);
        NetworkStream stream = client.GetStream();
        await stream.WriteAsync(Encoding.ASCII.GetBytes("PING\r\n"), responseDeadline.Token);
        log("TX PING<CR><LF>");
        var lines = new AsciiLineBuffer(maxLineLength: 64);
        byte[] buffer = new byte[128];
        while (true)
        {
            int count = await stream.ReadAsync(buffer, responseDeadline.Token);
            if (count == 0)
            {
                throw new IOException("PONG 수신 전에 연결이 종료됐습니다.");
            }
            bool received = Accept(lines, Encoding.ASCII.GetString(buffer, 0, count), log);
            if (received)
            {
                return;
            }
        }
    }

    internal static async Task SerialAsync(string name, Action<string> log,
        CancellationToken cancellationToken)
    {
        // 포트 열기와 읽기·쓰기는 UI 스레드 밖에서 수행한다.
        await Task.Run(async () =>
        {
            using var port = new SerialPort(name, 115200, Parity.None, 8, StopBits.One)
            {
                Encoding = Encoding.ASCII,
                Handshake = Handshake.None,
                DtrEnable = false,
                RtsEnable = false,
                ReadTimeout = 100,
                WriteTimeout = 250
            };
            cancellationToken.ThrowIfCancellationRequested();
            port.Open();
            log("포트 열림 · 115200 8N1");
            await Task.Delay(200, cancellationToken);
            port.DiscardInBuffer();
            port.Write("PING\r\n");
            log("TX PING<CR><LF>");
            var lines = new AsciiLineBuffer(maxLineLength: 64);
            var watch = Stopwatch.StartNew();
            while (watch.ElapsedMilliseconds < 800)
            {
                cancellationToken.ThrowIfCancellationRequested();
                string chunk = port.ReadExisting();
                bool received = Accept(lines, chunk, log);
                if (received)
                {
                    return;
                }
                await Task.Delay(5, cancellationToken);
            }
            throw new TimeoutException("PONG 응답 없음 (800 ms)");
        }, cancellationToken);
    }

    internal static bool Accept(AsciiLineBuffer buffer, string chunk, Action<string> log)
    {
        LineBufferResult result = buffer.Append(chunk);
        if (result.OverflowCount > 0)
        {
            throw new IOException("수신 줄 길이 초과");
        }
        foreach (string line in result.Lines)
        {
            log("RX " + line);
            if (line == "OK,PING,PONG")
            {
                return true;
            }
        }
        return false;
    }
}
