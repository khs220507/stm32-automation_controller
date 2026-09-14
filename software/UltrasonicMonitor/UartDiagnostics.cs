using System.Collections.ObjectModel;
using System.IO.Ports;
using System.Windows;

namespace UltrasonicMonitor;

public partial class MainWindow
{
    private readonly ObservableCollection<string> _serialLogs = [];

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

    private void SerialPorts_DropDownOpened(object? sender, EventArgs e)
    {
        RefreshSerialPorts();
    }

    private async void SerialCheck_Click(object sender, RoutedEventArgs e)
    {
        if (_uartTestPort is not null)
        {
            return;
        }
        if (SerialPortComboBox.SelectedItem is not string name)
        {
            SerialStatusText.Text = "UART COM 포트를 선택하세요";
            return;
        }
        if (_rs485TestPort == name || _rs485Port?.PortName == name)
        {
            SerialStatusText.Text = "RS-485 시험 또는 센서 연결에서 사용 중인 포트입니다";
            return;
        }
        _uartTestPort = name;
        SerialPortComboBox.IsEnabled = false;
        try
        {
            await RunProbeAsync("UART", name, SerialCheckButton,
                SerialStatusText, SerialTimeText, _serialLogs, SerialLogListBox,
                (log, token) => PingProbe.SerialAsync(name, log, token));
        }
        finally
        {
            _uartTestPort = null;
            SerialPortComboBox.IsEnabled = true;
        }
    }
}