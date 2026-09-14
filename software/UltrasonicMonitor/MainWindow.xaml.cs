using System.Collections.ObjectModel;
using System.Globalization;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Windows;
using System.Windows.Media;
using System.Windows.Threading;
using UltrasonicMonitor.Protocol;

namespace UltrasonicMonitor;

public partial class MainWindow : Window
{
    private static readonly TimeSpan ResponseTimeout = TimeSpan.FromMilliseconds(500);
    private readonly ObservableCollection<string> _logEntries = [];
    private readonly ObservableCollection<string> _uartLogEntries = [];
    private readonly ObservableCollection<string> _mpuLogEntries = [];
    private readonly ObservableCollection<string> _ultrasonicLogEntries = [];
    private readonly ObservableCollection<string> _w5500LogEntries = [];
    private readonly AsciiLineBuffer _lineBuffer = new(maxLineLength: 64);
    private readonly DispatcherTimer _sensorPollTimer;
    private bool _ultrasonicRepeating;
    private bool _mpuRepeating;
    private bool _accelRepeating;
    private bool _accelNeedsConfiguration;
    private bool _mpuTurn;
    private bool _connected;
    private TcpClient? _tcpClient;
    private CancellationTokenSource? _connectionCancellation;
    private CancellationTokenSource? _responseTimeoutCancellation;
    private string? _pendingCommand;

