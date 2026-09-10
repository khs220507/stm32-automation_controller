using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Text;
using System.Windows.Controls;
using System.Windows.Threading;

namespace UltrasonicMonitor.Tests;

[TestClass]
[DoNotParallelize]
public sealed class TcpTransportTests
{
    private static object? Call(MainWindow w, string name, params object?[] args) =>
        typeof(MainWindow).GetMethod(name, BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(w, args);
    private static string Text(MainWindow w, string name) => ((TextBlock)w.FindName(name)).Text;
    private static void Pump(Task task)
    {
        var frame = new DispatcherFrame();
        var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(10) };
        var deadline = DateTime.UtcNow.AddSeconds(5);
        timer.Tick += (_, _) => { if (task.IsCompleted || DateTime.UtcNow > deadline) frame.Continue = false; };
        timer.Start(); Dispatcher.PushFrame(frame); timer.Stop();
        Assert.IsTrue(task.IsCompleted, "TCP test exceeded five seconds");
        task.GetAwaiter().GetResult();
    }
    [STATestMethod]
    public void FragmentedResponseSurvivesLongByteGapAndReconnectClearsPartialLine()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0); listener.Start();
        var w = new MainWindow();
        var previousContext = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext(w.Dispatcher));
        try
        {
            Pump(Run());
            async Task Run()
            {
                int port = ((IPEndPoint)listener.LocalEndpoint).Port;
                await (Task)Call(w, "ConnectTcpAsync", IPAddress.Loopback, port)!;
                using (var peer = await listener.AcceptTcpClientAsync())
                {
                    Call(w, "SendCommand", "PING");
                    using var reader = new StreamReader(peer.GetStream(), Encoding.ASCII, leaveOpen: true);
                    Assert.AreEqual("PING", await reader.ReadLineAsync());
                    await peer.GetStream().WriteAsync("OK,PI"u8.ToArray());
                    await Task.Delay(100);
                    await peer.GetStream().WriteAsync("NG,PONG\r\npartial"u8.ToArray());
                    await Task.Delay(50);
                    StringAssert.Contains(Text(w, "UartStatusText"), "PONG");
                }
                await Task.Delay(50);
                StringAssert.Contains(Text(w, "ConnectionStatusText"), "연결 안 됨");
                await (Task)Call(w, "ConnectTcpAsync", IPAddress.Loopback, port)!;
                using var second = await listener.AcceptTcpClientAsync();
                Call(w, "SendCommand", "CHECK_W5500");
                using var reader2 = new StreamReader(second.GetStream(), Encoding.ASCII, leaveOpen: true);
                Assert.AreEqual("CHECK_W5500", await reader2.ReadLineAsync());
                await second.GetStream().WriteAsync("unrecognized\r\nOK,CHECK_W5500,4\r\n"u8.ToArray());
                await Task.Delay(50);
                StringAssert.Contains(Text(w, "W5500StatusText"), "버전 확인됨");
                Call(w, "SendCommand", "PING");
                Assert.AreEqual("PING", await reader2.ReadLineAsync());
                await Task.Delay(600);
                StringAssert.Contains(Text(w, "ConnectionStatusText"), "연결 안 됨");
                StringAssert.Contains(Text(w, "UartStatusText"), "500 ms");
            }
        }
        finally { w.Close(); listener.Stop(); SynchronizationContext.SetSynchronizationContext(previousContext); }
    }
}
