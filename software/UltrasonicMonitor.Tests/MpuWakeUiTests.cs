using System.Reflection;
using System.Windows.Controls;
using System.Windows.Threading;
using UltrasonicMonitor.Protocol;

namespace UltrasonicMonitor.Tests;

[TestClass]
[DoNotParallelize]
public sealed class MpuWakeUiTests
{
    [STATestMethod]
    public void LogsScrollInternallyWithoutGrowingDashboard()
    {
        var w = new MainWindow();
        try
        {
            ((TabControl)w.FindName("WorkspaceTabs")).SelectedIndex = 1;
            var content = (System.Windows.FrameworkElement)w.Content;
            var size = new System.Windows.Size(980, 700);
            void Layout()
            {
                content.Measure(size);
                content.Arrange(new System.Windows.Rect(size));
                content.UpdateLayout();
            }
            Layout();
            var dashboard = content;
            double initialHeight = dashboard.ActualHeight;
            foreach (string command in new[] { "PING", "CHECK_MPU6050", "CHECK_HCSR04", "SYSTEM" })
                for (int i = 0; i < 150; i++) Call(w, "AppendLog", "RX", $"{i}: test response", command);
            Layout();
            Assert.AreEqual(initialHeight, dashboard.ActualHeight, 1.0);
            foreach (string name in new[] { "UartLogListBox", "MpuLogListBox", "UltrasonicLogListBox", "LogListBox" })
            {
                var list = (ListBox)w.FindName(name);
                Assert.IsTrue(list.ActualHeight > 0 && list.ActualHeight < 260);
                var scroll = FindScroll(list)!;
                Assert.IsNotNull(scroll);
                Assert.IsGreaterThan(scroll.ViewportHeight, scroll.ExtentHeight);
            }
        }
        finally { w.Close(); }
    }

    private static ScrollViewer? FindScroll(System.Windows.DependencyObject node)
    {
        if (node is ScrollViewer scroll) return scroll;
        for (int i = 0; i < System.Windows.Media.VisualTreeHelper.GetChildrenCount(node); i++)
        {
            var result = FindScroll(System.Windows.Media.VisualTreeHelper.GetChild(node, i));
            if (result is not null) return result;
        }
        return null;
    }

