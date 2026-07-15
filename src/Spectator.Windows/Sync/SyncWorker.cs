using Spectator.Core.Drafts;
using Spectator.Core.Sessions;
using Spectator.Core.Time;
using Spectator.Core.Randomness;
using Spectator.Windows.Persistence;
using Spectator.Windows.Storage;
using Spectator.Windows.Volputas;

namespace Spectator.Windows.Sync;

internal sealed class SyncWorker : IAsyncDisposable
{
    private readonly DraftRepository _drafts;
    private readonly LocalSessionRepository _sessions;
    private readonly VolputasApiClient _api;
    private readonly CaptureStore _captureStore;
    private readonly ITimeSource _timeSource;
    private readonly IRandomSource _randomSource;
    private readonly CancellationTokenSource _lifetime = new();
    private readonly SemaphoreSlim _wakeSignal = new(0, 1);
    private readonly Task _runTask;
    private bool _disposed;

    internal SyncWorker(
        DraftRepository drafts,
        LocalSessionRepository sessions,
        VolputasApiClient api,
        CaptureStore captureStore,
        ITimeSource timeSource,
        IRandomSource randomSource)
    {
        _drafts = drafts;
        _sessions = sessions;
        _api = api;
        _captureStore = captureStore;
        _timeSource = timeSource;
        _randomSource = randomSource;
        _runTask = RunAsync(_lifetime.Token);
    }

    internal event EventHandler<SyncStatusChangedEventArgs>? StatusChanged;

    internal void Wake()
    {
        if (!_disposed && _wakeSignal.CurrentCount == 0)
        {
            _wakeSignal.Release();
        }
    }

    private async Task RunAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            if (_api.IsAuthenticated)
            {
                foreach (StoredDraft draft in _drafts.ListDue(_timeSource.GetUtcNow(), 10))
                {
                    await ProcessAsync(draft, cancellationToken);
                }
            }

            _ = await _wakeSignal.WaitAsync(TimeSpan.FromSeconds(5), cancellationToken);
        }
    }

    private async Task ProcessAsync(StoredDraft draft, CancellationToken cancellationToken)
    {
        try
        {
            LocalSession session = _sessions.Get(draft.LocalSessionId)
                ?? throw new InvalidOperationException("The draft's local session no longer exists.");
            string remoteSessionId = session.RemoteSessionId
                ?? await StartRecoveredSessionAsync(session, cancellationToken);
            RemoteImpression remote;
            if (draft.RemoteImpressionId is null)
            {
                remote = await _api.CreateImpressionAsync(remoteSessionId, draft, cancellationToken);
                _drafts.MarkUploading(draft.Id, remote.Id, _timeSource.GetUtcNow());
                await _api.UploadAssetsAsync(remote, draft, _captureStore.RootDirectory, cancellationToken);
                remote = await _api.CompleteImpressionAsync(remote.Id, cancellationToken);
                _drafts.MarkProcessing(draft.Id, _timeSource.GetUtcNow());
            }
            else
            {
                remote = await _api.GetImpressionAsync(draft.RemoteImpressionId, cancellationToken);
            }

            if (remote.Status == "ready")
            {
                _drafts.MarkSubmitted(draft.Id, _timeSource.GetUtcNow());
                StatusChanged?.Invoke(this, new SyncStatusChangedEventArgs(draft.Id, "submitted"));
            }
            else if (remote.Status == "rejected")
            {
                _drafts.MarkRejected(draft.Id, remote.RejectionReason ?? "Volputas rejected media processing.", _timeSource.GetUtcNow());
                StatusChanged?.Invoke(this, new SyncStatusChangedEventArgs(draft.Id, "rejected"));
            }
            else
            {
                ScheduleRetry(draft, "Volputas is still processing the impression.", TimeSpan.FromSeconds(10));
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
        }
        catch (VolputasApiException exception) when (exception.IsPermanent)
        {
            _drafts.MarkRejected(draft.Id, exception.Message, _timeSource.GetUtcNow());
            StatusChanged?.Invoke(this, new SyncStatusChangedEventArgs(draft.Id, "rejected"));
        }
        catch (Exception exception)
        {
            ScheduleRetry(draft, exception.Message, null);
        }
    }

    private async Task<string> StartRecoveredSessionAsync(LocalSession session, CancellationToken cancellationToken)
    {
        string remoteId = await _api.StartSessionAsync(session.GameId, cancellationToken);
        _sessions.Checkpoint(session with { RemoteSessionId = remoteId });
        if (session.EndedAt is not null)
        {
            await _api.HeartbeatAsync(
                remoteId,
                session.ElapsedMilliseconds,
                session.ActiveMilliseconds,
                session.LastCheckpointAt,
                cancellationToken);
            await _api.EndSessionAsync(remoteId, cancellationToken);
        }
        return remoteId;
    }

    private void ScheduleRetry(StoredDraft draft, string error, TimeSpan? fixedDelay)
    {
        int attempts = _drafts.GetAttemptCount(draft.Id) + 1;
        double exponentialSeconds = Math.Min(900, Math.Pow(2, Math.Min(attempts, 9)));
        double jitter = _randomSource.NextUnit() * Math.Min(30, exponentialSeconds * 0.25);
        TimeSpan delay = fixedDelay ?? TimeSpan.FromSeconds(exponentialSeconds + jitter);
        DateTimeOffset now = _timeSource.GetUtcNow();
        _drafts.ScheduleRetry(draft.Id, attempts, now + delay, error, now);
        StatusChanged?.Invoke(this, new SyncStatusChangedEventArgs(draft.Id, "queued"));
    }

    public async ValueTask DisposeAsync()
    {
        if (_disposed) return;
        _disposed = true;
        _lifetime.Cancel();
        try
        {
            await _runTask;
        }
        catch (OperationCanceledException)
        {
        }
        _wakeSignal.Dispose();
        _lifetime.Dispose();
        GC.SuppressFinalize(this);
    }
}
