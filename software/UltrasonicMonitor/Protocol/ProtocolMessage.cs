namespace UltrasonicMonitor.Protocol;

public enum ProtocolMessageKind
{
    Unknown,
    Ready,
    State,
    CommandSucceeded,
    CommandError,
    Ultrasonic,
    Mpu6050,
    Uart,
    Mpu6050Wake,
}

public sealed record ProtocolMessage(
    ProtocolMessageKind Kind,
    string Raw,
    string? Command = null,
    string? State = null,
    string? FaultCode = null,
    string? ErrorCode = null,
    string? SensorStatus = null,
    uint? DistanceCentimeters = null,
    uint? PulseMicroseconds = null,
    string? Description = null,
    byte? Identity = null,
    byte? PowerBefore = null,
    byte? PowerAfter = null);
