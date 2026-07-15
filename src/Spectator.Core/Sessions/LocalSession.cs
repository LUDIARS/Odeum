namespace Spectator.Core.Sessions;

public sealed record LocalSession(
    Guid Id,
    string GameId,
    string? RemoteSessionId,
    DateTimeOffset StartedAt,
    DateTimeOffset? EndedAt,
    DateTimeOffset LastCheckpointAt,
    long ElapsedMilliseconds,
    long ActiveMilliseconds);
