namespace Spectator.Core.Capture;

public interface IVideoCaptureBackend : IAsyncDisposable
{
    string Name { get; }

    Task<VideoBackendStatus> GetStatusAsync(CancellationToken cancellationToken);

    Task<VideoClip> SaveReplayAsync(CaptureAnchor anchor, CancellationToken cancellationToken);
}
