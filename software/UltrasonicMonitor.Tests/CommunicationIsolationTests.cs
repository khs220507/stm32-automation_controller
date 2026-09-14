using System.Collections.ObjectModel;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;

namespace UltrasonicMonitor.Tests;

[TestClass]
[DoNotParallelize]
public sealed class CommunicationIsolationTests
{
    private static object? Call(MainWindow window, string name, params object?[] args)
    {
        return typeof(MainWindow).GetMethod(name, BindingFlags.Instance | BindingFlags.NonPublic)!
            .Invoke(window, args);
    }

    private static T Element<T>(MainWindow window, string name) where T : FrameworkElement
    {
        return (T)window.FindName(name);
    }

    private static void Pump(Task task)
    {
        var frame = new DispatcherFrame();
        var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(10) };
        DateTime limit = DateTime.UtcNow.AddSeconds(5);
        timer.Tick += (_, _) =>
        {
            if (task.IsCompleted || DateTime.UtcNow > limit)
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
    public void OverlappingResultsAndFailuresStayInTheirOwnCards()
    {
        var window = new MainWindow();
        var previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext(window.Dispatcher));
        try
        {
            Pump(Run());
            async Task Run()
            {
                var tcpDone = new TaskCompletionSource();
                var serialDone = new TaskCompletionSource();
                var tcpLogs = new ObservableCollection<string>();
                var rsLogs = new ObservableCollection<string>();
                Task Start(string transport, string prefix, string endpoint,
                    ObservableCollection<string> logs, TaskCompletionSource completion)
                {
                    Func<Action<string>, CancellationToken, Task> probe = async (log, token) =>
                    {
                        log("TX PING");
                        await completion.Task.WaitAsync(token);
                        log("RX OK,PING,PONG");
                    };
                    return (Task)Call(window, "RunProbeAsync", transport, endpoint,
                        Element<Button>(window, prefix + "Button"),
                        Element<TextBlock>(window, prefix + "StatusText"),
                        Element<TextBlock>(window, prefix + "TimeText"), logs,
                        Element<ListBox>(window, prefix + "LogListBox"), probe)!;
                }
                Task tcp = Start("TCP", "TcpTest", "127.0.0.1:5000", tcpLogs, tcpDone);
                Task rs = Start("RS-485", "Rs485Test", "COM99", rsLogs, serialDone);
                Assert.AreEqual("미시험", Element<TextBlock>(window, "SerialStatusText").Text);
                serialDone.SetResult();
                await rs;
                StringAssert.Contains(Element<TextBlock>(window, "Rs485TestStatusText").Text, "PONG");
                StringAssert.Contains(Element<TextBlock>(window, "TcpTestStatusText").Text, "시험 중");
                string rsTime = Element<TextBlock>(window, "Rs485TestTimeText").Text;
                tcpDone.SetException(new TimeoutException("no response"));
                await tcp;
                await Task.Delay(20);
                StringAssert.Contains(Element<TextBlock>(window, "TcpTestStatusText").Text, "실패");
                Assert.AreEqual(rsTime, Element<TextBlock>(window, "Rs485TestTimeText").Text);
                Assert.IsTrue(tcpLogs.All(line => line.Contains("[TCP 127.0.0.1:5000]")));
                Assert.IsTrue(rsLogs.All(line => line.Contains("[RS-485 COM99]")));
                Assert.AreEqual("미시험", Element<TextBlock>(window, "SerialStatusText").Text);
                Assert.AreEqual(0, Element<ListBox>(window, "SerialLogListBox").Items.Count);
            }
        }
        finally
        {
            window.Close();
            SynchronizationContext.SetSynchronizationContext(previous);
        }
    }

