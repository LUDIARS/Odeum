using System.Diagnostics;

namespace Spectator.Core.Time;

public sealed class SystemTimeSource : ITimeSource
{
    public DateTimeOffset GetUtcNow() => DateTimeOffset.UtcNow;

    public long GetTimestamp() => Stopwatch.GetTimestamp();

    public TimeSpan GetElapsedTime(long startingTimestamp, long endingTimestamp) =>
        Stopwatch.GetElapsedTime(startingTimestamp, endingTimestamp);
}
