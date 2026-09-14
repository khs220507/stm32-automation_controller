using System.IO.Ports;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;

namespace UltrasonicMonitor.Tests;

[TestClass]
[DoNotParallelize]
public sealed class Rs485TransportTests
{
    private static object? Call(MainWindow window, string name, params object?[] args)
    {
        return typeof(MainWindow).GetMethod(name, BindingFlags.Instance | BindingFlags.NonPublic)!
            .Invoke(window, args);
    }

    private static void Set(MainWindow window, string name, object? value)
    {
        typeof(MainWindow).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!
            .SetValue(window, value);
    }

    private static string Text(MainWindow window, string name)
    {
        return ((TextBlock)window.FindName(name)).Text;
    }

    private static void Pump(Task task)
    {
        var frame = new DispatcherFrame();
        var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(10) };
        var deadline = DateTime.UtcNow.AddSeconds(5);
        timer.Tick += (_, _) =>
        {
            if (task.IsCompleted || DateTime.UtcNow > deadline)
            {
                frame.Continue = false;
            }
        };
        timer.Start();
        Dispatcher.PushFrame(frame);
        timer.Stop();
        Assert.IsTrue(task.IsCompleted);
        task.GetAwaiter().GetResult();
    }

    [STATestMethod]
    public void SerialSelectionFitsCompactWindowAndRetainsTcpControls()
    {
        var window = new MainWindow();
        try
        {
            var transport = (ComboBox)window.FindName("TransportComboBox");
            transport.SelectedIndex = 1;
            Assert.AreEqual(Visibility.Collapsed, ((TextBox)window.FindName("HostTextBox")).Visibility);
            Assert.AreEqual("RS-485 확인", ((Button)window.FindName("UartCheckButton")).Content);
            var root = (FrameworkElement)window.Content;
            var size = new Size(980, 700);
            root.Measure(size);
            root.Arrange(new Rect(size));
            root.UpdateLayout();
            foreach (string name in new[] { "TransportComboBox", "Rs485PortComboBox", "ConnectButton" })
            {
                var element = (FrameworkElement)window.FindName(name);
                var bounds = element.TransformToAncestor(root).TransformBounds(new Rect(element.RenderSize));
                Assert.IsTrue(bounds.Width > 0 && bounds.Height > 0, name);
                Assert.IsTrue(bounds.Left >= 0 && bounds.Right <= root.ActualWidth + 1, name);
            }
            transport.SelectedIndex = 0;
            Assert.AreEqual(Visibility.Visible, ((TextBox)window.FindName("HostTextBox")).Visibility);
            Assert.AreEqual(Visibility.Collapsed, ((ComboBox)window.FindName("Rs485PortComboBox")).Visibility);
        }
        finally
        {
            window.Close();
        }
    }

    [STATestMethod]
    public void SerialEchoAndFragmentedResponsesUpdateRealDashboardFields()
    {
        var window = new MainWindow();
        using var port = new SerialPort("COM99");
        try
        {
            ((ComboBox)window.FindName("TransportComboBox")).SelectedIndex = 1;
            Set(window, "_rs485Port", port);
            Call(window, "SetConnectionState", true, "COM99");
            Set(window, "_pendingCommand", "PING");
            Call(window, "ProcessRs485Chunk", port, "PING\r\nOK,PI");
            Assert.AreEqual("확인 전", Text(window, "UartStatusText"));
            Call(window, "ProcessRs485Chunk", port, "NG,PONG\r\n");
            StringAssert.Contains(Text(window, "UartStatusText"), "PONG");
            StringAssert.Contains(Text(window, "ConnectionStatusText"), "응답 확인됨");
            Set(window, "_pendingCommand", "READ_ACCEL");
            Call(window, "ProcessRs485Chunk", port, "OK,READ_ACCEL,-16384,0,16384\r\n");
            Assert.AreEqual("-1.000 g", Text(window, "AccelXText"));
            Assert.AreEqual("1.000 g", Text(window, "AccelZText"));
            Set(window, "_pendingCommand", "CHECK_HCSR04");
            Call(window, "ProcessRs485Chunk", port, "OK,CHECK_HCSR04,OK,25,1450\r\n");
            Assert.AreEqual("25", Text(window, "DistanceText"));
            Set(window, "_pendingCommand", "CHECK_W5500");
            Call(window, "ProcessRs485Chunk", port, "ERR,CHECK_W5500,TIMEOUT\r\n");
            StringAssert.Contains(Text(window, "W5500StatusText"), "시간 초과");
        }
        finally
        {
            window.Close();
        }
    }

    [STATestMethod]
    public void DeadlineClosesSerialAndOldCallbacksCannotChangeReconnectedUi()
    {
        var window = new MainWindow();
        using var oldPort = new SerialPort("COM98");
        using var newPort = new SerialPort("COM99");
        var previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext(window.Dispatcher));
        try
        {
            Set(window, "_rs485Port", oldPort);
            Call(window, "SetConnectionState", true, "COM98");
            Set(window, "_pendingCommand", "PING");
            Call(window, "ProcessRs485Chunk", oldPort, "OK,PI");
            Pump((Task)Call(window, "WaitForResponseTimeoutAsync", "PING", CancellationToken.None)!);
            StringAssert.Contains(Text(window, "ConnectionStatusText"), "연결 안 됨");
            StringAssert.Contains(Text(window, "UartStatusText"), "500 ms");
            Set(window, "_rs485Port", newPort);
            Call(window, "SetConnectionState", true, "COM99");
            Set(window, "_pendingCommand", "PING");
            Call(window, "ProcessRs485Chunk", oldPort, "NG,PONG\r\n");
            Assert.AreEqual("확인 전", Text(window, "UartStatusText"));
            Call(window, "ProcessRs485Chunk", newPort, "OK,PING,PONG\r\n");
            StringAssert.Contains(Text(window, "UartStatusText"), "PONG");
        }
        finally
        {
            window.Close();
            SynchronizationContext.SetSynchronizationContext(previous);
        }
    }
}
