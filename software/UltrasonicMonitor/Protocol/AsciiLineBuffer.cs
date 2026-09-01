using System.Text;

namespace UltrasonicMonitor.Protocol;

public sealed record LineBufferResult(IReadOnlyList<string> Lines, int OverflowCount);

public sealed class AsciiLineBuffer(int maxLineLength)
{
    private readonly object _syncRoot = new();
    private readonly StringBuilder _buffer = new();
    private bool _discardUntilLineEnd;
    private bool _previousDiscardedCharacterWasCarriageReturn;

    public bool HasPendingData
    {
        get
        {
            lock (_syncRoot)
            {
                return _buffer.Length > 0 || _discardUntilLineEnd;
            }
        }
    }

    public LineBufferResult Append(string chunk)
    {
        ArgumentNullException.ThrowIfNull(chunk);
        var lines = new List<string>();
        int overflowCount = 0;

        lock (_syncRoot)
        {
            foreach (char character in chunk)
            {
                if (_discardUntilLineEnd)
                {
                    if (_previousDiscardedCharacterWasCarriageReturn && character == '\n')
                    {
                        _discardUntilLineEnd = false;
                        _previousDiscardedCharacterWasCarriageReturn = false;
                    }
                    else
                    {
                        _previousDiscardedCharacterWasCarriageReturn = character == '\r';
                    }
                    continue;
                }

                _buffer.Append(character);
                if (_buffer.Length > maxLineLength)
                {
                    _buffer.Clear();
                    _discardUntilLineEnd = true;
                    _previousDiscardedCharacterWasCarriageReturn = character == '\r';
                    overflowCount++;
                    continue;
                }

                if (character == '\n' && _buffer.Length >= 2 && _buffer[^2] == '\r')
                {
                    lines.Add(_buffer.ToString(0, _buffer.Length - 2));
                    _buffer.Clear();
                }
            }
        }

        return new LineBufferResult(lines, overflowCount);
    }

    public void Reset()
    {
        lock (_syncRoot)
        {
            _buffer.Clear();
            _discardUntilLineEnd = false;
            _previousDiscardedCharacterWasCarriageReturn = false;
        }
    }
}
