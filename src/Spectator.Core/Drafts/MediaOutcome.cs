namespace Spectator.Core.Drafts;

public sealed record MediaOutcome(
    string Kind,
    bool IsAvailable,
    string? RelativePath,
    string? FailureReason);
