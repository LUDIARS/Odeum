using Spectator.Core.Drafts;

namespace Spectator.Windows.Capture;

internal sealed record CaptureResult(StoredDraft Draft, IReadOnlyList<string> Warnings);
