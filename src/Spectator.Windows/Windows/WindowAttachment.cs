using System.Windows;
using System.Windows.Threading;
using Spectator.Windows.Interop;

namespace Spectator.Windows.Windows;

internal sealed class WindowAttachment : IDisposable
{
    private const double Gap = 8;
    private readonly Window _toolWindow;
    private readonly IntPtr _targetHandle;
    private readonly WinEventHookSubscription _hooks;
    private bool _pendingPosition;
    private bool _disposed;

    internal WindowAttachment(Window toolWindow, IntPtr targetHandle)
    {
        _toolWindow = toolWindow;
        _targetHandle = targetHandle;
        _hooks = new WinEventHookSubscription(
            OnWinEvent,
            (NativeMethods.EventSystemMinimizeStart, NativeMethods.EventSystemMinimizeEnd),
            (NativeMethods.EventObjectDestroy, NativeMethods.EventObjectLocationChange));
        QueuePosition();
    }

    internal event EventHandler? TargetClosed;

    internal IntPtr TargetHandle => _targetHandle;

    internal void Reposition() => QueuePosition();

    private void OnWinEvent(
        IntPtr hook,
        uint eventType,
        IntPtr windowHandle,
        int objectId,
        int childId,
        uint eventThread,
        uint eventTime)
    {
        if (_disposed || windowHandle != _targetHandle)
        {
            return;
        }

        if (eventType == NativeMethods.EventObjectDestroy)
        {
            _toolWindow.Dispatcher.BeginInvoke(() => TargetClosed?.Invoke(this, EventArgs.Empty));
            return;
        }

        QueuePosition();
    }

    private void QueuePosition()
    {
        if (_disposed || _pendingPosition)
        {
            return;
        }

        _pendingPosition = true;
        _toolWindow.Dispatcher.BeginInvoke(DispatcherPriority.Background, () =>
        {
            _pendingPosition = false;
            PositionNow();
        });
    }

    private void PositionNow()
    {
        if (_disposed || !NativeMethods.IsWindow(_targetHandle))
        {
            return;
        }

        if (NativeMethods.IsIconic(_targetHandle))
        {
            _toolWindow.Hide();
            return;
        }

        if (!_toolWindow.IsVisible)
        {
            _toolWindow.Show();
        }

        NativeRect target = GetExtendedFrameBounds(_targetHandle);
        IntPtr monitor = NativeMethods.MonitorFromWindow(_targetHandle, NativeMethods.MonitorDefaultToNearest);
        var monitorInfo = new MonitorInfo { Size = System.Runtime.InteropServices.Marshal.SizeOf<MonitorInfo>() };
        if (!NativeMethods.GetMonitorInfo(monitor, ref monitorInfo))
        {
            return;
        }

        uint dpi = NativeMethods.GetDpiForWindow(_targetHandle);
        double scale = dpi == 0 ? 1d : dpi / 96d;
        double toolWidthPixels = _toolWindow.ActualWidth * scale;
        double toolHeightPixels = _toolWindow.ActualHeight * scale;
        double leftPixels = target.Right + Gap * scale;

        if (leftPixels + toolWidthPixels > monitorInfo.WorkArea.Right)
        {
            leftPixels = target.Left - toolWidthPixels - Gap * scale;
        }

        if (leftPixels < monitorInfo.WorkArea.Left)
        {
            leftPixels = Math.Max(monitorInfo.WorkArea.Left, target.Right - toolWidthPixels);
        }

        double topPixels = Math.Clamp(
            target.Top,
            monitorInfo.WorkArea.Top,
            Math.Max(monitorInfo.WorkArea.Top, monitorInfo.WorkArea.Bottom - toolHeightPixels));

        _toolWindow.Left = leftPixels / scale;
        _toolWindow.Top = topPixels / scale;
    }

    private static NativeRect GetExtendedFrameBounds(IntPtr handle)
    {
        int result = NativeMethods.DwmGetWindowAttribute(
            handle,
            NativeMethods.DwmwaExtendedFrameBounds,
            out NativeRect rectangle,
            System.Runtime.InteropServices.Marshal.SizeOf<NativeRect>());
        if (result == 0)
        {
            return rectangle;
        }

        if (!NativeMethods.GetWindowRect(handle, out rectangle))
        {
            throw new InvalidOperationException("The target window bounds could not be read.");
        }

        return rectangle;
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        _hooks.Dispose();
        GC.SuppressFinalize(this);
    }
}
