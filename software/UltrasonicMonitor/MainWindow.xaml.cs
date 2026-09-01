using System.Collections.ObjectModel;
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
    private readonly AsciiLineBuffer _lineBuffer = new(maxLineLength: 64);
    private readonly DispatcherTimer _receiveTimeoutTimer;
    private SerialPort? _serialPort;
    private DateTime _lastByteReceivedUtc;
    private CancellationTokenSource? _responseTimeoutCancellation;
    private string? _pendingCommand;

    public MainWindow()
    {
        InitializeComponent();
        LogListBox.ItemsSource = _logEntries;
        _receiveTimeoutTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(25) };
        _receiveTimeoutTimer.Tick += ReceiveTimeoutTimer_Tick;
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
        ConnectionStatusText.Text = connected ? $"{portName} 연결됨" : "연결 안 됨";
        ConnectionStatusText.Foreground = new SolidColorBrush(connected
            ? Color.FromRgb(29, 125, 79)
            : Color.FromRgb(82, 97, 107));
        ConnectButton.Content = connected ? "연결 해제" : "연결";
        PortComboBox.IsEnabled = !connected;
        StartButton.IsEnabled = connected;
        StopButton.IsEnabled = connected;
        StatusButton.IsEnabled = connected;
    }

    private void StartButton_Click(object sender, RoutedEventArgs e) => SendCommand("START");
    private void StopButton_Click(object sender, RoutedEventArgs e) => SendCommand("STOP");
    private void StatusButton_Click(object sender, RoutedEventArgs e) => SendCommand("GET_STATUS");

    private void SendCommand(string command)
    {
        if (_serialPort?.IsOpen != true)
        {
            AppendLog("ERROR", "COM 포트가 연결되지 않았습니다.");
            return;
        }
        if (_pendingCommand is not null)
        {
            AppendLog("WARN", $"{_pendingCommand} 응답 대기 중이므로 {command} 송신을 보류했습니다.");
            return;
        }

        try
        {
            _serialPort.Write(command + "\r\n");
            AppendLog("TX", command + "<CR><LF>");
            _pendingCommand = command;
            _responseTimeoutCancellation = new CancellationTokenSource();
            _ = WaitForResponseTimeoutAsync(command, _responseTimeoutCancellation.Token);
        }
        catch (Exception exception) when (exception is InvalidOperationException or IOException or TimeoutException)
        {
            AppendLog("ERROR", $"송신 실패: {exception.Message}");
            CancelPendingCommand();
        }
    }

    private async Task WaitForResponseTimeoutAsync(string command, CancellationToken cancellationToken)
    {
        try { await Task.Delay(ResponseTimeout, cancellationToken); }
        catch (OperationCanceledException) { return; }

        if (_pendingCommand == command)
        {
            AppendLog("TIMEOUT", $"{command} 응답이 500 ms 안에 도착하지 않았습니다.");
            CancelPendingCommand();
        }
    }

    private void SerialPort_DataReceived(object sender, SerialDataReceivedEventArgs e)
    {
        try
        {
            string chunk = ((SerialPort)sender).ReadExisting();
            if (chunk.Length == 0) return;
            _lastByteReceivedUtc = DateTime.UtcNow;
            LineBufferResult result = _lineBuffer.Append(chunk);
            Dispatcher.InvokeAsync(() =>
            {
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

    private void HandleReceivedLine(string line)
    {
        AppendLog("RX", line + "<CR><LF>");
        ProtocolMessage message = AsciiProtocolParser.Parse(line);
        switch (message.Kind)
        {
            case ProtocolMessageKind.Ready:
                SystemStateText.Text = "IDLE";
                break;
            case ProtocolMessageKind.State:
            case ProtocolMessageKind.CommandSucceeded:
                if (message.State is not null) SystemStateText.Text = message.State;
                CompletePendingCommand(message);
                break;
            case ProtocolMessageKind.CommandError:
                AppendLog("MCU-ERR", $"{message.Command}: {message.ErrorCode}");
                CompletePendingCommand(message);
                break;
            case ProtocolMessageKind.Ultrasonic:
                UpdateUltrasonicDisplay(message);
                break;
            case ProtocolMessageKind.Unknown:
                AppendLog("PARSE", message.Description ?? "알 수 없는 메시지 형식입니다.");
                break;
        }
    }

    private void UpdateUltrasonicDisplay(ProtocolMessage message)
    {
        SensorStatusText.Text = message.SensorStatus ?? "UNKNOWN";
        PulseText.Text = message.PulseMicroseconds?.ToString() ?? "—";
        DistanceText.Text = message.SensorStatus == "OK" && message.DistanceCentimeters is not null
            ? message.DistanceCentimeters.Value.ToString() : "—";
        LastUpdateText.Text = $"마지막 센서 수신: {DateTime.Now:HH:mm:ss.fff}";
        SensorStatusText.Foreground = new SolidColorBrush(message.SensorStatus switch
        {
            "OK" => Color.FromRgb(29, 125, 79),
            "TIMEOUT" => Color.FromRgb(183, 50, 50),
            _ => Color.FromRgb(181, 111, 0),
        });
    }

    private void CompletePendingCommand(ProtocolMessage message)
    {
        if (_pendingCommand is null) return;
        bool matches = string.Equals(message.Command, _pendingCommand, StringComparison.Ordinal)
            || (_pendingCommand == "START" && message.State == "AUTO")
            || (_pendingCommand == "STOP" && message.State == "STOP");
        if (matches) CancelPendingCommand();
    }

    private void CancelPendingCommand()
    {
        _responseTimeoutCancellation?.Cancel();
        _responseTimeoutCancellation?.Dispose();
        _responseTimeoutCancellation = null;
        _pendingCommand = null;
    }

    private void AppendLog(string direction, string text)
    {
        _logEntries.Add($"{DateTime.Now:HH:mm:ss.fff} [{direction,-8}] {text}");
        while (_logEntries.Count > 500) _logEntries.RemoveAt(0);
        if (_logEntries.Count > 0) LogListBox.ScrollIntoView(_logEntries[^1]);
    }

    private void Window_Closing(object? sender, System.ComponentModel.CancelEventArgs e) => Disconnect();
}
