using Spectator.Core.Drafts;
using Spectator.Core.Identifiers;
using Spectator.Core.Sessions;
using Spectator.Core.Time;
using Spectator.Windows.Capture;
using Spectator.Windows.Persistence;
using Spectator.Windows.Storage;

namespace Spectator.Windows.Importing;

internal sealed class LocalVideoImporter
{
    private readonly CaptureStore _store;
    private readonly DraftRepository _drafts;
    private readonly LocalSessionRepository _sessions;
    private readonly CaptureAssetFactory _assetFactory;
    private readonly IIdentifierSource _identifierSource;
    private readonly ITimeSource _timeSource;
    private readonly IMediaDurationSource _durationSource;

    internal LocalVideoImporter(
        CaptureStore store,
        DraftRepository drafts,
        LocalSessionRepository sessions,
        IIdentifierSource identifierSource,
        ITimeSource timeSource,
        IMediaDurationSource durationSource)
    {
        _store = store;
        _drafts = drafts;
        _sessions = sessions;
        _assetFactory = new CaptureAssetFactory(identifierSource, store);
        _identifierSource = identifierSource;
        _timeSource = timeSource;
        _durationSource = durationSource;
    }

    internal async Task<StoredDraft> ImportAsync(
        string sourcePath,
        string gameId,
        CancellationToken cancellationToken)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(sourcePath);
        ArgumentException.ThrowIfNullOrWhiteSpace(gameId);
        string normalizedGameId = gameId.Trim();
        if (normalizedGameId.Length is 0 or > 100)
        {
            throw new ArgumentException("The game identifier must contain 1 to 100 characters.", nameof(gameId));
        }
        string fullSourcePath = Path.GetFullPath(sourcePath);
        if (!File.Exists(fullSourcePath)) throw new FileNotFoundException("The local video does not exist.", fullSourcePath);
        string extension = Path.GetExtension(fullSourcePath).ToLowerInvariant();
        string mimeType = extension switch
        {
            ".mp4" => "video/mp4",
            ".mkv" => "video/x-matroska",
            ".webm" => "video/webm",
            _ => throw new InvalidDataException($"Unsupported local video container: {extension}"),
        };
        TimeSpan duration = _durationSource.GetDuration(fullSourcePath);
        if (duration <= TimeSpan.Zero || duration.TotalMilliseconds > int.MaxValue)
        {
            throw new InvalidDataException("The local video duration must be between 1 ms and 24.8 days.");
        }

        Guid sessionId = _identifierSource.NewIdentifier();
        Guid draftId = _identifierSource.NewIdentifier();
        DateTimeOffset now = _timeSource.GetUtcNow();
        string? destination = null;
        bool isSessionStored = false;
        try
        {
            destination = await _store.ImportMediaAsync(fullSourcePath, draftId, extension, cancellationToken);
            int durationMilliseconds = checked((int)Math.Round(duration.TotalMilliseconds));
            StoredAsset video = await _assetFactory.CreateAsync(
                draftId,
                "video",
                destination,
                mimeType,
                durationMilliseconds,
                null,
                null,
                null,
                cancellationToken);
            _sessions.Create(new LocalSession(sessionId, normalizedGameId, null, now, now, now, 0, 0));
            isSessionStored = true;
            var draft = new StoredDraft(
                draftId,
                sessionId,
                draftId.ToString("N"),
                $"import:{draftId:N}",
                string.Empty,
                now,
                0,
                0,
                DraftState.Ready,
                null,
                null,
                [video]);
            _drafts.Save(draft, now);
            return draft;
        }
        catch
        {
            if (isSessionStored) _sessions.Delete(sessionId);
            if (destination is not null && File.Exists(destination)) File.Delete(destination);
            throw;
        }
    }
}
