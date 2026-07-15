namespace Spectator.Windows.Capture;

internal interface IMediaDurationSource
{
    TimeSpan GetDuration(string path);
}
