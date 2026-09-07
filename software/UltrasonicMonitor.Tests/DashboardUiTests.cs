using System.Reflection;
using System.Windows.Controls;
using System.Windows.Threading;

namespace UltrasonicMonitor.Tests;

[TestClass]
[DoNotParallelize]
public sealed class DashboardUiTests
{
    [STATestMethod]
    public void LogsKeepSensorResponsesAndErrorsSeparate()
    {
        var window = new MainWindow();
        try
        {
            var common = (ListBox)window.FindName("LogListBox");
            var uart = (ListBox)window.FindName("UartLogListBox");
            var mpu = (ListBox)window.FindName("MpuLogListBox");
            var ultrasonic = (ListBox)window.FindName("UltrasonicLogListBox");
            int initialCommon = common.Items.Count;
            Reply(window, "PING", "OK,PING,PONG");
            Reply(window, "CHECK_HCSR04", "OK,CHECK_HCSR04,OK,25,1450");
            Reply(window, "CHECK_MPU6050", "ERR,CHECK_MPU6050,NACK");
            Assert.AreEqual(1, uart.Items.Count);
            Assert.AreEqual(1, ultrasonic.Items.Count);
            Assert.AreEqual(2, mpu.Items.Count);
            Assert.AreEqual(initialCommon, common.Items.Count);
            Call(window, "AppendLog", "TIMEOUT", "응답 없음", "CHECK_MPU6050");
            Assert.AreEqual(3, mpu.Items.Count);
            Call(window, "HandleReceivedLine", "READY");
            Call(window, "HandleReceivedLine", "invalid line");
            Assert.AreEqual(initialCommon + 3, common.Items.Count);
            for (int i = 0; i < 510; i++)
                Call(window, "AppendLog", "TX", "CHECK_HCSR04", "CHECK_HCSR04");
            Assert.AreEqual(500, ultrasonic.Items.Count);
            Assert.AreEqual(3, mpu.Items.Count);
            Assert.AreEqual(1, uart.Items.Count);
        }
        finally { window.Close(); }
    }

    private static object? Call(MainWindow window, string name, params object?[] args) =>
        typeof(MainWindow).GetMethod(name, BindingFlags.Instance | BindingFlags.NonPublic)!
            .Invoke(window, args);
    private static void Set(MainWindow window, string name, object? value) =>
        typeof(MainWindow).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(window, value);
    private static T Get<T>(MainWindow window, string name) =>
        (T)typeof(MainWindow).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(window)!;
    private static string Text(MainWindow window, string name) => ((TextBlock)window.FindName(name)).Text;
    private static void Reply(MainWindow window, string command, string line)
    {
        Set(window, "_pendingCommand", command);
        Call(window, "HandleReceivedLine", line);
    }

    [STATestMethod]
    public void EachModuleKeepsItsOwnResultAndTimestamp()
    {
        var window = new MainWindow();
        try
        {
            Reply(window, "PING", "OK,PING,PONG");
            string uartTime = Text(window, "UartLastCheckText");
            Reply(window, "CHECK_HCSR04", "OK,CHECK_HCSR04,OK,25,1450");
            string sensorTime = Text(window, "SensorLastCheckText");
            Reply(window, "CHECK_MPU6050", "ERR,CHECK_MPU6050,BUS_BUSY");
            StringAssert.Contains(Text(window, "UartStatusText"), "확인됨");
            Assert.AreEqual(uartTime, Text(window, "UartLastCheckText"));
            Assert.AreEqual("25", Text(window, "DistanceText"));
            Assert.AreEqual(sensorTime, Text(window, "SensorLastCheckText"));
            StringAssert.Contains(Text(window, "MpuStatusText"), "버스 사용 중");

            Reply(window, "CHECK_MPU6050", "OK,CHECK_MPU6050,104");
            string mpuTime = Text(window, "MpuLastCheckText");
            Reply(window, "CHECK_HCSR04", "OK,CHECK_HCSR04,TIMEOUT,0,0");
            Assert.AreEqual("—", Text(window, "DistanceText"));
            StringAssert.Contains(Text(window, "SensorStatusText"), "시간 초과");
            Assert.AreEqual("MPU6050 확인됨", Text(window, "MpuStatusText"));
            Assert.AreEqual(mpuTime, Text(window, "MpuLastCheckText"));
        }
        finally { window.Close(); }
    }

