using System.Runtime.InteropServices;
using System.Text;

namespace Spectator.Windows.Interop;

internal static class NativeMethods
{
    internal const uint EventSystemForeground = 0x0003;
    internal const uint EventSystemMinimizeStart = 0x0016;
    internal const uint EventSystemMinimizeEnd = 0x0017;
    internal const uint EventObjectDestroy = 0x8001;
    internal const uint EventObjectLocationChange = 0x800B;
    internal const uint WineventOutOfContext = 0x0000;
    internal const uint WineventSkipOwnProcess = 0x0002;
    internal const int DwmwaExtendedFrameBounds = 9;
    internal const int DwmwaCloaked = 14;
    internal const uint MonitorDefaultToNearest = 0x00000002;
    internal const uint ModControl = 0x0002;
    internal const uint ModShift = 0x0004;
    internal const uint WmHotkey = 0x0312;
    internal const uint WdaExcludeFromCapture = 0x00000011;
    internal const int GwlExStyle = -20;
    internal const long WsExNoActivate = 0x08000000L;

    internal delegate bool EnumWindowsCallback(IntPtr windowHandle, IntPtr state);

    internal delegate void WinEventCallback(
        IntPtr hook,
        uint eventType,
        IntPtr windowHandle,
        int objectId,
        int childId,
        uint eventThread,
        uint eventTime);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool EnumWindows(EnumWindowsCallback callback, IntPtr state);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool IsWindow(IntPtr windowHandle);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool IsWindowVisible(IntPtr windowHandle);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool IsIconic(IntPtr windowHandle);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    internal static extern int GetWindowText(IntPtr windowHandle, StringBuilder text, int maximumCount);

    [DllImport("user32.dll")]
    internal static extern int GetWindowTextLength(IntPtr windowHandle);

    [DllImport("user32.dll")]
    internal static extern uint GetWindowThreadProcessId(IntPtr windowHandle, out uint processId);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool GetWindowRect(IntPtr windowHandle, out NativeRect rectangle);

    [DllImport("user32.dll")]
    internal static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    internal static extern uint GetDpiForWindow(IntPtr windowHandle);

    [DllImport("user32.dll")]
    internal static extern IntPtr MonitorFromWindow(IntPtr windowHandle, uint flags);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool GetMonitorInfo(IntPtr monitorHandle, ref MonitorInfo monitorInfo);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool RegisterHotKey(IntPtr windowHandle, int id, uint modifiers, uint virtualKey);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool UnregisterHotKey(IntPtr windowHandle, int id);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool SetWindowDisplayAffinity(IntPtr windowHandle, uint affinity);

    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW", SetLastError = true)]
    internal static extern IntPtr GetWindowLongPtr(IntPtr windowHandle, int index);

    [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW", SetLastError = true)]
    internal static extern IntPtr SetWindowLongPtr(IntPtr windowHandle, int index, IntPtr value);

    [DllImport("user32.dll", SetLastError = true)]
    internal static extern IntPtr SetWinEventHook(
        uint eventMinimum,
        uint eventMaximum,
        IntPtr eventHookModule,
        WinEventCallback callback,
        uint processId,
        uint threadId,
        uint flags);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool UnhookWinEvent(IntPtr hook);

    [DllImport("dwmapi.dll")]
    internal static extern int DwmGetWindowAttribute(
        IntPtr windowHandle,
        int attribute,
        out NativeRect value,
        int valueSize);

    [DllImport("dwmapi.dll")]
    internal static extern int DwmGetWindowAttribute(
        IntPtr windowHandle,
        int attribute,
        out int value,
        int valueSize);
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct NativeRect
{
    internal readonly int Left;
    internal readonly int Top;
    internal readonly int Right;
    internal readonly int Bottom;

    internal int Width => Right - Left;

    internal int Height => Bottom - Top;
}

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
internal struct MonitorInfo
{
    internal int Size;
    internal NativeRect Monitor;
    internal NativeRect WorkArea;
    internal uint Flags;
}
