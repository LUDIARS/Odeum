namespace Spectator.Windows.Reactions;

internal sealed record ReactionRawDataDocument(
    string SchemaVersion,
    string SourceKind,
    string GameId,
    string SourceRef,
    ReactionRawDataSource Source,
    IReadOnlyList<ReactionRawDataEntry> Utterances);

internal sealed record ReactionRawDataSource(
    string DraftId,
    string CaptureAnchorId,
    string VideoSha256,
    string VideoMimeType,
    int VideoDurationMilliseconds,
    DateTimeOffset? ClipStartedAt,
    DateTimeOffset? ClipEndedAt);

internal sealed record ReactionRawDataEntry(
    string Id,
    long VideoOffsetMs,
    string ReactionKind,
    string Content,
    DateTimeOffset RecordedAt);
