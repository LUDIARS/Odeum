namespace Spectator.Core.Drafts;

public enum DraftState
{
    Ready,
    Queued,
    Uploading,
    Processing,
    Submitted,
    Rejected,
    Degraded,
    Deleted,
}
