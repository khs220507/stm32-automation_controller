using System.IO;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using UltrasonicMonitor.Protocol;

namespace UltrasonicMonitor.Tests;

[TestClass]
[DoNotParallelize]
public sealed class W5500UiTests
{
    private static object? Call(MainWindow w, string name, params object?[] args) =>
        typeof(MainWindow).GetMethod(name, BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(w, args);
    private static void Set(MainWindow w, string name, object value) =>
        typeof(MainWindow).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(w, value);
    private static string Text(MainWindow w, string name) => ((TextBlock)w.FindName(name)).Text;
    private static void Begin(MainWindow w)
    {
        Set(w, "_pendingCommand", "CHECK_W5500");
        Call(w, "BeginCommandDisplay", "CHECK_W5500");
    }

    [TestMethod]
    [DataRow("4", "OK")]
    [DataRow("0", "VERSION_MISMATCH")]
    [DataRow("255", "VERSION_MISMATCH")]
    public void DistinguishesTransferFromVersionMatch(string value, string status)
    {
        var message = AsciiProtocolParser.Parse($"OK,CHECK_W5500,{value}");
        Assert.AreEqual(ProtocolMessageKind.W5500, message.Kind);
        Assert.AreEqual(status, message.SensorStatus);
        Assert.AreEqual(byte.Parse(value), message.Identity);
    }

    [TestMethod]
    [DataRow("OK,CHECK_W5500,256")]
    [DataRow("OK,CHECK_W5500,-1")]
    [DataRow("OK,CHECK_W5500,0x04")]
    [DataRow("OK,CHECK_W5500, 4")]
    [DataRow("OK,CHECK_W5500")]
    [DataRow("OK,CHECK_W5500,4,0")]
    public void RejectsMalformedVersion(string line) =>
        Assert.AreEqual(ProtocolMessageKind.Unknown, AsciiProtocolParser.Parse(line).Kind);

    [STATestMethod]
    public void SerializesRequestsAndShowsRawMismatchInOwnLog()
    {
        var w = new MainWindow();
        try
        {
            var button = (Button)w.FindName("W5500CheckButton");
            Assert.IsFalse(button.IsEnabled);
            Call(w, "SetConnectionState", true, "TEST");
            Assert.IsTrue(button.IsEnabled);
            Begin(w);
            Assert.IsFalse(button.IsEnabled);
            Set(w, "_ultrasonicRepeating", true);
            Assert.IsNull(Call(w, "NextSensorCommand"));
            Call(w, "HandleReceivedLine", "OK,CHECK_MPU6050,114");
            Assert.AreEqual("확인 중…", Text(w, "W5500StatusText"));
            Call(w, "HandleReceivedLine", "OK,CHECK_W5500,4");
            Assert.AreEqual("W5500 버전 확인됨", Text(w, "W5500StatusText"));
            Assert.IsTrue(button.IsEnabled);
            Assert.AreEqual("CHECK_HCSR04", Call(w, "NextSensorCommand"));
            Begin(w);
            StringAssert.Contains(Text(w, "W5500VersionText"), "버전 값: —");
            Call(w, "HandleReceivedLine", "OK,CHECK_W5500,255");
            StringAssert.Contains(Text(w, "W5500StatusText"), "버전 불일치");
            StringAssert.Contains(Text(w, "W5500VersionText"), "0xFF");
            var log = (ListBox)w.FindName("W5500LogListBox");
            Assert.IsTrue(log.Items.Cast<string>().Any(x => x.Contains("OK,CHECK_W5500,255")));
            Assert.IsFalse(((ListBox)w.FindName("UartLogListBox")).Items.Cast<string>().Any(x => x.Contains("CHECK_W5500")));
            Call(w, "W5500CheckButton_Click", button, new RoutedEventArgs());
            StringAssert.Contains(Text(w, "W5500StatusText"), "TCP 연결");
        }
        finally { w.Close(); }
    }

    [STATestMethod]
    [DataRow("TIMEOUT", "시간 초과")]
    [DataRow("NOT_READY", "재시작")]
    [DataRow("HARDWARE_ERROR", "SPI 통신 오류")]
    [DataRow("DIRTY_STATE", "재시작")]
    [DataRow("INVALID_STATE", "대기 상태")]
    public void DisplaysBoardErrorsAndClearsPreviousVersion(string error, string expected)
    {
        var w = new MainWindow();
        try
        {
            Begin(w); Call(w, "HandleReceivedLine", "OK,CHECK_W5500,4");
            Begin(w); Call(w, "HandleReceivedLine", $"ERR,CHECK_W5500,{error}");
            StringAssert.Contains(Text(w, "W5500StatusText"), expected);
            StringAssert.Contains(Text(w, "W5500VersionText"), "버전 값: —");
        }
        finally { w.Close(); }
    }

    [STATestMethod]
    [DataRow("READY")]
    [DataRow("FAULT")]
    [DataRow("disconnect")]
    public void ClearsResultsAndPendingRequestOnResetOrDisconnect(string action)
    {
        var w = new MainWindow();
        try
        {
            Begin(w); Call(w, "HandleReceivedLine", "OK,CHECK_W5500,4");
            Begin(w);
            if (action == "disconnect") Call(w, "Disconnect");
            else Call(w, "HandleReceivedLine", action);
            Call(w, "HandleReceivedLine", "OK,CHECK_W5500,4");
            StringAssert.Contains(Text(w, "W5500VersionText"), "버전 값: —");
            Assert.AreEqual("확인 이력 없음", Text(w, "W5500LastCheckText"));
        }
        finally { w.Close(); }
    }

    [STATestMethod]
    public void PcTimeoutIgnoresLateReplyAndAllowsAnotherRequest()
    {
        var w = new MainWindow();
        var previous = SynchronizationContext.Current;
        try
        {
            SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext(w.Dispatcher));
            Call(w, "SetConnectionState", true, "TEST");
            Begin(w);
            var task = (Task)Call(w, "WaitForResponseTimeoutAsync", "CHECK_W5500", CancellationToken.None)!;
            var frame = new DispatcherFrame();
            _ = task.ContinueWith(_ => w.Dispatcher.BeginInvoke(new Action(() => frame.Continue = false)), TaskScheduler.Default);
            Dispatcher.PushFrame(frame);
            task.GetAwaiter().GetResult();
            Call(w, "HandleReceivedLine", "OK,CHECK_W5500,4");
            StringAssert.Contains(Text(w, "W5500StatusText"), "보드 응답 없음 (500 ms)");
            StringAssert.Contains(Text(w, "W5500VersionText"), "버전 값: —");
            Assert.IsTrue(((Button)w.FindName("W5500CheckButton")).IsEnabled);
            Begin(w); Call(w, "HandleReceivedLine", "OK,CHECK_W5500,4");
            Assert.AreEqual("W5500 버전 확인됨", Text(w, "W5500StatusText"));
        }
        finally { SynchronizationContext.SetSynchronizationContext(previous); w.Close(); }
    }

