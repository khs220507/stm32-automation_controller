using UltrasonicMonitor.Protocol;

namespace UltrasonicMonitor.Tests;

[TestClass]
public sealed class AsciiLineBufferTests
{
    [TestMethod]
    public void ReassemblesSplitCrlfLine()
    {
        var buffer = new AsciiLineBuffer(64);
        LineBufferResult first = buffer.Append("DATA,HCSR04,OK,25,");
        LineBufferResult second = buffer.Append("1450\r\n");
        Assert.IsEmpty(first.Lines);
        CollectionAssert.AreEqual(new[] { "DATA,HCSR04,OK,25,1450" }, second.Lines.ToArray());
        Assert.IsFalse(buffer.HasPendingData);
    }

    [TestMethod]
    public void ReturnsMultipleLinesFromSingleChunk()
    {
        var buffer = new AsciiLineBuffer(64);
        LineBufferResult result = buffer.Append("AUTO\r\nHCSR04 TIMEOUT\r\n");
        CollectionAssert.AreEqual(new[] { "AUTO", "HCSR04 TIMEOUT" }, result.Lines.ToArray());
    }

    [TestMethod]
    public void DiscardsOversizedLineAndRecoversAtNextCrlf()
    {
        var buffer = new AsciiLineBuffer(8);
        LineBufferResult overflow = buffer.Append("123456789\r\n");
        LineBufferResult recovered = buffer.Append("STOP\r\n");
        Assert.AreEqual(1, overflow.OverflowCount);
        CollectionAssert.AreEqual(new[] { "STOP" }, recovered.Lines.ToArray());
    }
}
