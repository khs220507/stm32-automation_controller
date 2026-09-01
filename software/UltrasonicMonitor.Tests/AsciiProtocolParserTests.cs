using UltrasonicMonitor.Protocol;

namespace UltrasonicMonitor.Tests;

[TestClass]
public sealed class AsciiProtocolParserTests
{
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
