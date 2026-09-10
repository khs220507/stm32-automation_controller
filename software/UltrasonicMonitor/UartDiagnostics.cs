using System.IO;
using System.IO.Ports;
using System.Text;
using System.Windows;
using UltrasonicMonitor.Protocol;
using System.Collections.ObjectModel;

namespace UltrasonicMonitor;

public partial class MainWindow
{
    private SerialPort? _diagnosticPort;
    private readonly AsciiLineBuffer _serialLines = new(maxLineLength: 64);
    private readonly ObservableCollection<string> _serialLogs = [];
    private CancellationTokenSource? _serialDeadline;

    private void InitializeUartDiagnostics()
    {
        SerialLogListBox.ItemsSource = _serialLogs;
        RefreshSerialPorts();
    }

    private void RefreshSerialPorts()
    {
        string? selected = SerialPortComboBox.SelectedItem as string;
        string[] ports = SerialPort.GetPortNames();
        Array.Sort(ports, StringComparer.OrdinalIgnoreCase);
        SerialPortComboBox.ItemsSource = ports;
        SerialPortComboBox.SelectedItem = selected is not null && ports.Contains(selected)
            ? selected : ports.FirstOrDefault();
    }

    private void SerialPorts_DropDownOpened(object? sender, EventArgs e) => RefreshSerialPorts();

    private void CloseUartDiagnostics()
    {
        var port = _diagnosticPort;
        _diagnosticPort = null;
        _serialDeadline?.Cancel();
        _serialDeadline?.Dispose();
        _serialDeadline = null;
        if (port is not null)
        {
            port.DataReceived -= DiagnosticPort_DataReceived;
            try { port.Dispose(); }
            catch (IOException exception) { LogSerial("ERROR " + exception.Message); }
        }
        _serialLines.Reset();
        SerialCheckButton.IsEnabled = SerialPortComboBox.IsEnabled = true;
    }

    private void LogSerial(string message)
    {
        _serialLogs.Insert(0, $"{DateTime.Now:HH:mm:ss.fff} {message}");
        while (_serialLogs.Count > 500) _serialLogs.RemoveAt(_serialLogs.Count - 1);
        SerialLogListBox.ScrollIntoView(_serialLogs[0]);
    }

    private async void SerialCheck_Click(object sender, RoutedEventArgs e)
    {
        if (_serialDeadline is not null) return;
        if (SerialPortComboBox.SelectedItem is not string name)
        {
            SerialStatusText.Text = "COM 포트를 선택하세요";
            RefreshSerialPorts();
            return;
        }
        try
        {
            if (_diagnosticPort?.PortName != name || !_diagnosticPort.IsOpen)
            {
                CloseUartDiagnostics();
                var port = new SerialPort(name, 115200, Parity.None, 8, StopBits.One)
                { Encoding = Encoding.ASCII, DtrEnable = false, RtsEnable = false, WriteTimeout = 250 };
                _diagnosticPort = port;
                port.DataReceived += DiagnosticPort_DataReceived;
                port.Open();
                LogSerial($"OPEN {name} · 115200 8N1");
            }
            _diagnosticPort.DiscardInBuffer();
            _serialLines.Reset();
            var deadline = new CancellationTokenSource();
            _serialDeadline = deadline;
            SerialCheckButton.IsEnabled = SerialPortComboBox.IsEnabled = false;
            SerialStatusText.Text = $"{name} · 응답 대기 중";
            LogSerial("TX PING<CR><LF>");
            _diagnosticPort.Write("PING\r\n");
            try { await Task.Delay(500, deadline.Token); }
            catch (OperationCanceledException) { return; }
            if (!ReferenceEquals(_serialDeadline, deadline)) return;
            CloseUartDiagnostics();
            SerialStatusText.Text = $"{name} · 응답 없음 (500 ms)";
            LogSerial("TIMEOUT 500 ms");
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidOperationException or ArgumentException or TimeoutException)
        {
            CloseUartDiagnostics();
            SerialStatusText.Text = "UART 연결·송수신 실패";
            LogSerial("ERROR " + exception.Message);
        }
    }

    private void DiagnosticPort_DataReceived(object sender, SerialDataReceivedEventArgs e)
    {
        try
        {
            string chunk = ((SerialPort)sender).ReadExisting();
            Dispatcher.InvokeAsync(() =>
            {
                if (!ReferenceEquals(sender, _diagnosticPort)) return;
                var result = _serialLines.Append(chunk);
                if (result.OverflowCount > 0) LogSerial("ERROR 수신 라인 길이 초과");
                foreach (string line in result.Lines)
                {
                    LogSerial("RX " + line);
                    if (_serialDeadline is null || line != "OK,PING,PONG") continue;
                    _serialDeadline.Cancel();
                    _serialDeadline.Dispose();
                    _serialDeadline = null;
                    SerialStatusText.Text = $"{_diagnosticPort!.PortName} · PONG 확인됨";
                    SerialCheckButton.IsEnabled = SerialPortComboBox.IsEnabled = true;
                }
            });
        }
        catch (Exception exception) when (exception is IOException or InvalidOperationException)
        {
            Dispatcher.InvokeAsync(() =>
            {
                if (!ReferenceEquals(sender, _diagnosticPort)) return;
                CloseUartDiagnostics();
                SerialStatusText.Text = "UART 수신 실패";
                LogSerial("ERROR " + exception.Message);
            });
        }
    }
}