    public MainWindow()
    {
        InitializeComponent();
        InitializeUartDiagnostics();
        InitializeRs485();
        SetMpuDisplay("확인 전");
        LogListBox.ItemsSource = _logEntries;
        UartLogListBox.ItemsSource = _uartLogEntries;
        MpuLogListBox.ItemsSource = _mpuLogEntries;
        UltrasonicLogListBox.ItemsSource = _ultrasonicLogEntries;
        W5500LogListBox.ItemsSource = _w5500LogEntries;
        _sensorPollTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(250) };
        _sensorPollTimer.Tick += (_, _) => RunSensorScheduler();
        AppendLog("INFO", "TCP 또는 RS-485를 선택하고 연결하세요. RS-485는 115200 8N1입니다.");
    }

    private async void Connect_Click(object sender, RoutedEventArgs e)
    {
        if (_tcpClient is not null || _rs485Port is not null)
        {
            Disconnect();
            return;
        }
        if (IsRs485Selected)
        {
            if (Rs485PortComboBox.SelectedItem is not string name)
            {
                AppendLog("ERROR", "USB-RS485 COM 포트를 선택하세요.");
                RefreshRs485Ports();
                return;
            }
            await ConnectRs485Async(name);
            return;
        }
        if (!IPAddress.TryParse(HostTextBox.Text.Trim(), out var address) ||
            address.AddressFamily != AddressFamily.InterNetwork ||
            !int.TryParse(TcpPortTextBox.Text, out int port) || port is < 1 or > 65535)
        {
            AppendLog("ERROR", "IPv4 주소와 1~65535 범위의 포트를 입력하세요.");
            return;
        }
        await ConnectTcpAsync(address, port);
    }

    private async Task ConnectTcpAsync(IPAddress address, int port)
    {
        Disconnect();
        var client = new TcpClient { NoDelay = true };
        var cancellation = new CancellationTokenSource();
        _tcpClient = client;
        _connectionCancellation = cancellation;
        HostTextBox.IsEnabled = TcpPortTextBox.IsEnabled = false;
        TransportComboBox.IsEnabled = false;
        ConnectButton.Content = "취소";
        ConnectionStatusText.Text = "TCP 연결 중…";
        try
        {
            using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellation.Token);
            deadline.CancelAfter(TimeSpan.FromSeconds(3));
            await client.ConnectAsync(address, port, deadline.Token);
            if (!ReferenceEquals(_tcpClient, client)) return;
            SetConnectionState(true, $"{address}:{port}");
            AppendLog("INFO", $"TCP 연결됨: {address}:{port}");
            _ = ReceiveTcpAsync(client, cancellation.Token);
        }
        catch (Exception exception) when (exception is SocketException or IOException or OperationCanceledException or ObjectDisposedException)
        {
            if (!ReferenceEquals(_tcpClient, client)) return;
            Disconnect();
            AppendLog("ERROR", $"TCP 연결 실패: {exception.Message}");
        }
    }

    private void Disconnect()
    {
        var client = _tcpClient;
        _tcpClient = null;
        _connectionCancellation?.Cancel();
        _connectionCancellation?.Dispose();
        _connectionCancellation = null;
        client?.Dispose();
        CloseRs485();
        CancelPendingCommand();
        _lineBuffer.Reset();
        SetConnectionState(false, null);
        if (client is not null) AppendLog("INFO", "TCP 연결 해제됨");
    }

    private void SetConnectionState(bool connected, string? portName)
    {
        _connected = connected;
        ConnectionStatusText.Text = connected ? $"{portName} 연결됨" : "연결 안 됨";
        ConnectionStatusText.Foreground = new SolidColorBrush(connected
            ? Color.FromRgb(29, 125, 79)
            : Color.FromRgb(82, 97, 107));
        ConnectButton.Content = connected ? "연결 해제" : "연결";
        HostTextBox.IsEnabled = TcpPortTextBox.IsEnabled = !connected;
        TransportComboBox.IsEnabled = Rs485PortComboBox.IsEnabled = connected == false;
        ResetDiagnosticDisplays(connected ? "확인 전" : "연결 안 됨");
        UpdateTestButtons();
    }

    private void StartButton_Click(object sender, RoutedEventArgs e)
    {
        if (!_connected) return;
        _ultrasonicRepeating = true;
        MeasurementModeText.Text = "반복 측정 중";
        _sensorPollTimer.Start();
        RunSensorScheduler();
    }
    private void StopButton_Click(object sender, RoutedEventArgs e) => StopUltrasonic();
    private void UartCheckButton_Click(object sender, RoutedEventArgs e) => SendCommand("PING");
    private void W5500CheckButton_Click(object sender, RoutedEventArgs e) => SendCommand("CHECK_W5500");

    private void MpuStartButton_Click(object sender, RoutedEventArgs e)
    {
        if (!_connected) return;
        StopAccel();
        _mpuRepeating = true;
        MpuModeText.Text = "반복 확인 중";
        _sensorPollTimer.Start();
        RunSensorScheduler();
    }

    private void MpuStopButton_Click(object sender, RoutedEventArgs e) => StopMpu();
    private void MpuWakeButton_Click(object sender, RoutedEventArgs e)
    {
        StopAccel();
        SendCommand("WAKE_MPU6050");
    }

    private void AccelStartButton_Click(object sender, RoutedEventArgs e)
    {
        if (!_connected || _pendingCommand is not null) return;
        StopMpu();
        _accelRepeating = true;
        _accelNeedsConfiguration = true;
        AccelModeText.Text = "반복 측정 중";
        ClearAccelDisplay("센서 설정 준비 중…");
        _mpuTurn = true;
        _sensorPollTimer.Start();
        RunSensorScheduler();
    }

    private void AccelStopButton_Click(object sender, RoutedEventArgs e) => StopAccel();

    private void StopAccel()
    {
        _accelRepeating = false;
        _accelNeedsConfiguration = true;
        if (!_ultrasonicRepeating && !_mpuRepeating) _sensorPollTimer.Stop();
        AccelModeText.Text = _pendingCommand is "CONFIG_ACCEL" or "READ_ACCEL"
            ? "정지 요청 · 현재 1회 응답 대기" : "정지 · 마지막 값 보관";
        UpdateTestButtons();
    }

    private void StopMpu()
    {
        _mpuRepeating = false;
        if (!_ultrasonicRepeating && !_accelRepeating) _sensorPollTimer.Stop();
        MpuModeText.Text = _pendingCommand == "CHECK_MPU6050"
            ? "정지 요청 · 현재 1회 응답 대기" : "정지";
        UpdateTestButtons();
    }

    private string? NextSensorCommand()
    {
        if (!_connected || _pendingCommand is not null) return null;
        if ((_mpuRepeating || _accelRepeating) && (!_ultrasonicRepeating || _mpuTurn))
        {
            _mpuTurn = false;
            return _accelRepeating ? (_accelNeedsConfiguration ? "CONFIG_ACCEL" : "READ_ACCEL") : "CHECK_MPU6050";
        }
        if (_ultrasonicRepeating)
        {
            _mpuTurn = true;
            return "CHECK_HCSR04";
        }
        return null;
    }

    private void RunSensorScheduler()
    {
        string? command = NextSensorCommand();
        if (command is not null) SendCommand(command);
        UpdateTestButtons();
    }

    private void StopUltrasonic()
    {
        _ultrasonicRepeating = false;
        if (!_mpuRepeating && !_accelRepeating) _sensorPollTimer.Stop();
        MeasurementModeText.Text = _pendingCommand == "CHECK_HCSR04"
            ? "정지 요청 · 현재 1회 응답 대기" : "정지";
        UpdateTestButtons();
    }

    private void UpdateTestButtons()
    {
        bool available = _connected && _pendingCommand is null;
        UartCheckButton.IsEnabled = available;
        W5500CheckButton.IsEnabled = available;
        MpuWakeButton.IsEnabled = available;
        AccelStartButton.IsEnabled = available && !_accelRepeating;
        AccelStopButton.IsEnabled = _connected && _accelRepeating;
        StartButton.IsEnabled = _connected && !_ultrasonicRepeating;
        MpuStartButton.IsEnabled = _connected && !_mpuRepeating;
        MpuStopButton.IsEnabled = _connected && _mpuRepeating;
        StopButton.IsEnabled = _connected && _ultrasonicRepeating;
    }

    private void ResetDiagnosticDisplays(string status)
    {
        StopAccel();
        ClearAccelDisplay(status);
        AccelTimeText.Text = "측정 이력 없음";
        AccelModeText.Text = "정지";
        StopUltrasonic();
        StopMpu();
        SetMpuDisplay(status);
        SetMpuWakeDisplay(status);
        MpuWakeTimeText.Text = "확인 이력 없음";
        SetUartDisplay(status);
        SetW5500Display(status);
        W5500LastCheckText.Text = "확인 이력 없음";
        SetUltrasonicStatus(status);
        MpuLastCheckText.Text = UartLastCheckText.Text = SensorLastCheckText.Text = "확인 이력 없음";
    }

    private void BeginCommandDisplay(string command)
    {
        if (command == "CHECK_W5500")
        {
            SetW5500Display("확인 중…");
            W5500LastCheckText.Text = "응답 대기 중";
        }
        if (command is "CONFIG_ACCEL" or "READ_ACCEL")
            AccelStatusText.Text = command == "CONFIG_ACCEL" ? "±2g 설정·확인 중…" : "새 측정값 대기 중…";
        if (command == "WAKE_MPU6050")
        {
            SetMpuWakeDisplay("SLEEP 해제·확인 중…");
            MpuWakeTimeText.Text = "응답 대기 중";
        }
        else if (command == "CHECK_MPU6050")
        {
            SetMpuDisplay("확인 중…");
            MpuLastCheckText.Text = "응답 대기 중";
        }
        else if (command == "PING")
        {
            SetUartDisplay("확인 중…");
            UartLastCheckText.Text = "응답 대기 중";
        }
        else if (command == "CHECK_HCSR04")
        {
            SetUltrasonicStatus("측정 중…");
            SensorLastCheckText.Text = "응답 대기 중";
        }
        UpdateTestButtons();
    }

    private void ShowCommandFailure(string command, string reason)
    {
        string timestamp = $"마지막 시험: {DateTime.Now:HH:mm:ss.fff}";
        switch (command)
        {
            case "CHECK_W5500":
                SetW5500Display(reason, failed: true);
                W5500LastCheckText.Text = timestamp;
                break;
            case "CONFIG_ACCEL":
            case "READ_ACCEL":
                StopAccel();
                ClearAccelDisplay(reason, failed: true);
                AccelTimeText.Text = timestamp;
                break;
            case "WAKE_MPU6050":
                SetMpuWakeDisplay(reason, failed: true);
                MpuWakeTimeText.Text = timestamp;
                break;
            case "PING":
                SetUartDisplay(reason, failed: true);
                UartLastCheckText.Text = timestamp;
                break;
            case "CHECK_MPU6050":
                StopMpu();
                SetMpuDisplay(reason, failed: true);
                MpuLastCheckText.Text = timestamp;
                break;
            case "CHECK_HCSR04":
                StopUltrasonic();
                SetUltrasonicStatus(reason, failed: true);
                SensorLastCheckText.Text = timestamp;
                break;
        }
    }

    private async void SendCommand(string command)
    {
        var client = _tcpClient;
        var serial = _rs485Port;
        if (_connected == false || (client is null && serial is null))
        {
            AppendLog("ERROR", $"{TransportName} 연결이 필요합니다.", command);
            ShowCommandFailure(command, $"{TransportName} 연결을 확인하세요");
            return;
        }
        if (_pendingCommand is not null) return;
        _pendingCommand = command;
        BeginCommandDisplay(command);
        _responseTimeoutCancellation = new CancellationTokenSource();
        var token = _responseTimeoutCancellation.Token;
        _ = WaitForResponseTimeoutAsync(command, token);
        try
        {
            AppendLog("TX", command + "<CR><LF>", command);
            if (serial is not null)
            {
                await Task.Run(() => serial.Write(command + "\r\n"), token);
            }
            else
            {
                await client!.GetStream().WriteAsync(Encoding.ASCII.GetBytes(command + "\r\n"), token);
            }
        }
        catch (Exception exception) when (exception is IOException or SocketException or OperationCanceledException
            or ObjectDisposedException or InvalidOperationException or TimeoutException or UnauthorizedAccessException)
        {
            if (ReferenceEquals(_tcpClient, client) == false || ReferenceEquals(_rs485Port, serial) == false
                || token.IsCancellationRequested)
            {
                return;
            }
            Disconnect();
            AppendLog("ERROR", $"{TransportName} 송신 실패: {exception.Message}", command);
            ShowCommandFailure(command, $"{TransportName} 송신 실패 · 다시 연결하세요");
        }
    }

    private async Task WaitForResponseTimeoutAsync(string command, CancellationToken cancellationToken)
    {
        try { await Task.Delay(ResponseTimeout, cancellationToken); }
        catch (OperationCanceledException) { return; }

        if (_pendingCommand == command)
        {
            AppendLog("TIMEOUT", $"{command} 응답이 500 ms 안에 도착하지 않았습니다.", command);
            if (_tcpClient is not null || _rs485Port is not null)
            {
                Disconnect();
            }
            ShowCommandFailure(command, "보드 응답 없음 (500 ms)");
            CancelPendingCommand();
        }
    }

    private async Task ReceiveTcpAsync(TcpClient client, CancellationToken cancellationToken)
    {
        byte[] bytes = new byte[512];
        string reason = "보드가 TCP 연결을 종료했습니다.";
        try
        {
            var stream = client.GetStream();
            while (true)
            {
                int count = await stream.ReadAsync(bytes, cancellationToken);
                if (!ReferenceEquals(_tcpClient, client)) return;
                if (count == 0) break;
                LineBufferResult result = _lineBuffer.Append(Encoding.ASCII.GetString(bytes, 0, count));
                for (int i = 0; i < result.OverflowCount; ++i)
                    AppendLog("RX-ERROR", "64바이트를 초과한 라인을 폐기했습니다.");
                foreach (string line in result.Lines) HandleReceivedLine(line);
            }
        }
        catch (Exception exception) when (exception is IOException or SocketException or OperationCanceledException or ObjectDisposedException)
        {
            reason = $"TCP 수신 종료: {exception.Message}";
        }
        if (!ReferenceEquals(_tcpClient, client)) return;
        string? pending = _pendingCommand;
        Disconnect();
        AppendLog("ERROR", reason);
        if (pending is not null) ShowCommandFailure(pending, "TCP 연결 끊김 · 다시 연결하세요");
    }

    private void HandleReceivedLine(string line)
    {
        ProtocolMessage message = AsciiProtocolParser.Parse(line);
        string? logCommand = message.Kind == ProtocolMessageKind.Ultrasonic
            ? "CHECK_HCSR04" : message.Command;
        AppendLog("RX", line + "<CR><LF>", logCommand);

        // READY/FAULT는 요청 응답이 아니라 보드가 보내는 상태 알림이다.
        if (message.Kind == ProtocolMessageKind.Ready || message.State == "FAULT")
        {
            CancelPendingCommand();
            ResetDiagnosticDisplays(message.Kind == ProtocolMessageKind.Ready
                ? "보드 재시작 · 다시 확인하세요" : "보드 고장 상태 · 다시 확인하세요");
            return;
        }
        if (message.Kind == ProtocolMessageKind.Unknown)
        {
            AppendLog("PARSE", message.Description ?? "알 수 없는 메시지 형식입니다.");
            return;
        }
        if (message.Kind == ProtocolMessageKind.CommandError)
            AppendLog("MCU-ERR", $"{message.Command}: {message.ErrorCode}", message.Command);

        // 늦게 도착한 응답·이전 자동운전 출력은 로그에만 남긴다.
        if (_pendingCommand is null || message.Command != _pendingCommand) return;

        switch (message.Kind)
        {
            case ProtocolMessageKind.CommandError:
                if (message.Command == "READ_ACCEL" && message.ErrorCode == "WARMING_UP")
                    ClearAccelDisplay("센서 초기 대기 중…");
                else if (message.Command == "CHECK_MPU6050") UpdateMpuDisplay(message);
                else if (message.Command == "WAKE_MPU6050") UpdateMpuWakeDisplay(message);
                else if (message.Command == "CHECK_W5500") UpdateW5500Display(message);
                else ShowCommandFailure(_pendingCommand, $"시험 오류: {message.ErrorCode}");
                break;
            case ProtocolMessageKind.AccelConfigured:
                _accelNeedsConfiguration = false;
                ClearAccelDisplay("±2g 설정 확인됨 · 첫 측정 대기");
                break;
            case ProtocolMessageKind.Accelerometer:
                UpdateAccelDisplay(message);
                break;
            case ProtocolMessageKind.Mpu6050Wake:
                UpdateMpuWakeDisplay(message);
                break;
            case ProtocolMessageKind.Mpu6050:
                UpdateMpuDisplay(message);
                break;
            case ProtocolMessageKind.W5500:
                UpdateW5500Display(message);
                break;
            case ProtocolMessageKind.Uart:
                SetUartDisplay("요청·응답 확인됨 (PONG)", succeeded: true);
                if (_rs485Port is not null)
                {
                    ConnectionStatusText.Text = $"RS-485 {_rs485Port.PortName} 응답 확인됨";
                }
                UartLastCheckText.Text = $"마지막 시험: {DateTime.Now:HH:mm:ss.fff}";
                break;
            case ProtocolMessageKind.Ultrasonic:
                UpdateUltrasonicDisplay(message);
                break;
            default:
                return;
        }
        CancelPendingCommand();
    }
    private void UpdateUltrasonicDisplay(ProtocolMessage message)
    {
        SensorStatusText.Text = message.SensorStatus switch
        {
            "OK" => "측정 성공",
            "TIMEOUT" => "센서 응답 시간 초과",
            "OUT_OF_RANGE" => "측정 범위 밖",
            _ => "알 수 없는 결과",
        };
        PulseText.Text = message.PulseMicroseconds?.ToString() ?? "—";
        DistanceText.Text = message.SensorStatus == "OK" && message.DistanceCentimeters is not null
            ? message.DistanceCentimeters.Value.ToString() : "—";
        SensorLastCheckText.Text = $"마지막 시험: {DateTime.Now:HH:mm:ss.fff}";
        SensorStatusText.Foreground = new SolidColorBrush(message.SensorStatus switch
        {
            "OK" => Color.FromRgb(29, 125, 79),
            "TIMEOUT" => Color.FromRgb(183, 50, 50),
            _ => Color.FromRgb(181, 111, 0),
        });
    }

    private void SetW5500Display(string status, byte? version = null, bool failed = false)
    {
        W5500StatusText.Text = status;
        W5500VersionText.Text = version is byte value
            ? $"버전 값: 0x{value:X2} · 기대값: 0x{AsciiProtocolParser.ExpectedW5500Version:X2}"
            : $"버전 값: — · 기대값: 0x{AsciiProtocolParser.ExpectedW5500Version:X2}";
        W5500StatusText.Foreground = new SolidColorBrush(failed ? Color.FromRgb(183, 50, 50)
            : version == AsciiProtocolParser.ExpectedW5500Version ? Color.FromRgb(29, 125, 79)
            : Color.FromRgb(82, 97, 107));
    }

    private void UpdateW5500Display(ProtocolMessage message)
    {
        string status = message.SensorStatus switch
        {
            "OK" => "W5500 버전 확인됨",
            "VERSION_MISMATCH" => "버전 불일치 · 전원·배선을 확인하세요",
            _ => message.ErrorCode switch
            {
                "TIMEOUT" => "모듈 준비 또는 SPI 통신 시간 초과",
                "NOT_READY" => "SPI 준비 안 됨 · 보드를 재시작한 뒤 확인하세요",
                "HARDWARE_ERROR" => "SPI 통신 오류 · 보드 재시작 필요",
                "DIRTY_STATE" => "이전 SPI 전송 상태가 남아 있음 · 보드 재시작 필요",
                "INVALID_STATE" => "보드가 대기 상태일 때 실행하세요",
                _ => $"확인 오류: {message.ErrorCode ?? "UNKNOWN"}",
            },
        };
        SetW5500Display(status, message.Identity, failed: message.SensorStatus != "OK");
        W5500LastCheckText.Text = $"마지막 확인: {DateTime.Now:HH:mm:ss.fff}";
    }

    private void SetUartDisplay(string status, bool failed = false, bool succeeded = false)
    {
        UartStatusText.Text = status;
        UartStatusText.Foreground = new SolidColorBrush(failed ? Color.FromRgb(183, 50, 50)
            : succeeded ? Color.FromRgb(29, 125, 79) : Color.FromRgb(82, 97, 107));
    }

    private void SetUltrasonicStatus(string status, bool failed = false)
    {
        SensorStatusText.Text = status;
        DistanceText.Text = PulseText.Text = "—";
        SensorStatusText.Foreground = new SolidColorBrush(failed
            ? Color.FromRgb(183, 50, 50) : Color.FromRgb(82, 97, 107));
    }

    private void SetMpuDisplay(string status, byte? identity = null, bool failed = false)
    {
        MpuStatusText.Text = status;
        MpuIdentityText.Text = identity is byte value
            ? $"식별값: 0x{value:X2} · 기대값: 0x{AsciiProtocolParser.ExpectedMpu6050Identity:X2}"
            : $"식별값: — · 기대값: 0x{AsciiProtocolParser.ExpectedMpu6050Identity:X2}";
        MpuStatusText.Foreground = new SolidColorBrush(failed
            ? Color.FromRgb(183, 50, 50)
            : identity == AsciiProtocolParser.ExpectedMpu6050Identity
                ? Color.FromRgb(29, 125, 79) : Color.FromRgb(82, 97, 107));
    }

    private void UpdateMpuDisplay(ProtocolMessage message)
    {
        string status = message.SensorStatus switch
        {
            "OK" => "MPU6050 확인됨",
            "ID_MISMATCH" => "식별값 불일치 · 센서 종류를 확인하세요",
            _ => message.ErrorCode switch
            {
                "NACK" => "센서 ACK 응답 없음 · 전원·배선·주소를 확인하세요",
                "BUS_BUSY" => "I2C 버스 사용 중 · SDA/SCL 배선을 확인하세요",
                "TIMEOUT" => "센서 통신 시간 초과",
                "BUS_ERROR" => "I2C 버스 오류",
                "ARBITRATION_LOST" => "I2C 버스 중재 상실",
                "OVERRUN" => "I2C 데이터 처리 오류",
                "NOT_READY" => "I2C 또는 타이머 초기화 상태를 확인하세요",
                "INVALID_STATE" => "자동운전을 정지한 뒤 시험하세요",
                _ => $"진단 오류: {message.ErrorCode ?? "UNKNOWN"}",
            },
        };
        SetMpuDisplay(status, message.Identity, failed: message.SensorStatus != "OK");
        MpuLastCheckText.Text = $"마지막 확인: {DateTime.Now:HH:mm:ss.fff}";
    }

    private void SetMpuWakeDisplay(string status, byte? before = null, byte? after = null,
                                   bool failed = false, bool succeeded = false)
    {
        MpuWakeStatusText.Text = status;
        MpuPowerText.Text = before is byte b && after is byte a
            ? $"변경 전: 0x{b:X2} → 확인값: 0x{a:X2}"
            : "변경 전: — → 확인값: —";
        MpuWakeStatusText.Foreground = new SolidColorBrush(failed ? Color.FromRgb(183, 50, 50)
            : succeeded ? Color.FromRgb(29, 125, 79) : Color.FromRgb(82, 97, 107));
    }

    private void UpdateMpuWakeDisplay(ProtocolMessage message)
    {
        bool succeeded = message.SensorStatus == "OK";
        string status = succeeded ? "SLEEP=0 확인됨" : (message.ErrorCode ?? message.SensorStatus) switch
        {
            "VERIFY_FAILED" => "설정 확인 실패 · 다시 읽은 값이 다릅니다",
            "SENSOR_RESET" => "센서 리셋 중 · 잠시 후 다시 확인하세요",
            "INVALID_STATE" => "보드가 대기 상태일 때 실행하세요",
            "NACK" => "센서 ACK 응답 없음 · 배선을 확인하세요",
            "BUS_BUSY" => "I2C 버스 사용 중 · 배선을 확인하세요",
            "TIMEOUT" => "센서 통신 시간 초과 · 적용 여부 미확인",
            "NOT_READY" => "I2C 또는 타이머 준비 안 됨",
            "BUS_ERROR" => "I2C 버스 오류",
            "ARBITRATION_LOST" => "I2C 버스 중재 상실",
            "OVERRUN" => "I2C 데이터 처리 오류",
            _ => $"설정 확인 오류: {message.ErrorCode ?? message.SensorStatus ?? "UNKNOWN"}",
        };
        SetMpuWakeDisplay(status, message.PowerBefore, message.PowerAfter,
            failed: !succeeded, succeeded: succeeded);
        MpuWakeTimeText.Text = $"마지막 확인: {DateTime.Now:HH:mm:ss.fff}";
    }

    private void CancelPendingCommand()
    {
        _responseTimeoutCancellation?.Cancel();
        _responseTimeoutCancellation?.Dispose();
        _responseTimeoutCancellation = null;
        _pendingCommand = null;
        if (!_ultrasonicRepeating) MeasurementModeText.Text = "정지";
        if (!_mpuRepeating) MpuModeText.Text = "정지";
        if (!_accelRepeating) AccelModeText.Text = "정지 · 마지막 값 보관";
        UpdateTestButtons();
    }

    private void AppendLog(string direction, string text, string? command = null)
    {
        var (entries, listBox) = command switch
        {
            "PING" => (_uartLogEntries, UartLogListBox),
            "CHECK_W5500" => (_w5500LogEntries, W5500LogListBox),
            "CHECK_MPU6050" or "WAKE_MPU6050" or "CONFIG_ACCEL" or "READ_ACCEL" => (_mpuLogEntries, MpuLogListBox),
            "CHECK_HCSR04" => (_ultrasonicLogEntries, UltrasonicLogListBox),
            _ => (_logEntries, LogListBox),
        };
        entries.Insert(0, $"{DateTime.Now:HH:mm:ss.fff} [{direction,-8}] {text}");
        while (entries.Count > 500) entries.RemoveAt(entries.Count - 1);
        listBox.ScrollIntoView(entries[0]);
    }

    private void Window_Closing(object? sender, System.ComponentModel.CancelEventArgs e)
    { CloseUartDiagnostics(); Disconnect(); }

    private void ClearAccelDisplay(string status, bool failed = false)
    {
        AccelXText.Text = AccelYText.Text = AccelZText.Text = "— g";
        AccelXRawText.Text = AccelYRawText.Text = AccelZRawText.Text = "원시값: —";
        AccelStatusText.Text = status;
        AccelStatusText.Foreground = new SolidColorBrush(failed
            ? Color.FromRgb(183, 50, 50) : Color.FromRgb(82, 97, 107));
    }

    private void UpdateAccelDisplay(ProtocolMessage message)
    {
        // MPU6050 ±2g 감도: 16384 LSB/g. MCU는 부호 있는 원시값만 보낸다.
        static string InG(short? raw) => ((raw ?? 0) / 16384.0).ToString("0.000", CultureInfo.InvariantCulture) + " g";
        AccelXText.Text = InG(message.AccelX);
        AccelYText.Text = InG(message.AccelY);
        AccelZText.Text = InG(message.AccelZ);
        AccelXRawText.Text = $"원시값: {message.AccelX}";
        AccelYRawText.Text = $"원시값: {message.AccelY}";
        AccelZRawText.Text = $"원시값: {message.AccelZ}";
        AccelStatusText.Text = "측정값 수신됨 · ±2g 환산";
        AccelStatusText.Foreground = new SolidColorBrush(Color.FromRgb(29, 125, 79));
        AccelTimeText.Text = $"마지막 수신: {DateTime.Now:HH:mm:ss.fff}";
    }
}
