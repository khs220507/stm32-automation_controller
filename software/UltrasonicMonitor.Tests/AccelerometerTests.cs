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
public sealed class AccelerometerTests
{
    private static object? Call(MainWindow w, string name, params object?[] args) =>
        typeof(MainWindow).GetMethod(name, BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(w, args);
    private static void Set(MainWindow w, string name, object? value) =>
        typeof(MainWindow).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(w, value);
    private static T Get<T>(MainWindow w, string name) =>
        (T)typeof(MainWindow).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(w)!;
    private static string Text(MainWindow w, string name) => ((TextBlock)w.FindName(name)).Text;
    private static void Reply(MainWindow w, string command, string line)
    {
        Set(w, "_pendingCommand", command);
        Call(w, "HandleReceivedLine", line);
    }

    [TestMethod]
    public void ParsesSignedLimitsAndConfiguration()
    {
        var m = AsciiProtocolParser.Parse("OK,READ_ACCEL,-32768,32767,-1");
        Assert.AreEqual(ProtocolMessageKind.Accelerometer, m.Kind);
        Assert.AreEqual((short)-32768, m.AccelX);
        Assert.AreEqual((short)32767, m.AccelY);
        Assert.AreEqual((short)-1, m.AccelZ);
        Assert.AreEqual(ProtocolMessageKind.AccelConfigured, AsciiProtocolParser.Parse("OK,CONFIG_ACCEL,2G").Kind);
    }

    [TestMethod]
    [DataRow("OK,READ_ACCEL,32768,0,0")]
    [DataRow("OK,READ_ACCEL,0,-32769,0")]
    [DataRow("OK,READ_ACCEL,0,0,1.2")]
    [DataRow("OK,READ_ACCEL,0,0")]
    [DataRow("OK,READ_ACCEL,0,0,0,0")]
    [DataRow("OK,READ_ACCEL, 1,0,0")]
    [DataRow("OK,CONFIG_ACCEL,4G")]
    public void RejectsInvalidSamples(string line) =>
        Assert.AreEqual(ProtocolMessageKind.Unknown, AsciiProtocolParser.Parse(line).Kind);

    [STATestMethod]
    public void ConfiguresBeforePollingAndAlternatesWithUltrasonic()
    {
        var w = new MainWindow();
        try
        {
            Call(w, "SetConnectionState", true, "TEST");
            Set(w, "_accelRepeating", true);
            Set(w, "_accelNeedsConfiguration", true);
            Assert.AreEqual("CONFIG_ACCEL", Call(w, "NextSensorCommand"));
            Set(w, "_pendingCommand", "CONFIG_ACCEL");
            Assert.IsNull(Call(w, "NextSensorCommand"));
            Call(w, "HandleReceivedLine", "OK,CONFIG_ACCEL,2G");
            Assert.AreEqual("READ_ACCEL", Call(w, "NextSensorCommand"));
            Set(w, "_ultrasonicRepeating", true);
            Assert.AreEqual("CHECK_HCSR04", Call(w, "NextSensorCommand"));
            Assert.AreEqual("READ_ACCEL", Call(w, "NextSensorCommand"));
            Set(w, "_pendingCommand", "READ_ACCEL");
            Call(w, "StopAccel");
            StringAssert.Contains(Text(w, "AccelModeText"), "응답 대기");
            Call(w, "HandleReceivedLine", "OK,READ_ACCEL,16384,-16384,0");
            Assert.IsFalse(Get<bool>(w, "_accelRepeating"));
            Assert.AreEqual("1.000 g", Text(w, "AccelXText"));
            Assert.AreEqual("-1.000 g", Text(w, "AccelYText"));
            Assert.AreEqual("0.000 g", Text(w, "AccelZText"));
            StringAssert.Contains(Text(w, "AccelModeText"), "정지");
            Assert.AreEqual("CHECK_HCSR04", Call(w, "NextSensorCommand"));
        }
        finally { w.Close(); }
    }

    [STATestMethod]
    public void StopWhileConfiguringDoesNotRestartPolling()
    {
        var w = new MainWindow();
        try
        {
            Set(w, "_connected", true); Set(w, "_accelRepeating", true);
            Set(w, "_pendingCommand", "CONFIG_ACCEL");
            Call(w, "StopAccel");
            Call(w, "HandleReceivedLine", "OK,CONFIG_ACCEL,2G");
            Assert.IsNull(Call(w, "NextSensorCommand"));
            Assert.IsFalse(Get<bool>(w, "_accelRepeating"));
        }
        finally { w.Close(); }
    }

    [STATestMethod]
    [DataRow("ERR,READ_ACCEL,NACK")]
    [DataRow("ERR,READ_ACCEL,CONFIG_CHANGED")]
    [DataRow("ERR,READ_ACCEL,DATA_TIMEOUT")]
    [DataRow("READY")]
    [DataRow("FAULT")]
    [DataRow("disconnect")]
    public void FailureOrDisconnectClearsOldValuesAndStopsPolling(string action)
    {
        var w = new MainWindow();
        try
        {
            Set(w, "_connected", true); Set(w, "_accelRepeating", true);
            Reply(w, "READ_ACCEL", "OK,READ_ACCEL,16384,0,0");
            if (action == "disconnect") Call(w, "Disconnect");
            else Reply(w, "READ_ACCEL", action);
            Assert.AreEqual("— g", Text(w, "AccelXText"));
            Assert.IsFalse(Get<bool>(w, "_accelRepeating"));
            Assert.IsNull(Call(w, "NextSensorCommand"));
            Call(w, "HandleReceivedLine", "OK,READ_ACCEL,16384,0,0");
            Assert.AreEqual("— g", Text(w, "AccelXText"));
        }
        finally { w.Close(); }
    }

    [STATestMethod]
    public void ResponseTimeoutStopsAndDiscardsLateSample()
    {
        var w = new MainWindow();
        var previous = SynchronizationContext.Current;
        try
        {
            SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext(w.Dispatcher));
            Set(w, "_accelRepeating", true);
            Reply(w, "READ_ACCEL", "OK,READ_ACCEL,16384,0,0");
            Set(w, "_pendingCommand", "READ_ACCEL");
            var task = (Task)Call(w, "WaitForResponseTimeoutAsync", "READ_ACCEL", CancellationToken.None)!;
            var frame = new DispatcherFrame();
            _ = task.ContinueWith(_ => w.Dispatcher.BeginInvoke(new Action(() => frame.Continue = false)), TaskScheduler.Default);
            Dispatcher.PushFrame(frame);
            task.GetAwaiter().GetResult();
            Call(w, "HandleReceivedLine", "OK,READ_ACCEL,16384,0,0");
            Assert.AreEqual("— g", Text(w, "AccelXText"));
            Assert.IsFalse(Get<bool>(w, "_accelRepeating"));
            StringAssert.Contains(Text(w, "AccelStatusText"), "보드 응답 없음");
        }
        finally { SynchronizationContext.SetSynchronizationContext(previous); w.Close(); }
    }

