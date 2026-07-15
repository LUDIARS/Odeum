namespace Spectator.Windows.Windows;

public sealed record WindowDescriptor(
    IntPtr Handle,
    uint ProcessId,
    string ProcessName,
    string Title)
{
    public override string ToString() => $"{Title} — {ProcessName} ({ProcessId})";
}
