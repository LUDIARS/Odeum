using Spectator.Core.Sessions;

namespace Spectator.Core.Capture;

public sealed record CaptureAnchor(
    Guid Id,
    DateTimeOffset CapturedAt,
    PlaytimeSnapshot Playtime);
