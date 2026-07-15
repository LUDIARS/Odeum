namespace Spectator.Windows.Capture;

internal sealed class WindowsMediaDurationSource : IMediaDurationSource
{
    public TimeSpan GetDuration(string path) => MediaDurationProbe.GetDuration(path);
}
