using UltrasonicMonitor.Protocol;

namespace UltrasonicMonitor.Tests;

[TestClass]
public sealed class AsciiProtocolParserTests
{
    [TestMethod]
    public void ParsesDedicatedUartProbe()
    {
        ProtocolMessage message = AsciiProtocolParser.Parse("OK,PING,PONG");
        Assert.AreEqual(ProtocolMessageKind.Uart, message.Kind);
        Assert.AreEqual("PING", message.Command);
        Assert.IsNull(message.SensorStatus);
    }

    [DataRow("OK,CHECK_HCSR04,OK,25,1450", "OK", 25U, 1450U)]
    [DataRow("OK,CHECK_HCSR04,OUT_OF_RANGE,0,25000", "OUT_OF_RANGE", 0U, 25000U)]
    [DataRow("OK,CHECK_HCSR04,TIMEOUT,0,0", "TIMEOUT", 0U, 0U)]
    [TestMethod]
    public void ParsesIndividualUltrasonicResult(string line, string status, uint distance, uint pulse)
    {
        ProtocolMessage message = AsciiProtocolParser.Parse(line);
        Assert.AreEqual(ProtocolMessageKind.Ultrasonic, message.Kind);
        Assert.AreEqual("CHECK_HCSR04", message.Command);
        Assert.AreEqual(status, message.SensorStatus);
        Assert.AreEqual(distance, message.DistanceCentimeters);
        Assert.AreEqual(pulse, message.PulseMicroseconds);
    }

    [DataRow("OK,PING")]
    [DataRow("OK,PING,WRONG")]
    [DataRow("OK,PING,PONG,EXTRA")]
    [DataRow("OK,CHECK_HCSR04,INVALID,0,0")]
    [DataRow("OK,CHECK_HCSR04,TIMEOUT,25,1450")]
    [DataRow("OK,CHECK_HCSR04,TIMEOUT,0,1450")]
    [DataRow("OK,CHECK_HCSR04,OK,-25,1450")]
    [DataRow("OK,CHECK_HCSR04,OK,25")]
    [DataRow("OK,CHECK_HCSR04,OK,25,1450,EXTRA")]
    [TestMethod]
    public void RejectsMalformedDashboardResponses(string line) =>
        Assert.AreEqual(ProtocolMessageKind.Unknown, AsciiProtocolParser.Parse(line).Kind);

    [DataRow("OK,CHECK_MPU6050,104", "OK", (byte)104)]
    [DataRow("OK,CHECK_MPU6050,112", "ID_MISMATCH", (byte)112)]
    [DataRow("OK,CHECK_MPU6050,0", "ID_MISMATCH", (byte)0)]
    [DataRow("OK,CHECK_MPU6050,255", "ID_MISMATCH", (byte)255)]
    [TestMethod]
    public void ParsesActualIdentityAndSeparatesMismatch(string line, string status, byte identity)
    {
        ProtocolMessage message = AsciiProtocolParser.Parse(line);
        Assert.AreEqual(ProtocolMessageKind.Mpu6050, message.Kind);
        Assert.AreEqual("CHECK_MPU6050", message.Command);
        Assert.AreEqual(status, message.SensorStatus);
        Assert.AreEqual(identity, message.Identity);
    }

    [DataRow("OK,CHECK_MPU6050,256")]
    [DataRow("OK,CHECK_MPU6050,-1")]
    [DataRow("OK,CHECK_MPU6050,0x68")]
    [DataRow("OK,CHECK_MPU6050, 104")]
    [DataRow("OK,CHECK_MPU6050,+104")]
    [DataRow("OK,CHECK_MPU6050,")]
    [DataRow("OK,CHECK_MPU6050,104,EXTRA")]
    [DataRow("OK,CHECK_MPU6050")]
    [TestMethod]
    public void RejectsMalformedIdentity(string line) =>
        Assert.AreEqual(ProtocolMessageKind.Unknown, AsciiProtocolParser.Parse(line).Kind);