    [STATestMethod]
    public void PendingRequestDisablesOtherTestsButAllowsRepeatStop()
    {
        var window = new MainWindow();
        try
        {
            Set(window, "_connected", true);
            Set(window, "_ultrasonicRepeating", true);
            Set(window, "_pendingCommand", "CHECK_HCSR04");
            Call(window, "UpdateTestButtons");
            Assert.IsFalse(((Button)window.FindName("UartCheckButton")).IsEnabled);
            Assert.IsTrue(((Button)window.FindName("MpuStartButton")).IsEnabled);
            Assert.IsTrue(((Button)window.FindName("StopButton")).IsEnabled);

            Call(window, "StopUltrasonic");
            Assert.IsFalse(Get<bool>(window, "_ultrasonicRepeating"));
            Assert.AreEqual("CHECK_HCSR04", Get<string>(window, "_pendingCommand"));
            StringAssert.Contains(Text(window, "MeasurementModeText"), "응답 대기");
            Call(window, "HandleReceivedLine", "OK,CHECK_HCSR04,OK,25,1450");
            Assert.AreEqual("정지", Text(window, "MeasurementModeText"));
            Assert.IsTrue(((Button)window.FindName("UartCheckButton")).IsEnabled);
            Assert.IsTrue(((Button)window.FindName("MpuStartButton")).IsEnabled);
        }
        finally { window.Close(); }
    }

    [STATestMethod]
    public void SelectingAnotherTestKeepsUltrasonicScheduler()
    {
        var window = new MainWindow();
        try
        {
            Set(window, "_ultrasonicRepeating", true);
            var timer = Get<DispatcherTimer>(window, "_sensorPollTimer");
            timer.Start();
            // MPU 송신 실패도 초음파 반복 실행을 중단하지 않는다.
            Call(window, "SendCommand", "CHECK_MPU6050");
            Assert.IsTrue(timer.IsEnabled);
            Assert.IsTrue(Get<bool>(window, "_ultrasonicRepeating"));
            Assert.AreEqual("확인 전", Text(window, "UartStatusText"));
        }
        finally { window.Close(); }
    }

    [STATestMethod]
    public void BothSensorsAlternateAndStopIndependently()
    {
        var window = new MainWindow();
        try
        {
            Set(window, "_connected", true);
            Set(window, "_ultrasonicRepeating", true);
            Set(window, "_mpuRepeating", true);
            var timer = Get<DispatcherTimer>(window, "_sensorPollTimer");
            timer.Start();
            Assert.AreEqual("CHECK_HCSR04", Call(window, "NextSensorCommand"));
            Set(window, "_pendingCommand", "CHECK_HCSR04");
            Assert.IsNull(Call(window, "NextSensorCommand"));
            Call(window, "HandleReceivedLine", "OK,CHECK_HCSR04,OK,25,1450");
            Assert.AreEqual("CHECK_MPU6050", Call(window, "NextSensorCommand"));
            Reply(window, "CHECK_MPU6050", "ERR,CHECK_MPU6050,NACK");
            Assert.IsTrue(Get<bool>(window, "_ultrasonicRepeating"));
            Assert.IsTrue(Get<bool>(window, "_mpuRepeating"));
            Assert.AreEqual("CHECK_HCSR04", Call(window, "NextSensorCommand"));
            Call(window, "StopUltrasonic");
            Assert.IsTrue(timer.IsEnabled);
            Assert.AreEqual("CHECK_MPU6050", Call(window, "NextSensorCommand"));
            Call(window, "StopMpu");
            Assert.IsFalse(timer.IsEnabled);
            Assert.IsNull(Call(window, "NextSensorCommand"));

            Set(window, "_ultrasonicRepeating", true);
            Set(window, "_mpuRepeating", true);
            Call(window, "ShowCommandFailure", "CHECK_MPU6050", "보드 응답 없음");
            Assert.IsTrue(Get<bool>(window, "_ultrasonicRepeating"));
            Assert.IsFalse(Get<bool>(window, "_mpuRepeating"));
            Assert.AreEqual("CHECK_HCSR04", Call(window, "NextSensorCommand"));
            Set(window, "_mpuRepeating", true);
            Call(window, "HandleReceivedLine", "READY");
            Assert.IsFalse(Get<bool>(window, "_ultrasonicRepeating"));
            Assert.IsFalse(Get<bool>(window, "_mpuRepeating"));
            Assert.IsFalse(timer.IsEnabled);
        }
        finally { window.Close(); }
    }

