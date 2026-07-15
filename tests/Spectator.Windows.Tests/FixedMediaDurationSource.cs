using Spectator.Windows.Capture;

namespace Spectator.Windows.Tests;

internal sealed class FixedMediaDurationSource(TimeSpan duration) : IMediaDurationSource
{
    public TimeSpan GetDuration(string path) => duration;
}
