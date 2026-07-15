using Spectator.Core.Time;

namespace Spectator.Core.Tests;

internal sealed class ManualTimeSource : ITimeSource
{
    private long _milliseconds;

    public ManualTimeSource(DateTimeOffset initialUtc)
    {
        InitialUtc = initialUtc;
    }

    public DateTimeOffset InitialUtc { get; }

    public DateTimeOffset GetUtcNow() => InitialUtc.AddMilliseconds(_milliseconds);

    public long GetTimestamp() => _milliseconds;

    public TimeSpan GetElapsedTime(long startingTimestamp, long endingTimestamp) =>
        TimeSpan.FromMilliseconds(endingTimestamp - startingTimestamp);

    public void Advance(TimeSpan duration)
    {
        _milliseconds += checked((long)duration.TotalMilliseconds);
    }
}
