using System.ComponentModel;
using Spectator.Windows.Interop;

namespace Spectator.Windows.Windows;

internal sealed class WinEventHookSubscription : IDisposable
{
    private readonly NativeMethods.WinEventCallback _callback;
    private readonly List<IntPtr> _hooks = [];
    private bool _disposed;

    internal WinEventHookSubscription(
        NativeMethods.WinEventCallback callback,
        params (uint Minimum, uint Maximum)[] ranges)
    {
        _callback = callback;
        foreach ((uint minimum, uint maximum) in ranges)
        {
            IntPtr hook = NativeMethods.SetWinEventHook(
                minimum,
                maximum,
                IntPtr.Zero,
                _callback,
                0,
                0,
                NativeMethods.WineventOutOfContext | NativeMethods.WineventSkipOwnProcess);

            if (hook == IntPtr.Zero)
            {
                Dispose();
                throw new Win32Exception("SetWinEventHook failed.");
            }

            _hooks.Add(hook);
        }
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        foreach (IntPtr hook in _hooks)
        {
            _ = NativeMethods.UnhookWinEvent(hook);
        }

        _hooks.Clear();
        _disposed = true;
        GC.SuppressFinalize(this);
    }
}