    private static object? Call(MainWindow w, string name, params object?[] args) =>
        typeof(MainWindow).GetMethod(name, BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(w, args);
    private static void Set(MainWindow w, string name, object value) =>
        typeof(MainWindow).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(w, value);
    private static string Text(MainWindow w, string name) => ((TextBlock)w.FindName(name)).Text;
    private static void Begin(MainWindow w)
    {
        Set(w, "_pendingCommand", "WAKE_MPU6050");
        Call(w, "BeginCommandDisplay", "WAKE_MPU6050");
    }

    [TestMethod]
    [DataRow("OK,WAKE_MPU6050,64,0", "OK")]
    [DataRow("OK,WAKE_MPU6050,105,41", "OK")]
    [DataRow("OK,WAKE_MPU6050,1,1", "OK")]
    [DataRow("OK,WAKE_MPU6050,64,64", "VERIFY_FAILED")]
    [DataRow("OK,WAKE_MPU6050,65,0", "VERIFY_FAILED")]
    [DataRow("OK,WAKE_MPU6050,192,128", "VERIFY_FAILED")]
    public void ChecksReadbackAndPreservedBits(string line, string status)
    {
        var message = AsciiProtocolParser.Parse(line);
        Assert.AreEqual(ProtocolMessageKind.Mpu6050Wake, message.Kind);
        Assert.AreEqual(status, message.SensorStatus);
    }

    [TestMethod]
    [DataRow("OK,WAKE_MPU6050,256,0")]
    [DataRow("OK,WAKE_MPU6050,64,-1")]
    [DataRow("OK,WAKE_MPU6050,64")]
    [DataRow("OK,WAKE_MPU6050,64,0,0")]
    public void RejectsMalformedResults(string line) =>
        Assert.AreEqual(ProtocolMessageKind.Unknown, AsciiProtocolParser.Parse(line).Kind);

    [STATestMethod]
    public void ShowsWakeResultSeparatelyAndSerializesRequests()
    {
        var w = new MainWindow();
        try
        {
            var button = (Button)w.FindName("MpuWakeButton");
            Assert.IsFalse(button.IsEnabled);
            Call(w, "SetConnectionState", true, "TEST");
            Assert.IsTrue(button.IsEnabled);
            Set(w, "_pendingCommand", "CHECK_MPU6050");
            Call(w, "HandleReceivedLine", "OK,CHECK_MPU6050,114");
            Begin(w);
            Assert.IsFalse(button.IsEnabled);
            Set(w, "_mpuRepeating", true);
            Assert.IsNull(Call(w, "NextSensorCommand"));
            Call(w, "HandleReceivedLine", "OK,WAKE_MPU6050,64,0");
            Assert.AreEqual("SLEEP=0 확인됨", Text(w, "MpuWakeStatusText"));
            StringAssert.Contains(Text(w, "MpuPowerText"), "0x40 → 확인값: 0x00");
            StringAssert.Contains(Text(w, "MpuIdentityText"), "0x72");
            Assert.IsTrue(button.IsEnabled);
            Assert.AreEqual("CHECK_MPU6050", Call(w, "NextSensorCommand"));
            Begin(w);
            StringAssert.Contains(Text(w, "MpuPowerText"), "변경 전: —");
            Call(w, "HandleReceivedLine", "ERR,WAKE_MPU6050,VERIFY_FAILED");
            StringAssert.Contains(Text(w, "MpuWakeStatusText"), "설정 확인 실패");
            var log = (ListBox)w.FindName("MpuLogListBox");
            Assert.IsTrue(log.Items.Cast<string>().Any(x => x.Contains("WAKE_MPU6050")));
        }
        finally { w.Close(); }
    }

    [STATestMethod]
    [DataRow("READY")]
    [DataRow("FAULT")]
    [DataRow("disconnect")]
    public void InvalidatesResultsOnResetOrDisconnect(string action)
    {
        var w = new MainWindow();
        try
        {
            Begin(w);
            Call(w, "HandleReceivedLine", "OK,WAKE_MPU6050,64,0");
            if (action == "disconnect") Call(w, "Disconnect");
            else Call(w, "HandleReceivedLine", action);
            StringAssert.Contains(Text(w, "MpuPowerText"), "변경 전: —");
            Assert.AreNotEqual("SLEEP=0 확인됨", Text(w, "MpuWakeStatusText"));
        }
        finally { w.Close(); }
    }

    [STATestMethod]
    public void TimeoutClearsValuesAndIgnoresLateReply()
    {
        var w = new MainWindow();
        var previous = SynchronizationContext.Current;
        try
        {
            SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext(w.Dispatcher));
            Begin(w);
            Call(w, "HandleReceivedLine", "OK,WAKE_MPU6050,64,0");
            Begin(w);
            var task = (Task)Call(w, "WaitForResponseTimeoutAsync", "WAKE_MPU6050", CancellationToken.None)!;
            var frame = new DispatcherFrame();
            _ = task.ContinueWith(_ => w.Dispatcher.BeginInvoke(new Action(() => frame.Continue = false)), TaskScheduler.Default);
            Dispatcher.PushFrame(frame);
            task.GetAwaiter().GetResult();
            Call(w, "HandleReceivedLine", "OK,WAKE_MPU6050,64,0");
            StringAssert.Contains(Text(w, "MpuWakeStatusText"), "보드 응답 없음");
            StringAssert.Contains(Text(w, "MpuPowerText"), "변경 전: —");
        }
        finally { SynchronizationContext.SetSynchronizationContext(previous); w.Close(); }
    }
}
