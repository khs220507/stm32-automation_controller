using System.Collections.ObjectModel;
using System.IO;
using System.IO.Ports;
using System.Net;
using System.Net.Sockets;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace UltrasonicMonitor;

public partial class MainWindow
{
    private readonly CancellationTokenSource _probeLifetime = new();
    private readonly ObservableCollection<string> _tcpTestLogs = [];
    private readonly ObservableCollection<string> _rs485TestLogs = [];
    private bool _tcpTestRunning;
    private string? _rs485TestPort;
    private string? _uartTestPort;
    private bool _testsClosed;

    private void InitializeCommunicationTests()
    {
        TcpTestLogListBox.ItemsSource = _tcpTestLogs;
        Rs485TestLogListBox.ItemsSource = _rs485TestLogs;
        RefreshTestPorts();
    }

    private void RefreshTestPorts()
    {
        string? selected = Rs485TestPortComboBox.SelectedItem as string;
        string[] ports = SerialPort.GetPortNames();
        Array.Sort(ports, StringComparer.OrdinalIgnoreCase);
        Rs485TestPortComboBox.ItemsSource = ports;
        Rs485TestPortComboBox.SelectedItem = selected is not null && ports.Contains(selected)
            ? selected : ports.LastOrDefault();
    }

    private void TestPorts_DropDownOpened(object? sender, EventArgs e)
    {
        RefreshTestPorts();
    }

    private async void TcpTest_Click(object sender, RoutedEventArgs e)
    {
        if (_tcpTestRunning)
        {
            return;
        }
        bool validAddress = IPAddress.TryParse(TcpTestHostTextBox.Text.Trim(), out var address);
        bool validPort = int.TryParse(TcpTestPortTextBox.Text, out int port);
        if (validAddress == false || address!.AddressFamily != AddressFamily.InterNetwork
            || validPort == false || port < 1 || port > 65535)
        {
            TcpTestStatusText.Text = "IPv4 주소와 포트(1~65535)를 확인하세요";
            return;
        }
        if (_tcpClient is not null)
        {
            TcpTestStatusText.Text = "센서 TCP 연결을 해제한 뒤 시험하세요";
            return;
        }
        _tcpTestRunning = true;
        TcpTestHostTextBox.IsEnabled = false;
        TcpTestPortTextBox.IsEnabled = false;
        try
        {
            await RunProbeAsync("TCP", $"{address}:{port}", TcpTestButton,
                TcpTestStatusText, TcpTestTimeText, _tcpTestLogs, TcpTestLogListBox,
                (log, token) => PingProbe.TcpAsync(address, port, log, token));
        }
        finally
        {
            _tcpTestRunning = false;
            TcpTestHostTextBox.IsEnabled = true;
            TcpTestPortTextBox.IsEnabled = true;
        }
    }

    private async void Rs485Test_Click(object sender, RoutedEventArgs e)
    {
        if (_rs485TestPort is not null)
        {
            return;
        }
        if (Rs485TestPortComboBox.SelectedItem is not string name)
        {
            Rs485TestStatusText.Text = "USB-RS485 COM 포트를 선택하세요";
            return;
        }
        if (_uartTestPort == name || _rs485Port?.PortName == name)
        {
            Rs485TestStatusText.Text = "다른 시험 또는 센서 연결에서 사용 중인 포트입니다";
            return;
        }
        _rs485TestPort = name;
        Rs485TestPortComboBox.IsEnabled = false;
        try
        {
            await RunProbeAsync("RS-485", name, Rs485TestButton,
                Rs485TestStatusText, Rs485TestTimeText, _rs485TestLogs, Rs485TestLogListBox,
                (log, token) => PingProbe.SerialAsync(name, log, token));
        }
        finally
        {
            _rs485TestPort = null;
            Rs485TestPortComboBox.IsEnabled = true;
        }
    }

    private async Task RunProbeAsync(string transport, string endpoint, Button button,
        TextBlock status, TextBlock timestamp, ObservableCollection<string> entries, ListBox list,
        Func<Action<string>, CancellationToken, Task> probe)
    {
        button.IsEnabled = false;
        status.Text = $"{transport} · {endpoint} 시험 중…";
        status.Foreground = Brushes.SlateGray;
        timestamp.Text = "응답 대기 중";
        void Log(string text)
        {
            string line = $"{DateTime.Now:HH:mm:ss.fff} [{transport} {endpoint}] {text}";
            Dispatcher.InvokeAsync(() =>
            {
                if (_testsClosed)
                {
                    return;
                }
                entries.Insert(0, line);
                while (entries.Count > 500)
                {
                    entries.RemoveAt(entries.Count - 1);
                }
                list.ScrollIntoView(entries[0]);
            });
        }
        Log("시험 시작");
        try
        {
            await probe(Log, _probeLifetime.Token);
            if (_testsClosed)
            {
                return;
            }
            status.Text = $"{transport} · {endpoint} PONG 확인됨";
            status.Foreground = Brushes.SeaGreen;
            Log("성공 · 연결 해제됨");
        }
        catch (Exception exception) when (exception is IOException or SocketException
            or UnauthorizedAccessException or InvalidOperationException or ArgumentException
            or TimeoutException or OperationCanceledException)
        {
            if (_testsClosed)
            {
                return;
            }
            string reason = exception is OperationCanceledException
                ? "연결 또는 PONG 응답 시간 초과" : exception.Message;
            status.Text = $"{transport} · {endpoint} 실패: {reason}";
            status.Foreground = Brushes.Firebrick;
            Log("실패 · " + reason);
        }
        finally
        {
            if (_testsClosed == false)
            {
                timestamp.Text = $"마지막 시험: {DateTime.Now:HH:mm:ss.fff} · {endpoint}";
                button.IsEnabled = true;
            }
        }
    }

    private void CloseCommunicationTests()
    {
        _testsClosed = true;
        _probeLifetime.Cancel();
    }
}
