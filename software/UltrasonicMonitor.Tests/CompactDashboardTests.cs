using System.Globalization;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace UltrasonicMonitor.Tests;

[TestClass]
[DoNotParallelize]
public sealed class CompactDashboardTests
{
    private static void Call(MainWindow w, string name, params object?[] args) =>
        typeof(MainWindow).GetMethod(name, BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(w, args);

    [STATestMethod]
    [DataRow(980.0, 700.0)]
    [DataRow(1220.0, 780.0)]
    public void KeepsAllPanelsInsideWindowWithoutPageScroll(double width, double height)
    {
        var w = new MainWindow();
        try
        {
            var root = (FrameworkElement)w.Content;
            Assert.IsInstanceOfType<Grid>(root);
            Call(w, "SetConnectionState", true, "COM3");
            Call(w, "SetMpuDisplay", "보드 재시작 · 다시 확인하세요", null, false);
            Call(w, "ShowCommandFailure", "CHECK_W5500", "이전 SPI 전송 상태가 남아 있음 · 보드 재시작 필요");
            var size = new Size(width, height);
            root.Measure(size); root.Arrange(new Rect(size)); root.UpdateLayout();
            foreach (string name in new[] { "SerialPortComboBox", "SerialCheckButton", "SerialStatusText", "HostTextBox", "TcpPortTextBox", "ConnectButton", "UartCheckButton",
                "MpuStartButton", "MpuStopButton", "StartButton", "StopButton", "W5500CheckButton",
                "MpuWakeButton", "AccelStartButton", "AccelStopButton", "AccelZText", "AccelModeText",
                "UartLogListBox", "MpuLogListBox", "UltrasonicLogListBox", "W5500LogListBox", "LogListBox" })
            {
                var element = (FrameworkElement)w.FindName(name);
                var bounds = element.TransformToAncestor(root).TransformBounds(new Rect(element.RenderSize));
                Assert.IsGreaterThan(0.0, bounds.Width, name);
                Assert.IsGreaterThan(0.0, bounds.Height, name);
                Assert.IsTrue(bounds.Left >= -1 && bounds.Top >= -1 &&
                    bounds.Right <= root.ActualWidth + 1 && bounds.Bottom <= root.ActualHeight + 1, name);
            }
            foreach (string name in new[] { "MpuStatusText", "W5500StatusText" })
            {
                var t = (TextBlock)w.FindName(name);
                var text = new FormattedText(t.Text, CultureInfo.CurrentUICulture, t.FlowDirection,
                    new Typeface(t.FontFamily, t.FontStyle, t.FontWeight, t.FontStretch),
                    t.FontSize, t.Foreground, VisualTreeHelper.GetDpi(t).PixelsPerDip)
                { MaxTextWidth = t.ActualWidth };
                Assert.IsLessThanOrEqualTo(t.ActualHeight + 1, text.Height, name);
            }
        }
        finally { w.Close(); }
    }
}
