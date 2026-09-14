using System.IO;
using System.IO.Ports;
using System.Text;
using System.Windows;
using UltrasonicMonitor.Protocol;

namespace UltrasonicMonitor;

public partial class MainWindow
{
    private SerialPort? _rs485Port;
    private bool IsRs485Selected => TransportComboBox.SelectedIndex == 1;
    private string TransportName => IsRs485Selected ? "RS-485" : "TCP";

    private void InitializeRs485()
    {
        TransportComboBox.SelectedIndex = 0;
        TransportComboBox.SelectionChanged += (_, _) => UpdateTransportControls();
        RefreshRs485Ports();
    }

    private void RefreshRs485Ports()
    {
        string? selected = Rs485PortComboBox.SelectedItem as string;
        string[] ports = SerialPort.GetPortNames();
        Array.Sort(ports, StringComparer.OrdinalIgnoreCase);
        Rs485PortComboBox.ItemsSource = ports;
        Rs485PortComboBox.SelectedItem = selected is not null && ports.Contains(selected)
            ? selected : ports.LastOrDefault();
    }

    private void Rs485Ports_DropDownOpened(object? sender, EventArgs e)
    {
        RefreshRs485Ports();
    }

    private void UpdateTransportControls()
    {
        bool serial = IsRs485Selected;
        HostTextBox.Visibility = serial ? Visibility.Collapsed : Visibility.Visible;
        TcpPortTextBox.Visibility = HostTextBox.Visibility;
        Rs485PortComboBox.Visibility = serial ? Visibility.Visible : Visibility.Collapsed;
        UartCheckButton.Content = serial ? "센서 경로 PING (RS-485)" : "센서 경로 PING (TCP)";
        TransportDetailText.Text = serial ? "RS-485 · 115200 8N1 · CRLF" : "Ethernet · TCP · CRLF";
    }

    private async Task ConnectRs485Async(string name)
    {
        Disconnect();
        if (_uartTestPort == name || _rs485TestPort == name)
        {
            ConnectionStatusText.Text = "개별 시험에서 사용 중인 COM 포트입니다";
            return;
        }
        var port = new SerialPort(name, 115200, Parity.None, 8, StopBits.One)
        {
            Encoding = Encoding.ASCII,
            Handshake = Handshake.None,
            DtrEnable = false,
            RtsEnable = false,
            ReadTimeout = 250,
            WriteTimeout = 250
        };
        _rs485Port = port;
        TransportComboBox.IsEnabled = Rs485PortComboBox.IsEnabled = false;
        ConnectButton.Content = "취소";
        ConnectionStatusText.Text = $"{name} 여는 중…";
        try
        {
            port.Open();
            // 이전 연결의 미완성 명령과 늦은 응답이 새 요청에 섞이지 않게 한다.
            await Task.Delay(200);
            if (ReferenceEquals(_rs485Port, port) == false)
            {
                return;
            }
            port.DiscardInBuffer();
            port.DataReceived += Rs485_DataReceived;
            port.ErrorReceived += Rs485_ErrorReceived;
            SetConnectionState(true, $"RS-485 {name}");
            ConnectionStatusText.Text = $"{name} 열림 · 보드 확인 중";
            AppendLog("INFO", $"RS-485 포트 열림: {name} · 115200 8N1");
            SendCommand("PING");
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException
            or InvalidOperationException or ArgumentException)
        {
            if (ReferenceEquals(_rs485Port, port) == false)
            {
                return;
            }
            Disconnect();
            AppendLog("ERROR", $"RS-485 연결 실패: {exception.Message}");
        }
    }

    private void CloseRs485()
    {
        var port = _rs485Port;
        _rs485Port = null;
        if (port is null)
        {
            return;
        }
        port.DataReceived -= Rs485_DataReceived;
        port.ErrorReceived -= Rs485_ErrorReceived;
        try
        {
            port.Dispose();
        }
        catch (IOException exception)
        {
            AppendLog("ERROR", $"RS-485 종료: {exception.Message}");
        }
        AppendLog("INFO", "RS-485 연결 해제됨");
    }

    private void Rs485_DataReceived(object sender, SerialDataReceivedEventArgs e)
    {
        var port = (SerialPort)sender;
        try
        {
            string chunk = port.ReadExisting();
            Dispatcher.InvokeAsync(() => ProcessRs485Chunk(port, chunk));
        }
        catch (Exception exception) when (exception is IOException or InvalidOperationException)
        {
            Dispatcher.InvokeAsync(() => FailRs485(port, exception.Message));
        }
    }

    private void Rs485_ErrorReceived(object sender, SerialErrorReceivedEventArgs e)
    {
        Dispatcher.InvokeAsync(() => FailRs485((SerialPort)sender, $"수신 오류: {e.EventType}"));
    }

    private void FailRs485(SerialPort port, string reason)
    {
        if (ReferenceEquals(_rs485Port, port) == false)
        {
            return;
        }
        string? pending = _pendingCommand;
        Disconnect();
        AppendLog("ERROR", $"RS-485 통신 종료: {reason}");
        if (pending is not null)
        {
            ShowCommandFailure(pending, "RS-485 연결 끊김 · 다시 연결하세요");
        }
    }

    private void ProcessRs485Chunk(SerialPort port, string chunk)
    {
        if (ReferenceEquals(_rs485Port, port) == false)
        {
            return;
        }
        LineBufferResult result = _lineBuffer.Append(chunk);
        if (result.OverflowCount > 0)
        {
            AppendLog("RX-ERROR", "RS-485 수신 라인 길이 초과");
        }
        foreach (string line in result.Lines)
        {
            // 에코를 제공하는 USB 변환기에서도 요청을 응답으로 처리하지 않는다.
            if (line == _pendingCommand)
            {
                AppendLog("ECHO", line, _pendingCommand);
                continue;
            }
            HandleReceivedLine(line);
        }
    }
}
