namespace Spectator.Core.Reactions;

public sealed record ReactionAnnotation(
    Guid Id,
    Guid DraftId,
    long VideoOffsetMilliseconds,
    ReactionKind Kind,
    string Content,
    DateTimeOffset RecordedAt);
