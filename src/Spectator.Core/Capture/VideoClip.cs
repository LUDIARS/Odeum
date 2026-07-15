namespace Spectator.Core.Capture;

public sealed record VideoClip(
    string FilePath,
    DateTimeOffset ClipStartedAt,
    DateTimeOffset ClipEndedAt,
    DateTimeOffset SavedAt,
    TimeSpan Duration);
