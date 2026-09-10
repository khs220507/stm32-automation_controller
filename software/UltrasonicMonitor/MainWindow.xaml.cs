using System.Collections.ObjectModel;
using System.Globalization;
using System.IO;
using System.IO.Ports;
using System.Text;
using System.Windows;
using System.Windows.Media;
using System.Windows.Threading;
using UltrasonicMonitor.Protocol;

namespace UltrasonicMonitor;

public partial class MainWindow : Window
{
    private static readonly TimeSpan InterByteTimeout = TimeSpan.FromMilliseconds(50);
    private static readonly TimeSpan ResponseTimeout = TimeSpan.FromMilliseconds(500);
    private readonly ObservableCollection<string> _logEntries = [];
    private readonly ObservableCollection<string> _uartLogEntries = [];
    private readonly ObservableCollection<string> _mpuLogEntries = [];
    private readonly ObservableCollection<string> _ultrasonicLogEntries = [];
    private readonly ObservableCollection<string> _w5500LogEntries = [];
    private readonly AsciiLineBuffer _lineBuffer = new(maxLineLength: 64);
    private readonly DispatcherTimer _receiveTimeoutTimer;
    private readonly DispatcherTimer _sensorPollTimer;
    private bool _ultrasonicRepeating;
    private bool _mpuRepeating;
    private bool _accelRepeating;
    private bool _accelNeedsConfiguration;
    private bool _mpuTurn;
    private bool _connected;
    private SerialPort? _serialPort;
    private DateTime _lastByteReceivedUtc;
    private CancellationTokenSource? _responseTimeoutCancellation;
    private string? _pendingCommand;