    [STATestMethod]
    public void TcpButtonUsesOnlyTcpAndClosesAfterFragmentedPong()
    {
        var window = new MainWindow();
        var previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext(window.Dispatcher));
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        try
        {
            Pump(Run());
            async Task Run()
            {
                Element<TextBox>(window, "TcpTestHostTextBox").Text = "127.0.0.1";
                Element<TextBox>(window, "TcpTestPortTextBox").Text = ((IPEndPoint)listener.LocalEndpoint).Port.ToString();
                Call(window, "TcpTest_Click", window, new RoutedEventArgs());
                using var peer = await listener.AcceptTcpClientAsync();
                using var reader = new StreamReader(peer.GetStream(), Encoding.ASCII, leaveOpen: true);
                Assert.AreEqual("PING", await reader.ReadLineAsync());
                await peer.GetStream().WriteAsync("PING\r\nOK,PI"u8.ToArray());
                await Task.Delay(40);
                StringAssert.Contains(Element<TextBlock>(window, "TcpTestStatusText").Text, "시험 중");
                await peer.GetStream().WriteAsync("NG,PONG\r\n"u8.ToArray());
                await Task.Delay(60);
                StringAssert.Contains(Element<TextBlock>(window, "TcpTestStatusText").Text, "PONG");
                Assert.AreEqual("미시험", Element<TextBlock>(window, "Rs485TestStatusText").Text);
                Assert.AreEqual("미시험", Element<TextBlock>(window, "SerialStatusText").Text);
                Assert.AreEqual(0, Element<ListBox>(window, "Rs485TestLogListBox").Items.Count);
                Assert.IsNull(await reader.ReadLineAsync());
            }
        }
        finally
        {
            window.Close();
            listener.Stop();
            SynchronizationContext.SetSynchronizationContext(previous);
        }
    }

    [STATestMethod]
    public void TcpEchoAloneTimesOutAndDoesNotMarkOtherTransportsSuccessful()
    {
        var window = new MainWindow();
        var previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext(window.Dispatcher));
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        try
        {
            Pump(Run());
            async Task Run()
            {
                Element<TextBox>(window, "TcpTestHostTextBox").Text = "127.0.0.1";
                Element<TextBox>(window, "TcpTestPortTextBox").Text = ((IPEndPoint)listener.LocalEndpoint).Port.ToString();
                Call(window, "TcpTest_Click", window, new RoutedEventArgs());
                using var peer = await listener.AcceptTcpClientAsync();
                await peer.GetStream().WriteAsync("PING\r\n"u8.ToArray());
                await Task.Delay(950);
                StringAssert.Contains(Element<TextBlock>(window, "TcpTestStatusText").Text, "시간");
                Assert.IsTrue(Element<Button>(window, "TcpTestButton").IsEnabled);
                Assert.AreEqual("미시험", Element<TextBlock>(window, "Rs485TestStatusText").Text);
                Assert.AreEqual("미시험", Element<TextBlock>(window, "SerialStatusText").Text);
            }
        }
        finally
        {
            window.Close();
            listener.Stop();
            SynchronizationContext.SetSynchronizationContext(previous);
        }
    }

    [STATestMethod]
    public void SameComPortIsRejectedWithoutChangingTheRunningTest()
    {
        var window = new MainWindow();
        try
        {
            var uart = Element<ComboBox>(window, "SerialPortComboBox");
            uart.ItemsSource = new[] { "COM99" };
            uart.SelectedItem = "COM99";
            var rs485 = Element<ComboBox>(window, "Rs485TestPortComboBox");
            rs485.ItemsSource = new[] { "COM99" };
            rs485.SelectedItem = "COM99";
            Field("_rs485TestPort", "COM99");
            Call(window, "SerialCheck_Click", window, new RoutedEventArgs());
            StringAssert.Contains(Element<TextBlock>(window, "SerialStatusText").Text, "사용 중");
            Assert.AreEqual(0, Element<ListBox>(window, "SerialLogListBox").Items.Count);
            Assert.AreEqual("미시험", Element<TextBlock>(window, "Rs485TestStatusText").Text);
            Field("_rs485TestPort", null);
            Field("_uartTestPort", "COM99");
            Call(window, "Rs485Test_Click", window, new RoutedEventArgs());
            StringAssert.Contains(Element<TextBlock>(window, "Rs485TestStatusText").Text, "사용 중");
            Assert.AreEqual(0, Element<ListBox>(window, "Rs485TestLogListBox").Items.Count);
            void Field(string name, string? value)
            {
                typeof(MainWindow).GetField(name, BindingFlags.NonPublic | BindingFlags.Instance)!
                    .SetValue(window, value);
            }
        }
        finally
        {
            window.Close();
        }
    }

    [STATestMethod]
    [DataRow(980.0, 700.0)]
    [DataRow(1220.0, 780.0)]
    public void ThreeSeparateCardsAndLogsFitWindow(double width, double height)
    {
        var window = new MainWindow();
        try
        {
            var root = (FrameworkElement)window.Content;
            var size = new Size(width, height);
            root.Measure(size);
            root.Arrange(new Rect(size));
            root.UpdateLayout();
            foreach (string name in new[] { "TcpTestCard", "Rs485TestCard", "UartTestCard",
                "TcpTestButton", "Rs485TestButton", "SerialCheckButton", "TcpTestLogListBox",
                "Rs485TestLogListBox", "SerialLogListBox", "SerialTimeText" })
            {
                var element = Element<FrameworkElement>(window, name);
                Rect bounds = element.TransformToAncestor(root).TransformBounds(new Rect(element.RenderSize));
                Assert.IsTrue(bounds.Width > 0 && bounds.Height > 0, name);
                Assert.IsTrue(bounds.Left >= 0 && bounds.Top >= 0
                    && bounds.Right <= root.ActualWidth + 1 && bounds.Bottom <= root.ActualHeight + 1, name);
            }
        }
        finally
        {
            window.Close();
        }
    }
}
