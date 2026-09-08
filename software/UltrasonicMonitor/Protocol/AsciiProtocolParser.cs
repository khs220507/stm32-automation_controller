using System.Globalization;

namespace UltrasonicMonitor.Protocol;

public static class AsciiProtocolParser
{
    // 사용자가 현재 모듈의 연결 확인 기대값으로 합의한 값. 장치 주소와는 별개다.
    public const byte ExpectedMpu6050Identity = 0x72;
    private static readonly HashSet<string> States =
    [
        "INIT", "IDLE", "AUTO", "DIAG", "STOP", "FAULT",
    ];

    public static ProtocolMessage Parse(string line)
    {
        if (string.IsNullOrEmpty(line))
        {
            return Unknown(line, "빈 라인은 메시지로 처리하지 않습니다.");
        }

        if (line == "READY")
        {
            return new ProtocolMessage(ProtocolMessageKind.Ready, line, State: "IDLE");
        }

        if (States.Contains(line))
        {
            return new ProtocolMessage(ProtocolMessageKind.State, line, State: line);
        }

        ProtocolMessage? structuredMessage = ParseStructured(line);
        if (structuredMessage is not null)
        {
            return structuredMessage;
        }

        ProtocolMessage? legacyMessage = ParseLegacyUltrasonic(line);
        return legacyMessage ?? Unknown(line, "ICD 또는 임시 HC-SR04 출력 형식과 일치하지 않습니다.");
    }

    private static ProtocolMessage? ParseStructured(string line)
    {
        string[] fields = line.Split(',');

        if (fields[0] == "OK")
        {
            if (fields.Length == 3 && fields[1] == "CONFIG_ACCEL" && fields[2] == "2G")
                return new ProtocolMessage(ProtocolMessageKind.AccelConfigured, line, Command: fields[1]);

            if (fields.Length == 5 && fields[1] == "READ_ACCEL"
                && short.TryParse(fields[2], NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out short x)
                && short.TryParse(fields[3], NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out short y)
                && short.TryParse(fields[4], NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out short z))
                return new ProtocolMessage(ProtocolMessageKind.Accelerometer, line, Command: fields[1],
                    AccelX: x, AccelY: y, AccelZ: z);

            if (fields.Length == 4 && fields[1] == "WAKE_MPU6050"
                && byte.TryParse(fields[2], NumberStyles.None, CultureInfo.InvariantCulture, out byte before)
                && byte.TryParse(fields[3], NumberStyles.None, CultureInfo.InvariantCulture, out byte after))
                return new ProtocolMessage(ProtocolMessageKind.Mpu6050Wake, line,
                    Command: fields[1],
                    SensorStatus: (before & 0x80) == 0 && after == (before & ~0x40)
                        ? "OK" : "VERIFY_FAILED",
                    PowerBefore: before, PowerAfter: after);

            if (fields.Length == 3 && fields[1] == "PING" && fields[2] == "PONG")
                return new ProtocolMessage(ProtocolMessageKind.Uart, line, Command: "PING");

            if (fields.Length == 5 && fields[1] == "CHECK_HCSR04"
                && fields[2] is "OK" or "OUT_OF_RANGE" or "TIMEOUT"
                && TryParseUnsigned(fields[3], out uint distance)
                && TryParseUnsigned(fields[4], out uint pulse)
                && (fields[2] == "OK" || distance == 0U)
                && (fields[2] != "TIMEOUT" || pulse == 0U))
                return new ProtocolMessage(ProtocolMessageKind.Ultrasonic, line,
                    Command: "CHECK_HCSR04", SensorStatus: fields[2],
                    DistanceCentimeters: distance, PulseMicroseconds: pulse);

            if (fields.Length == 3 && fields[1] == "CHECK_MPU6050"
                && byte.TryParse(fields[2], NumberStyles.None, CultureInfo.InvariantCulture,
                    out byte identity))
            {
                // OK는 읽기 성공이다. 기대 식별값과 다르면 센서 확인 성공으로 표시하지 않는다.
                return new ProtocolMessage(ProtocolMessageKind.Mpu6050, line,
                    Command: fields[1], SensorStatus: identity == ExpectedMpu6050Identity ? "OK" : "ID_MISMATCH",
                    Identity: identity);
            }

            if (fields.Length == 4 && fields[1] == "GET_STATUS" && States.Contains(fields[2]))
            {
                return new ProtocolMessage(ProtocolMessageKind.CommandSucceeded, line,
                    Command: fields[1], State: fields[2], FaultCode: fields[3]);
            }

            if (fields.Length == 3 && (fields[1] == "START" || fields[1] == "STOP")
                && States.Contains(fields[2]))
            {
                return new ProtocolMessage(ProtocolMessageKind.CommandSucceeded, line,
                    Command: fields[1], State: fields[2]);
            }

            return Unknown(line, "OK 응답의 필드 수 또는 값이 올바르지 않습니다.");
        }

        if (fields[0] == "ERR")
        {
            return fields.Length == 3
                ? new ProtocolMessage(ProtocolMessageKind.CommandError, line,
                    Command: fields[1], ErrorCode: fields[2])
                : Unknown(line, "ERR 응답은 ERR,COMMAND,ERROR_CODE 형식이어야 합니다.");
        }

        if (fields[0] != "DATA")
        {
            return null;
        }

        if (fields.Length != 5 || fields[1] != "HCSR04")
        {
            return Unknown(line, "HC-SR04 데이터는 5개 필드여야 합니다.");
        }

        if (!TryParseUnsigned(fields[3], out uint distanceCentimeters)
            || !TryParseUnsigned(fields[4], out uint pulseMicroseconds))
        {
            return Unknown(line, "HC-SR04 거리와 펄스폭은 부호 없는 10진수여야 합니다.");
        }

        if (fields[2] is not ("OK" or "OUT_OF_RANGE" or "TIMEOUT"))
        {
            return Unknown(line, "정의되지 않은 HC-SR04 상태입니다.");
        }

        return new ProtocolMessage(ProtocolMessageKind.Ultrasonic, line,
            SensorStatus: fields[2], DistanceCentimeters: distanceCentimeters,
            PulseMicroseconds: pulseMicroseconds);
    }

