using Spectator.Core.Capture;

namespace Spectator.Core.Drafts;

public sealed record CaptureDraft(
    CaptureAnchor Anchor,
    string GameProcess,
    string Comment,
    IReadOnlyList<MediaOutcome> Media,
    DateTimeOffset CreatedAt);