    [DataRow("PING", "UartStatusText")]
    [DataRow("CHECK_HCSR04", "SensorStatusText")]
    [STATestMethod]
    public void NoBoardResponseIsReportedOnlyForRequestedTest(string command, string field)
    {
        var window = new MainWindow();
        SynchronizationContext? previous = SynchronizationContext.Current;
        try
        {
            Reply(window, "CHECK_MPU6050", "OK,CHECK_MPU6050,104");
            Set(window, "_pendingCommand", command);
            Set(window, "_ultrasonicRepeating", command == "CHECK_HCSR04");
            SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext(window.Dispatcher));
            var task = (Task)Call(window, "WaitForResponseTimeoutAsync", command, CancellationToken.None)!;
            var frame = new DispatcherFrame();
            _ = task.ContinueWith(_ => window.Dispatcher.BeginInvoke(new Action(() => frame.Continue = false)), TaskScheduler.Default);
            Dispatcher.PushFrame(frame);
            task.GetAwaiter().GetResult();
            StringAssert.Contains(Text(window, field), "보드 응답 없음");
            Assert.AreEqual("MPU6050 확인됨", Text(window, "MpuStatusText"));
            Assert.IsFalse(Get<bool>(window, "_ultrasonicRepeating"));
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previous);
            window.Close();
        }
    }

    [STATestMethod]
    public void DisconnectClearsAllResultsAndStopsRepeat()
    {
        var window = new MainWindow();
        try
        {
            Reply(window, "PING", "OK,PING,PONG");
            Reply(window, "CHECK_HCSR04", "OK,CHECK_HCSR04,OK,25,1450");
            Reply(window, "CHECK_MPU6050", "OK,CHECK_MPU6050,104");
            Set(window, "_ultrasonicRepeating", true);
            Get<DispatcherTimer>(window, "_sensorPollTimer").Start();
            Call(window, "Disconnect");
            foreach (string field in new[] { "UartStatusText", "MpuStatusText", "SensorStatusText" })
                Assert.AreEqual("연결 안 됨", Text(window, field));
            Assert.AreEqual("—", Text(window, "DistanceText"));
            Assert.IsFalse(Get<DispatcherTimer>(window, "_sensorPollTimer").IsEnabled);
        }
        finally { window.Close(); }
    }

    [STATestMethod]
    public void LegacyStreamingMessageDoesNotCompleteIndividualTest()
    {
        var window = new MainWindow();
        try
        {
            Set(window, "_pendingCommand", "CHECK_HCSR04");
            Call(window, "HandleReceivedLine", "HCSR04 OK DIST_CM=25 PULSE_US=1450");
            Assert.AreEqual("CHECK_HCSR04", Get<string>(window, "_pendingCommand"));
            Assert.AreEqual("확인 전", Text(window, "SensorStatusText"));
        }
        finally { window.Close(); }
    }
}