    private static ProtocolMessage? ParseLegacyUltrasonic(string line)
    {
        string[] fields = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
        if (fields.Length < 2 || fields[0] != "HCSR04")
        {
            return null;
        }

        if (fields.Length == 2 && fields[1] == "TIMEOUT")
        {
            return new ProtocolMessage(ProtocolMessageKind.Ultrasonic, line,
                SensorStatus: "TIMEOUT", DistanceCentimeters: 0, PulseMicroseconds: 0);
        }

        if (fields.Length == 4 && fields[1] == "OK"
            && TryParseKeyValue(fields[2], "DIST_CM", out uint distanceCentimeters)
            && TryParseKeyValue(fields[3], "PULSE_US", out uint pulseMicroseconds))
        {
            return new ProtocolMessage(ProtocolMessageKind.Ultrasonic, line,
                SensorStatus: "OK", DistanceCentimeters: distanceCentimeters,
                PulseMicroseconds: pulseMicroseconds);
        }

        if (fields.Length == 3 && fields[1] == "OUT_OF_RANGE"
            && TryParseKeyValue(fields[2], "PULSE_US", out uint outOfRangePulse))
        {
            return new ProtocolMessage(ProtocolMessageKind.Ultrasonic, line,
                SensorStatus: "OUT_OF_RANGE", DistanceCentimeters: 0,
                PulseMicroseconds: outOfRangePulse);
        }

        return Unknown(line, "현재 펌웨어의 HC-SR04 임시 출력 형식과 일치하지 않습니다.");
    }

    private static bool TryParseKeyValue(string field, string key, out uint value)
    {
        string prefix = key + "=";
        value = 0;
        return field.StartsWith(prefix, StringComparison.Ordinal)
            && TryParseUnsigned(field[prefix.Length..], out value);
    }

    private static bool TryParseUnsigned(string text, out uint value) =>
        uint.TryParse(text, NumberStyles.None, CultureInfo.InvariantCulture, out value);

    private static ProtocolMessage Unknown(string line, string description) =>
        new(ProtocolMessageKind.Unknown, line, Description: description);
}
