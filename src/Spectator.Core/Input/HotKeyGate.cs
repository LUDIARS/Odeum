using Spectator.Core.Time;

namespace Spectator.Core.Input;

public sealed class HotKeyGate
{
    private readonly ITimeSource _timeSource;
    private readonly TimeSpan _minimumInterval;
    private long? _lastAcceptedTimestamp;

    public HotKeyGate(ITimeSource timeSource, TimeSpan minimumInterval)
    {
        _timeSource = timeSource;
        _minimumInterval = minimumInterval;
    }

    public bool TryAccept()
    {
        long now = _timeSource.GetTimestamp();
        if (_lastAcceptedTimestamp is not null
            && _timeSource.GetElapsedTime(_lastAcceptedTimestamp.Value, now) < _minimumInterval)
        {
            return false;
        }
        _lastAcceptedTimestamp = now;
        return true;
    }
}
