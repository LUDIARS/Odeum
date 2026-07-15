using Spectator.Core.Capture;
using Spectator.Core.Identifiers;
using Spectator.Core.Sessions;
using Spectator.Core.Time;
using Spectator.Windows.Persistence;
using Spectator.Windows.Volputas;

namespace Spectator.Windows.Sessions;

internal sealed class SessionController : IAsyncDisposable
{
    private readonly LocalSessionRepository _repository;
    private readonly VolputasApiClient? _api;
    private readonly ITimeSource _timeSource;
    private readonly IIdentifierSource _identifierSource;
    private readonly PlaySessionClock _clock;
    private readonly CancellationTokenSource _lifetime = new();
    private readonly SemaphoreSlim _checkpointGate = new(1, 1);
    private Task? _heartbeatTask;
    private LocalSession? _current;
    private readonly List<LocalSession> _recoveredSessions = [];
    private bool _isSuspended;
    private bool _disposed;

    internal SessionController(
        LocalSessionRepository repository,
        VolputasApiClient? api,
        ITimeSource timeSource,
        IIdentifierSource identifierSource)
    {
        _repository = repository;
        _api = api;
        _timeSource = timeSource;
        _identifierSource = identifierSource;
        _clock = new PlaySessionClock(timeSource);
    }

    internal bool IsRunning => _current is not null && _clock.IsRunning;

    internal LocalSession? Current => _current;

    internal async Task StartAsync(string gameId, bool isActive, CancellationToken cancellationToken)
    {
        if (IsRunning) await EndAsync(cancellationToken);
        DateTimeOffset now = _timeSource.GetUtcNow();
        string? remoteId = null;
        if (_api is { IsAuthenticated: true } api)
        {
            try
            {
                remoteId = await api.StartSessionAsync(gameId, cancellationToken);
            }
            catch (Exception exception) when (exception is HttpRequestException or VolputasApiException)
            {
            }
        }
        _current = new LocalSession(
            _identifierSource.NewIdentifier(),
            gameId,
            remoteId,
            now,
            null,
            now,
            0,
            0);
        _repository.Create(_current);
        _clock.Start(isActive);
        _heartbeatTask ??= RunHeartbeatAsync(_lifetime.Token);
    }

    internal void SetActive(bool isActive) => _clock.SetActive(isActive && !_isSuspended);

    internal void SetSuspended(bool isSuspended)
    {
        _isSuspended = isSuspended;
        if (isSuspended) _clock.SetActive(false);
    }

    internal PlaytimeSnapshot Snapshot() => _clock.Snapshot();

    internal async Task EndAsync(CancellationToken cancellationToken)
    {
        if (_current is null || !_clock.IsRunning) return;
        PlaytimeSnapshot snapshot = _clock.Stop();
        LocalSession ended = ToCheckpoint(_current, snapshot) with { EndedAt = snapshot.CapturedAt };
        _repository.Checkpoint(ended);
        _current = ended;
        if (ended.RemoteSessionId is not null && _api is { IsAuthenticated: true } api)
        {
            try
            {
                await api.HeartbeatAsync(
                    ended.RemoteSessionId,
                    ended.ElapsedMilliseconds,
                    ended.ActiveMilliseconds,
                    ended.LastCheckpointAt,
                    cancellationToken);
                await api.EndSessionAsync(ended.RemoteSessionId, cancellationToken);
            }
            catch (Exception exception) when (exception is HttpRequestException or VolputasApiException)
            {
            }
        }
    }

    internal async Task RecoverAsync(CancellationToken cancellationToken)
    {
        _recoveredSessions.AddRange(_repository.RecoverOpenSessions());
        if (_api is not { IsAuthenticated: true } api) return;
        foreach (LocalSession session in _recoveredSessions.Where(item => item.RemoteSessionId is not null).ToArray())
        {
            try
            {
                await api.HeartbeatAsync(
                    session.RemoteSessionId!,
                    session.ElapsedMilliseconds,
                    session.ActiveMilliseconds,
                    session.LastCheckpointAt,
                    cancellationToken);
                await api.EndSessionAsync(session.RemoteSessionId!, cancellationToken);
                _recoveredSessions.Remove(session);
            }
            catch (Exception exception) when (exception is HttpRequestException or VolputasApiException)
            {
            }
        }
    }

    private async Task RunHeartbeatAsync(CancellationToken cancellationToken)
    {
        using var timer = new PeriodicTimer(TimeSpan.FromSeconds(30));
        while (await timer.WaitForNextTickAsync(cancellationToken))
        {
            await CheckpointAsync(cancellationToken);
        }
    }

    private async Task CheckpointAsync(CancellationToken cancellationToken)
    {
        if (_current is null || !_clock.IsRunning || !await _checkpointGate.WaitAsync(0, cancellationToken)) return;
        try
        {
            LocalSession checkpoint = ToCheckpoint(_current, _clock.Snapshot());
            _repository.Checkpoint(checkpoint);
            _current = checkpoint;
            if (checkpoint.RemoteSessionId is not null && _api is { IsAuthenticated: true } api)
            {
                try
                {
                    await api.HeartbeatAsync(
                        checkpoint.RemoteSessionId,
                        checkpoint.ElapsedMilliseconds,
                        checkpoint.ActiveMilliseconds,
                        checkpoint.LastCheckpointAt,
                        cancellationToken);
                }
                catch (Exception exception) when (exception is HttpRequestException or VolputasApiException)
                {
                }
            }
        }
        finally
        {
            _checkpointGate.Release();
        }
    }

    private static LocalSession ToCheckpoint(LocalSession current, PlaytimeSnapshot snapshot) => current with
    {
        LastCheckpointAt = snapshot.CapturedAt,
        ElapsedMilliseconds = checked((long)snapshot.Elapsed.TotalMilliseconds),
        ActiveMilliseconds = checked((long)snapshot.Active.TotalMilliseconds),
    };

    public async ValueTask DisposeAsync()
    {
        if (_disposed) return;
        _disposed = true;
        if (IsRunning) await EndAsync(CancellationToken.None);
        _lifetime.Cancel();
        if (_heartbeatTask is not null)
        {
            try
            {
                await _heartbeatTask;
            }
            catch (OperationCanceledException)
            {
            }
        }
        _checkpointGate.Dispose();
        _lifetime.Dispose();
        GC.SuppressFinalize(this);
    }
}
