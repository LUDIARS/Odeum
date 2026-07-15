using System.ComponentModel;
using System.Diagnostics;
using System.Text;
using Spectator.Windows.Interop;

namespace Spectator.Windows.Windows;

public sealed class WindowCatalog
{
    public IReadOnlyList<WindowDescriptor> ListCapturableWindows()
    {
        var currentProcessId = (uint)Environment.ProcessId;
        var windows = new List<WindowDescriptor>();

        if (!NativeMethods.EnumWindows(CollectWindow, IntPtr.Zero))
        {
            throw new Win32Exception("Unable to enumerate desktop windows.");
        }

        return windows
            .OrderBy(window => window.ProcessName, StringComparer.OrdinalIgnoreCase)
            .ThenBy(window => window.Title, StringComparer.CurrentCultureIgnoreCase)
            .ToArray();

        bool CollectWindow(IntPtr windowHandle, IntPtr state)
        {
            if (!NativeMethods.IsWindowVisible(windowHandle) || IsCloaked(windowHandle))
            {
                return true;
            }

            var titleLength = NativeMethods.GetWindowTextLength(windowHandle);
            if (titleLength <= 0)
            {
                return true;
            }

            _ = NativeMethods.GetWindowThreadProcessId(windowHandle, out var processId);
            if (processId == 0 || processId == currentProcessId)
            {
                return true;
            }

            var titleBuilder = new StringBuilder(titleLength + 1);
            _ = NativeMethods.GetWindowText(windowHandle, titleBuilder, titleBuilder.Capacity);
            var title = titleBuilder.ToString().Trim();
            if (title.Length == 0)
            {
                return true;
            }

            var processName = TryGetProcessName(processId);
            if (processName is null)
            {
                return true;
            }

            windows.Add(new WindowDescriptor(windowHandle, processId, processName, title));
            return true;
        }
    }

    private static bool IsCloaked(IntPtr windowHandle)
    {
        var result = NativeMethods.DwmGetWindowAttribute(
            windowHandle,
            NativeMethods.DwmwaCloaked,
            out int cloaked,
            sizeof(int));
        return result == 0 && cloaked != 0;
    }

    private static string? TryGetProcessName(uint processId)
    {
        try
        {
            using var process = Process.GetProcessById(checked((int)processId));
            return process.ProcessName;
        }
        catch (ArgumentException)
        {
            return null;
        }
        catch (InvalidOperationException)
        {
            return null;
        }
        catch (Win32Exception)
        {
            return null;
        }
    }
}
