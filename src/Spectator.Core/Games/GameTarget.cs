namespace Spectator.Core.Games;

public sealed record GameTarget(
    string GameId,
    string DisplayName,
    string ExecutableName,
    string? TitlePattern,
    bool IsTopmost,
    DateTimeOffset UpdatedAt);
