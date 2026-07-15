using System.ComponentModel;
using System.Windows.Interop;
using Spectator.Windows.Interop;

namespace Spectator.Windows.Input;

internal sealed class GlobalHotKeyRegistration : IDisposable
{
    private const int HotKeyId = 0x5350;
    private readonly HwndSource _source;
    private readonly HwndSourceHook _hook;
    private bool _disposed;

    internal GlobalHotKeyRegistration(IntPtr windowHandle, uint modifiers, uint virtualKey)
    {
        _source = HwndSource.FromHwnd(windowHandle)
            ?? throw new InvalidOperationException("The WPF window source is unavailable.");
        _hook = HandleMessage;
        _source.AddHook(_hook);

        if (!NativeMethods.RegisterHotKey(windowHandle, HotKeyId, modifiers, virtualKey))
        {
            _source.RemoveHook(_hook);
            throw new Win32Exception("The capture hotkey could not be registered.");
        }
    }

    internal event EventHandler? Pressed;

    private IntPtr HandleMessage(IntPtr hwnd, int message, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if (message == NativeMethods.WmHotkey && wParam.ToInt32() == HotKeyId)
        {
            handled = true;
            Pressed?.Invoke(this, EventArgs.Empty);
        }

        return IntPtr.Zero;
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _ = NativeMethods.UnregisterHotKey(_source.Handle, HotKeyId);
        _source.RemoveHook(_hook);
        _disposed = true;
        GC.SuppressFinalize(this);
    }
}
