namespace Spectator.Windows.Sync;

internal sealed class SyncStatusChangedEventArgs : EventArgs
{
    internal SyncStatusChangedEventArgs(Guid draftId, string status)
    {
        DraftId = draftId;
        Status = status;
    }

    internal Guid DraftId { get; }

    internal string Status { get; }
}
