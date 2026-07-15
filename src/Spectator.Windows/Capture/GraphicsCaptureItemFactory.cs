using System.Runtime.InteropServices;
using Windows.Graphics.Capture;
using WinRT;

namespace Spectator.Windows.Capture;

internal static class GraphicsCaptureItemFactory
{
    private static readonly Guid GraphicsCaptureItemId = new("79C3F95B-31F7-4EC2-A464-632EF5D30760");

    internal static GraphicsCaptureItem CreateForWindow(IntPtr windowHandle)
    {
        Marshal.ThrowExceptionForHR(WindowsCreateString(
            "Windows.Graphics.Capture.GraphicsCaptureItem",
            "Windows.Graphics.Capture.GraphicsCaptureItem".Length,
            out IntPtr className));
        try
        {
            Guid factoryId = typeof(IGraphicsCaptureItemInterop).GUID;
            Marshal.ThrowExceptionForHR(RoGetActivationFactory(className, ref factoryId, out IntPtr factoryPointer));
            try
            {
                var interop = (IGraphicsCaptureItemInterop)Marshal.GetObjectForIUnknown(factoryPointer);
                Guid interfaceId = GraphicsCaptureItemId;
                interop.CreateForWindow(windowHandle, ref interfaceId, out IntPtr itemPointer);
                try
                {
                    return MarshalInterface<GraphicsCaptureItem>.FromAbi(itemPointer);
                }
                finally
                {
                    Marshal.Release(itemPointer);
                }
            }
            finally
            {
                Marshal.Release(factoryPointer);
            }
        }
        finally
        {
            _ = WindowsDeleteString(className);
        }
    }

    [DllImport("combase.dll", ExactSpelling = true)]
    private static extern int RoGetActivationFactory(IntPtr className, ref Guid interfaceId, out IntPtr factory);

    [DllImport("combase.dll", ExactSpelling = true, CharSet = CharSet.Unicode)]
    private static extern int WindowsCreateString(string source, int length, out IntPtr value);

    [DllImport("combase.dll", ExactSpelling = true)]
    private static extern int WindowsDeleteString(IntPtr value);

    [ComImport]
    [Guid("3628E81B-3CAC-4C60-B7F4-23CE0E0C3356")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IGraphicsCaptureItemInterop
    {
        void CreateForWindow(IntPtr windowHandle, ref Guid interfaceId, out IntPtr result);

        void CreateForMonitor(IntPtr monitorHandle, ref Guid interfaceId, out IntPtr result);
    }
}
