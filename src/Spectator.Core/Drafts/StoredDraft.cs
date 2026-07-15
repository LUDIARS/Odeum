namespace Spectator.Core.Drafts;

public sealed record StoredDraft(
    Guid Id,
    Guid LocalSessionId,
    string ClientSubmissionId,
    string CaptureAnchorId,
    string Text,
    DateTimeOffset CapturedAt,
    long ElapsedMilliseconds,
    long ActiveMilliseconds,
    DraftState State,
    string? RemoteImpressionId,
    string? LastError,
    IReadOnlyList<StoredAsset> Assets);
