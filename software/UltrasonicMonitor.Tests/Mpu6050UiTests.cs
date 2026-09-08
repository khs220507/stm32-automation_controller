using System.Reflection;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace UltrasonicMonitor.Tests;

// 실제 COM 포트를 열지 않고 수신 처리부터 WPF 표시까지 확인한다.
[TestClass]
[DoNotParallelize] // WPF XAML/Dispatcher 시험은 병렬 STA 생성 없이 순서대로 실행한다.
public sealed class Mpu6050UiTests
{
    [STATestMethod]
    public void BoardResponseTimeoutClearsIdentityAndIgnoresLateReply()
    {
        var window = new MainWindow();
        SynchronizationContext? previous = SynchronizationContext.Current;
        try
        {
            SynchronizationContext.SetSynchronizationContext(
                new DispatcherSynchronizationContext(window.Dispatcher));
            BeginDiagnostic(window);
            Call(window, "HandleReceivedLine", "OK,CHECK_MPU6050,114");
            BeginDiagnostic(window);
            var task = (Task)Call(window, "WaitForResponseTimeoutAsync",
                "CHECK_MPU6050", CancellationToken.None)!;
            var frame = new DispatcherFrame();
            _ = task.ContinueWith(_ => window.Dispatcher.BeginInvoke(
                new Action(() => frame.Continue = false)), TaskScheduler.Default);
            Dispatcher.PushFrame(frame);
            task.GetAwaiter().GetResult();
            StringAssert.Contains(Text(window, "MpuStatusText"), "보드 응답 없음");
            StringAssert.Contains(Text(window, "MpuIdentityText"), "식별값: —");
            Call(window, "HandleReceivedLine", "OK,CHECK_MPU6050,114");
            StringAssert.Contains(Text(window, "MpuStatusText"), "보드 응답 없음");
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previous);
            window.Close();
        }
    }

    [STATestMethod]
    public void RendersDisconnectedWindowAtMinimumSize()
    {
        var window = new MainWindow();
        try
        {
            var content = (FrameworkElement)window.Content;
            var size = new Size(980, 700);
            content.Measure(size);
            content.Arrange(new Rect(size));
            content.UpdateLayout();
            Assert.IsGreaterThan(0, ((Button)window.FindName("MpuStartButton")).ActualWidth);
            var bitmap = new RenderTargetBitmap(980, 700, 96, 96, PixelFormats.Pbgra32);
            var background = new DrawingVisual();
            using (DrawingContext drawing = background.RenderOpen())
                drawing.DrawRectangle(window.Background, null, new Rect(size));
            bitmap.Render(background);
            bitmap.Render(content);
            var encoder = new PngBitmapEncoder();
            encoder.Frames.Add(BitmapFrame.Create(bitmap));
            using var file = File.Create(Path.Combine(AppContext.BaseDirectory, "통신-대시보드-미연결.png"));
            encoder.Save(file);
        }
        finally { window.Close(); }
    }

    private static object? Call(MainWindow window, string name, params object?[] args) =>
        typeof(MainWindow).GetMethod(name, BindingFlags.Instance | BindingFlags.NonPublic)!
            .Invoke(window, args);

    private static void BeginDiagnostic(MainWindow window) =>
        typeof(MainWindow).GetField("_pendingCommand", BindingFlags.Instance | BindingFlags.NonPublic)!
            .SetValue(window, "CHECK_MPU6050");

    private static string Text(MainWindow window, string name) =>
        ((TextBlock)window.FindName(name)).Text;

    [STATestMethod]
    public void ShowsIdentityThenClearsItOnSensorFailure()
    {
        var window = new MainWindow();
        try
        {
            BeginDiagnostic(window);
            Call(window, "HandleReceivedLine", "OK,CHECK_MPU6050,114");
            Assert.AreEqual("MPU6050 확인됨", Text(window, "MpuStatusText"));
            StringAssert.Contains(Text(window, "MpuIdentityText"), "식별값: 0x72");

            BeginDiagnostic(window);
            Call(window, "HandleReceivedLine", "ERR,CHECK_MPU6050,NACK");
            StringAssert.Contains(Text(window, "MpuStatusText"), "ACK 응답 없음");
            StringAssert.Contains(Text(window, "MpuIdentityText"), "식별값: —");
        }
        finally { window.Close(); }
    }

    [STATestMethod]
    public void ShowsUnexpectedIdentityWithoutClaimingSuccess()
    {
        var window = new MainWindow();
        try
        {
            BeginDiagnostic(window);
            Call(window, "HandleReceivedLine", "OK,CHECK_MPU6050,112");
            StringAssert.Contains(Text(window, "MpuStatusText"), "식별값 불일치");
            StringAssert.Contains(Text(window, "MpuIdentityText"), "0x70");
        }
        finally { window.Close(); }
    }

    [STATestMethod]
    public void DisconnectAndBoardRestartInvalidatePreviousIdentity()
    {
        var window = new MainWindow();
        try
        {
            BeginDiagnostic(window);
            Call(window, "HandleReceivedLine", "OK,CHECK_MPU6050,114");
            Call(window, "Disconnect");
            Assert.AreEqual("연결 안 됨", Text(window, "MpuStatusText"));
            StringAssert.Contains(Text(window, "MpuIdentityText"), "식별값: —");

            BeginDiagnostic(window);
            Call(window, "HandleReceivedLine", "OK,CHECK_MPU6050,114");
            Call(window, "HandleReceivedLine", "READY");
            StringAssert.Contains(Text(window, "MpuStatusText"), "보드 재시작");
            StringAssert.Contains(Text(window, "MpuIdentityText"), "식별값: —");

            BeginDiagnostic(window);
            Call(window, "HandleReceivedLine", "OK,CHECK_MPU6050,114");
            Call(window, "HandleReceivedLine", "FAULT");
            StringAssert.Contains(Text(window, "MpuStatusText"), "보드 고장 상태");
            StringAssert.Contains(Text(window, "MpuIdentityText"), "식별값: —");
        }
        finally { window.Close(); }
    }

    [STATestMethod]
    public void UnsolicitedResponseDoesNotReplaceDisplay()
    {
        var window = new MainWindow();
        try
        {
            Call(window, "HandleReceivedLine", "OK,CHECK_MPU6050,114");
            Assert.AreEqual("확인 전", Text(window, "MpuStatusText"));
            StringAssert.Contains(Text(window, "MpuIdentityText"), "식별값: —");
        }
        finally { window.Close(); }
    }
}
