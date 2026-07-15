using Spectator.Core.Time;

namespace Spectator.Windows.Tests;

internal sealed class FixedTimeSource(DateTimeOffset value) : ITimeSource
{
    public DateTimeOffset GetUtcNow() => value;

    public long GetTimestamp() => 0;

    public TimeSpan GetElapsedTime(long startingTimestamp, long endingTimestamp) =>
        TimeSpan.FromTicks(endingTimestamp - startingTimestamp);
}