    public MainWindow()
    {
        InitializeComponent();
        SetMpuDisplay("확인 전");
        LogListBox.ItemsSource = _logEntries;
        UartLogListBox.ItemsSource = _uartLogEntries;
        MpuLogListBox.ItemsSource = _mpuLogEntries;
        UltrasonicLogListBox.ItemsSource = _ultrasonicLogEntries;
        W5500LogListBox.ItemsSource = _w5500LogEntries;
        _receiveTimeoutTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(25) };
        _receiveTimeoutTimer.Tick += ReceiveTimeoutTimer_Tick;
        // 각 센서의 실행 여부는 독립적이며 UART 요청만 번갈아 처리한다.
        _sensorPollTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(250) };
        _sensorPollTimer.Tick += (_, _) => RunSensorScheduler();
        RefreshPorts();
        AppendLog("INFO", "앱 준비 완료. NUCLEO의 ST-LINK Virtual COM Port를 선택하세요.");
    }

    private void RefreshPorts_Click(object sender, RoutedEventArgs e) => RefreshPorts();

    private void RefreshPorts()
    {
        string? selectedPort = PortComboBox.SelectedItem as string;
        string[] portNames = SerialPort.GetPortNames();
        Array.Sort(portNames, StringComparer.OrdinalIgnoreCase);
        PortComboBox.ItemsSource = portNames;
        PortComboBox.SelectedItem = selectedPort is not null && portNames.Contains(selectedPort)
            ? selectedPort
            : portNames.FirstOrDefault();
        AppendLog("INFO", portNames.Length == 0
            ? "사용 가능한 COM 포트가 없습니다."
            : $"COM 포트 검색: {string.Join(", ", portNames)}");
    }

    private void Connect_Click(object sender, RoutedEventArgs e)
    {
        if (_serialPort?.IsOpen == true)
        {
            Disconnect();
            return;
        }

        if (PortComboBox.SelectedItem is not string portName)
        {
            MessageBox.Show(this, "연결할 COM 포트를 선택하세요.", "COM 포트",
                MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }

        try
        {
            var serialPort = new SerialPort(portName, 115200, Parity.None, 8, StopBits.One)
            {
                Encoding = Encoding.ASCII,
                Handshake = Handshake.None,
                NewLine = "\r\n",
                ReadTimeout = 50,
                WriteTimeout = 500,
                DtrEnable = false,
                RtsEnable = false,
            };
            serialPort.DataReceived += SerialPort_DataReceived;
            serialPort.ErrorReceived += SerialPort_ErrorReceived;
            serialPort.Open();
            _serialPort = serialPort;
            SetConnectionState(true, portName);
            AppendLog("INFO", $"{portName} 연결됨 (115200, 8-N-1, CRLF)");
        }
        catch (Exception exception) when (exception is UnauthorizedAccessException or IOException or ArgumentException)
        {
            AppendLog("ERROR", $"연결 실패: {exception.Message}");
            MessageBox.Show(this, exception.Message, "시리얼 연결 실패",
                MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void Disconnect()
    {
        StopUltrasonic();
        CancelPendingCommand();
        _receiveTimeoutTimer.Stop();
        _lineBuffer.Reset();
        if (_serialPort is not null)
        {
            string portName = _serialPort.PortName;
            _serialPort.DataReceived -= SerialPort_DataReceived;
            _serialPort.ErrorReceived -= SerialPort_ErrorReceived;
            try
            {
                if (_serialPort.IsOpen) _serialPort.Close();
            }
            catch (IOException exception)
            {
                AppendLog("ERROR", $"포트 닫기 실패: {exception.Message}");
            }
            _serialPort.Dispose();
            _serialPort = null;
            AppendLog("INFO", $"{portName} 연결 해제됨");
        }
        SetConnectionState(false, null);
    }

    private void SetConnectionState(bool connected, string? portName)
    {
        _connected = connected;
        ConnectionStatusText.Text = connected ? $"{portName} 연결됨" : "연결 안 됨";
        ConnectionStatusText.Foreground = new SolidColorBrush(connected
            ? Color.FromRgb(29, 125, 79)
            : Color.FromRgb(82, 97, 107));
        ConnectButton.Content = connected ? "연결 해제" : "연결";
        PortComboBox.IsEnabled = !connected;
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

    private void SendCommand(string command)
    {
        if (_serialPort?.IsOpen != true)
        {
            AppendLog("ERROR", "COM 포트가 연결되지 않았습니다.", command);
            ShowCommandFailure(command, "COM 포트 연결을 확인하세요");
            return;
        }
        if (_pendingCommand is not null)
        {
            AppendLog("WARN", $"{_pendingCommand} 응답 대기 중이므로 {command} 송신을 보류했습니다.", command);
            return;
        }

        try
        {
            _serialPort.Write(command + "\r\n");
            AppendLog("TX", command + "<CR><LF>", command);
            _pendingCommand = command;
            BeginCommandDisplay(command);
            _responseTimeoutCancellation = new CancellationTokenSource();
            _ = WaitForResponseTimeoutAsync(command, _responseTimeoutCancellation.Token);
        }
        catch (Exception exception) when (exception is InvalidOperationException or IOException or TimeoutException)
        {
            AppendLog("ERROR", $"송신 실패: {exception.Message}", command);
            ShowCommandFailure(command, "명령 송신 실패");
            CancelPendingCommand();
        }
    }

    private async Task WaitForResponseTimeoutAsync(string command, CancellationToken cancellationToken)
    {
        try { await Task.Delay(ResponseTimeout, cancellationToken); }
        catch (OperationCanceledException) { return; }

        if (_pendingCommand == command)
        {
            AppendLog("TIMEOUT", $"{command} 응답이 500 ms 안에 도착하지 않았습니다.", command);
            ShowCommandFailure(command, "보드 응답 없음 (500 ms)");
            CancelPendingCommand();
        }
    }

    private void SerialPort_DataReceived(object sender, SerialDataReceivedEventArgs e)
    {
        try
        {
            string chunk = ((SerialPort)sender).ReadExisting();
            if (chunk.Length == 0) return;
            DateTime receivedUtc = DateTime.UtcNow;
            Dispatcher.InvokeAsync(() =>
            {
                // 이전 연결에서 큐에 남은 수신으로 새 연결의 진단 결과를 덮어쓰지 않는다.
                if (!ReferenceEquals(sender, _serialPort)) return;
                _lastByteReceivedUtc = receivedUtc;
                LineBufferResult result = _lineBuffer.Append(chunk);
                _receiveTimeoutTimer.Start();
                for (int index = 0; index < result.OverflowCount; index++)
                    AppendLog("RX-ERROR", "64바이트를 초과한 라인을 폐기했습니다.");
                foreach (string line in result.Lines) HandleReceivedLine(line);
            });
        }
        catch (Exception exception) when (exception is InvalidOperationException or IOException)
        {
            Dispatcher.InvokeAsync(() => AppendLog("ERROR", $"수신 실패: {exception.Message}"));
        }
    }

    private void SerialPort_ErrorReceived(object sender, SerialErrorReceivedEventArgs e) =>
        Dispatcher.InvokeAsync(() => AppendLog("SERIAL", $"시리얼 오류: {e.EventType}"));

    private void ReceiveTimeoutTimer_Tick(object? sender, EventArgs e)
    {
        if (!_lineBuffer.HasPendingData)
        {
            _receiveTimeoutTimer.Stop();
            return;
        }
        if (DateTime.UtcNow - _lastByteReceivedUtc >= InterByteTimeout)
        {
            _lineBuffer.Reset();
            _receiveTimeoutTimer.Stop();
            AppendLog("RX-ERROR", "불완전한 라인을 50 ms 바이트 간 시간초과로 폐기했습니다.");
        }
    }

    // 수신 흐름: 한 줄 해석 → 로그 기록 → 요청 일치 확인 → 화면 갱신 → 대기 해제.
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

    private void Window_Closing(object? sender, System.ComponentModel.CancelEventArgs e) => Disconnect();

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
