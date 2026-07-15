using Spectator.Core.Capture;
using Spectator.Core.Drafts;
using Spectator.Core.Identifiers;
using Spectator.Core.Sessions;
using Spectator.Core.Time;
using Spectator.Windows.Configuration;
using Spectator.Windows.Persistence;
using Spectator.Windows.Sessions;
using Spectator.Windows.Storage;

namespace Spectator.Windows.Capture;

internal sealed class CaptureCoordinator
{
    private readonly WindowScreenshotCapture _screenshot;
    private readonly CaptureStore _store;
    private readonly DraftRepository _drafts;
    private readonly SessionController _sessions;
    private readonly CaptureAnchorFactory _anchors;
    private readonly CaptureAssetFactory _assetFactory;
    private readonly IIdentifierSource _identifierSource;
    private readonly ITimeSource _timeSource;
    private readonly AppSettings _settings;

    internal CaptureCoordinator(
        WindowScreenshotCapture screenshot,
        CaptureStore store,
        DraftRepository drafts,
        SessionController sessions,
        IIdentifierSource identifierSource,
        ITimeSource timeSource,
        AppSettings settings)
    {
        _screenshot = screenshot;
        _store = store;
        _drafts = drafts;
        _sessions = sessions;
        _identifierSource = identifierSource;
        _timeSource = timeSource;
        _settings = settings;
        _anchors = new CaptureAnchorFactory(identifierSource);
        _assetFactory = new CaptureAssetFactory(identifierSource, store);
    }

    internal async Task<CaptureResult> CaptureAsync(
        IntPtr targetHandle,
        IVideoCaptureBackend? videoBackend,
        CancellationToken cancellationToken)
    {
        LocalSession localSession = _sessions.Current
            ?? throw new InvalidOperationException("A play session is not running.");
        PlaytimeSnapshot playtime = _sessions.Snapshot();
        CaptureAnchor anchor = _anchors.Create(playtime);
        Guid draftId = _identifierSource.NewIdentifier();
        var warnings = new List<string>();
        var assets = new List<StoredAsset>();
        string imageExtension = _settings.ScreenshotFormat == "jpeg" ? ".jpg" : ".png";
        string imageMime = _settings.ScreenshotFormat == "jpeg" ? "image/jpeg" : "image/png";
        string imagePath = _store.GetMediaPath(draftId, imageExtension);

        Task<StoredAsset?> screenshotTask = CaptureScreenshotAsync(
            targetHandle,
            draftId,
            imagePath,
            imageMime,
            anchor,
            warnings,
            cancellationToken);
        Task<StoredAsset?> videoTask = CaptureVideoAsync(
            videoBackend,
            draftId,
            anchor,
            warnings,
            cancellationToken);
        StoredAsset?[] outcomes = await Task.WhenAll(screenshotTask, videoTask);
        assets.AddRange(outcomes.OfType<StoredAsset>());
        DraftState state = warnings.Count == 0 ? DraftState.Ready : DraftState.Degraded;
        var draft = new StoredDraft(
            draftId,
            localSession.Id,
            draftId.ToString("N"),
            anchor.Id.ToString("N"),
            string.Empty,
            anchor.CapturedAt,
            checked((long)anchor.Playtime.Elapsed.TotalMilliseconds),
            checked((long)anchor.Playtime.Active.TotalMilliseconds),
            state,
            null,
            warnings.Count == 0 ? null : string.Join(" | ", warnings),
            assets);
        _drafts.Save(draft, _timeSource.GetUtcNow());
        return new CaptureResult(draft, warnings);
    }

    private async Task<StoredAsset?> CaptureScreenshotAsync(
        IntPtr targetHandle,
        Guid draftId,
        string imagePath,
        string imageMime,
        CaptureAnchor anchor,
        List<string> warnings,
        CancellationToken cancellationToken)
    {
        try
        {
            await _screenshot.SaveAsync(targetHandle, imagePath, _settings.ScreenshotFormat, cancellationToken);
            return await _assetFactory.CreateAsync(
                draftId,
                "screenshot",
                imagePath,
                imageMime,
                null,
                _timeSource.GetUtcNow(),
                null,
                null,
                cancellationToken);
        }
        catch (Exception exception)
        {
            lock (warnings) warnings.Add($"Screenshot: {exception.Message}");
            return null;
        }
    }

    private async Task<StoredAsset?> CaptureVideoAsync(
        IVideoCaptureBackend? backend,
        Guid draftId,
        CaptureAnchor anchor,
        List<string> warnings,
        CancellationToken cancellationToken)
    {
        if (backend is null)
        {
            lock (warnings) warnings.Add("Video: no capture backend is ready.");
            return null;
        }
        try
        {
            VideoClip clip = await backend.SaveReplayAsync(anchor, cancellationToken);
            string extension = Path.GetExtension(clip.FilePath).ToLowerInvariant();
            string mimeType = extension switch
            {
                ".mp4" => "video/mp4",
                ".mkv" => "video/x-matroska",
                ".webm" => "video/webm",
                _ => throw new InvalidDataException($"Unsupported replay container: {extension}"),
            };
            string destination = _store.GetMediaPath(draftId, extension);
            await CopyAtomicAsync(clip.FilePath, destination, cancellationToken);
            return await _assetFactory.CreateAsync(
                draftId,
                "video",
                destination,
                mimeType,
                checked((int)clip.Duration.TotalMilliseconds),
                null,
                clip.ClipStartedAt,
                clip.ClipEndedAt,
                cancellationToken);
        }
        catch (Exception exception)
        {
            lock (warnings) warnings.Add($"Video: {exception.Message}");
            return null;
        }
    }

    private static async Task CopyAtomicAsync(string source, string destination, CancellationToken cancellationToken)
    {
        string temporary = destination + ".tmp";
        try
        {
            await using (var input = new FileStream(source, FileMode.Open, FileAccess.Read, FileShare.Read, 1024 * 1024, true))
            await using (var output = new FileStream(temporary, FileMode.Create, FileAccess.Write, FileShare.None, 1024 * 1024, true))
            {
                await input.CopyToAsync(output, cancellationToken);
                await output.FlushAsync(cancellationToken);
            }
            File.Move(temporary, destination, true);
        }
        finally
        {
            if (File.Exists(temporary)) File.Delete(temporary);
        }
    }
}