    [STATestMethod]
    public void RendersAtMinimumWidthAndBoundsLogGrowth()
    {
        var w = new MainWindow();
        try
        {
            Call(w, "SetConnectionState", true, "모의 응답 · 실측 아님");
            Begin(w); Call(w, "HandleReceivedLine", "OK,CHECK_W5500,4");
            ((System.Windows.Controls.TabControl)w.FindName("WorkspaceTabs")).SelectedIndex = 1;
            var content = (FrameworkElement)w.Content;
            var size = new Size(980, 700);
            void Layout() { content.Measure(size); content.Arrange(new Rect(size)); content.UpdateLayout(); }
            Layout();
            var dashboard = content;
            double height = dashboard.ActualHeight;
            for (int i = 0; i < 510; ++i) Call(w, "AppendLog", "RX", $"모의 응답 {i}", "CHECK_W5500");
            Layout();
            Assert.AreEqual(height, dashboard.ActualHeight, 1.0);
            Assert.AreEqual(500, ((ListBox)w.FindName("W5500LogListBox")).Items.Count);
            foreach (string name in new[] { "W5500CheckButton", "W5500VersionText", "W5500LogListBox" })
            {
                var element = (FrameworkElement)w.FindName(name);
                Assert.IsGreaterThan(0.0, element.ActualWidth);
                Assert.IsLessThanOrEqualTo(size.Width,
                    element.TransformToAncestor(content).Transform(new Point(element.ActualWidth, 0)).X);
            }
            var bitmap = new RenderTargetBitmap(980, 700, 96, 96, PixelFormats.Pbgra32);
            var background = new DrawingVisual();
            using (var drawing = background.RenderOpen()) drawing.DrawRectangle(new SolidColorBrush(Color.FromRgb(243, 243, 243)), null, new Rect(size));
            bitmap.Render(background); bitmap.Render(content);
            var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap));
            using var file = File.Create(Path.Combine(AppContext.BaseDirectory, "W5500-모의응답-화면.png"));
            encoder.Save(file);
        }
        finally { w.Close(); }
    }
}
