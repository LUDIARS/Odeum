using Spectator.Core.Time;

namespace Spectator.Core.Sessions;

public sealed class PlaySessionClock
{
    private readonly object _gate = new();
    private readonly ITimeSource _timeSource;
    private bool _isRunning;
    private bool _isActive;
    private DateTimeOffset _startedAt;
    private long _startedTimestamp;
    private long _activeStartedTimestamp;
    private TimeSpan _completedActive = TimeSpan.Zero;

    public PlaySessionClock(ITimeSource timeSource)
    {
        _timeSource = timeSource ?? throw new ArgumentNullException(nameof(timeSource));
    }

    public bool IsRunning
    {
        get
        {
            lock (_gate)
            {
                return _isRunning;
            }
        }
    }

    public void Start(bool isActive)
    {
        lock (_gate)
        {
            var nowTimestamp = _timeSource.GetTimestamp();
            _startedAt = _timeSource.GetUtcNow();
            _startedTimestamp = nowTimestamp;
            _activeStartedTimestamp = nowTimestamp;
            _completedActive = TimeSpan.Zero;
            _isRunning = true;
            _isActive = isActive;
        }
    }

    public void SetActive(bool isActive)
    {
        lock (_gate)
        {
            if (!_isRunning || _isActive == isActive)
            {
                return;
            }

            var nowTimestamp = _timeSource.GetTimestamp();
            if (_isActive)
            {
                _completedActive += _timeSource.GetElapsedTime(_activeStartedTimestamp, nowTimestamp);
            }
            else
            {
                _activeStartedTimestamp = nowTimestamp;
            }

            _isActive = isActive;
        }
    }

    public PlaytimeSnapshot Snapshot()
    {
        lock (_gate)
        {
            if (!_isRunning)
            {
                throw new InvalidOperationException("A play session has not been started.");
            }

            var nowTimestamp = _timeSource.GetTimestamp();
            var active = _completedActive;
            if (_isActive)
            {
                active += _timeSource.GetElapsedTime(_activeStartedTimestamp, nowTimestamp);
            }

            var elapsed = _timeSource.GetElapsedTime(_startedTimestamp, nowTimestamp);
            if (active > elapsed)
            {
                active = elapsed;
            }

            return new PlaytimeSnapshot(_startedAt, _timeSource.GetUtcNow(), elapsed, active);
        }
    }

    public PlaytimeSnapshot Stop()
    {
        lock (_gate)
        {
            var snapshot = Snapshot();
            _isRunning = false;
            _isActive = false;
            return snapshot;
        }
    }
}