    [STATestMethod]
    public void RendersSimulatedValuesAndBoundsCardAtMinimumWidth()
    {
        var w = new MainWindow();
        try
        {
            Call(w, "SetConnectionState", true, "모의 응답 · 실측 아님");
            Reply(w, "READ_ACCEL", "OK,READ_ACCEL,0,-8192,14189");
            ((System.Windows.Controls.TabControl)w.FindName("WorkspaceTabs")).SelectedIndex = 1;
            var content = (FrameworkElement)w.Content;
            var size = new Size(980, 700);
            content.Measure(size); content.Arrange(new Rect(size)); content.UpdateLayout();
            var x = (FrameworkElement)w.FindName("AccelXText");
            var z = (FrameworkElement)w.FindName("AccelZText");
            Assert.IsTrue(x.ActualWidth > 0 && z.ActualWidth > 0);
            Assert.IsLessThanOrEqualTo(size.Width, z.TransformToAncestor(content).Transform(new Point(z.ActualWidth, 0)).X);
            var bitmap = new RenderTargetBitmap(980, 700, 96, 96, PixelFormats.Pbgra32);
            var background = new DrawingVisual();
            using (var drawing = background.RenderOpen()) drawing.DrawRectangle(new SolidColorBrush(Color.FromRgb(243, 243, 243)), null, new Rect(size));
            bitmap.Render(background); bitmap.Render(content);
            var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap));
            using var file = File.Create(Path.Combine(AppContext.BaseDirectory, "가속도-모의응답-화면.png"));
            encoder.Save(file);
        }
        finally { w.Close(); }
    }
}
