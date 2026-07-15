namespace Spectator.Core.Drafts;

public sealed record StoredAsset(
    Guid Id,
    Guid DraftId,
    string Kind,
    string RelativePath,
    string MimeType,
    long SizeBytes,
    string Sha256,
    int? DurationMilliseconds,
    DateTimeOffset? CapturedAt,
    DateTimeOffset? ClipStartedAt,
    DateTimeOffset? ClipEndedAt,
    AssetState State,
    bool IsEnabled);
