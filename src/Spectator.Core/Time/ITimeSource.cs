namespace Spectator.Core.Time;

public interface ITimeSource
{
    DateTimeOffset GetUtcNow();

    long GetTimestamp();

    TimeSpan GetElapsedTime(long startingTimestamp, long endingTimestamp);
}