    [DataRow("NACK")]
    [DataRow("TIMEOUT")]
    [DataRow("BUS_BUSY")]
    [DataRow("BUS_ERROR")]
    [DataRow("ARBITRATION_LOST")]
    [DataRow("OVERRUN")]
    [DataRow("NOT_READY")]
    [DataRow("INTERNAL_ERROR")]
    [TestMethod]
    public void PreservesDiagnosticErrorReason(string reason)
    {
        ProtocolMessage message = AsciiProtocolParser.Parse($"ERR,CHECK_MPU6050,{reason}");
        Assert.AreEqual(ProtocolMessageKind.CommandError, message.Kind);
        Assert.AreEqual("CHECK_MPU6050", message.Command);
        Assert.AreEqual(reason, message.ErrorCode);
        Assert.IsNull(message.Identity);
    }

    [TestMethod]
    public void ReassemblesSplitDiagnosticResponseBesideUltrasonicData()
    {
        var buffer = new AsciiLineBuffer(64);
        Assert.IsEmpty(buffer.Append("OK,CHECK_MPU").Lines);
        LineBufferResult result = buffer.Append("6050,104\r\nHCSR04 TIMEOUT\r\n");
        Assert.HasCount(2, result.Lines);
        Assert.AreEqual(ProtocolMessageKind.Mpu6050, AsciiProtocolParser.Parse(result.Lines[0]).Kind);
        Assert.AreEqual(ProtocolMessageKind.Ultrasonic, AsciiProtocolParser.Parse(result.Lines[1]).Kind);
    }

    [TestMethod]
    public void ParsesStructuredUltrasonicMeasurement()
    {
        ProtocolMessage message = AsciiProtocolParser.Parse("DATA,HCSR04,OK,25,1450");
        Assert.AreEqual(ProtocolMessageKind.Ultrasonic, message.Kind);
        Assert.AreEqual("OK", message.SensorStatus);
        Assert.AreEqual(25U, message.DistanceCentimeters);
        Assert.AreEqual(1450U, message.PulseMicroseconds);
    }

    [TestMethod]
    public void ParsesCurrentFirmwareMeasurement()
    {
        ProtocolMessage message = AsciiProtocolParser.Parse("HCSR04 OK DIST_CM=25 PULSE_US=1450");
        Assert.AreEqual(ProtocolMessageKind.Ultrasonic, message.Kind);
        Assert.AreEqual("OK", message.SensorStatus);
        Assert.AreEqual(25U, message.DistanceCentimeters);
        Assert.AreEqual(1450U, message.PulseMicroseconds);
    }

    [DataRow("DATA,HCSR04,OUT_OF_RANGE,0,25000", "OUT_OF_RANGE", 25000U)]
    [DataRow("DATA,HCSR04,TIMEOUT,0,0", "TIMEOUT", 0U)]
    [DataRow("HCSR04 OUT_OF_RANGE PULSE_US=25000", "OUT_OF_RANGE", 25000U)]
    [DataRow("HCSR04 TIMEOUT", "TIMEOUT", 0U)]
    [TestMethod]
    public void ParsesUltrasonicErrorStates(string line, string expectedStatus, uint expectedPulse)
    {
        ProtocolMessage message = AsciiProtocolParser.Parse(line);
        Assert.AreEqual(ProtocolMessageKind.Ultrasonic, message.Kind);
        Assert.AreEqual(expectedStatus, message.SensorStatus);
        Assert.AreEqual(expectedPulse, message.PulseMicroseconds);
    }

    [DataRow("OK,START,AUTO", "START", "AUTO")]
    [DataRow("OK,STOP,STOP", "STOP", "STOP")]
    [TestMethod]
    public void ParsesCommandResponses(string line, string command, string state)
    {
        ProtocolMessage message = AsciiProtocolParser.Parse(line);
        Assert.AreEqual(ProtocolMessageKind.CommandSucceeded, message.Kind);
        Assert.AreEqual(command, message.Command);
        Assert.AreEqual(state, message.State);
    }

    [TestMethod]
    public void RejectsMalformedMeasurement()
    {
        ProtocolMessage message = AsciiProtocolParser.Parse("DATA,HCSR04,OK,twenty,1450");
        Assert.AreEqual(ProtocolMessageKind.Unknown, message.Kind);
    }
}
