namespace UltrasonicMonitor.Protocol;

public enum ProtocolMessageKind
{
    Unknown,
    Ready,
    State,
    CommandSucceeded,
    CommandError,
    Ultrasonic,
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
    string? Description = null);
