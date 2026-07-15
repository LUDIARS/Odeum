namespace Spectator.Core.Sessions;

public sealed record PlaytimeSnapshot(
    DateTimeOffset StartedAt,
    DateTimeOffset CapturedAt,
    TimeSpan Elapsed,
    TimeSpan Active);
