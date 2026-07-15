using System.Runtime.InteropServices;
using Windows.Graphics.DirectX.Direct3D11;
using WinRT;

namespace Spectator.Windows.Capture;

internal static class Direct3DDeviceFactory
{
    private const uint D3d11SdkVersion = 7;
    private const uint D3d11CreateDeviceBgraSupport = 0x20;
    private static readonly Guid DxgiDeviceId = new("54EC77FA-1377-44E6-8C32-88FD5F44C84C");

    internal static IDirect3DDevice CreateHardwareDevice()
    {
        int result = D3D11CreateDevice(
            IntPtr.Zero,
            D3dDriverType.Hardware,
            IntPtr.Zero,
            D3d11CreateDeviceBgraSupport,
            IntPtr.Zero,
            0,
            D3d11SdkVersion,
            out IntPtr d3dDevice,
            out _,
            out IntPtr immediateContext);
        Marshal.ThrowExceptionForHR(result);
        try
        {
            Guid interfaceId = DxgiDeviceId;
            Marshal.ThrowExceptionForHR(Marshal.QueryInterface(d3dDevice, ref interfaceId, out IntPtr dxgiDevice));
            try
            {
                Marshal.ThrowExceptionForHR(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice, out IntPtr inspectable));
                try
                {
                    return MarshalInterface<IDirect3DDevice>.FromAbi(inspectable);
                }
                finally
                {
                    Marshal.Release(inspectable);
                }
            }
            finally
            {
                Marshal.Release(dxgiDevice);
            }
        }
        finally
        {
            if (immediateContext != IntPtr.Zero) Marshal.Release(immediateContext);
            if (d3dDevice != IntPtr.Zero) Marshal.Release(d3dDevice);
        }
    }

    [DllImport("d3d11.dll", ExactSpelling = true)]
    private static extern int D3D11CreateDevice(
        IntPtr adapter,
        D3dDriverType driverType,
        IntPtr software,
        uint flags,
        IntPtr featureLevels,
        uint featureLevelCount,
        uint sdkVersion,
        out IntPtr device,
        out int selectedFeatureLevel,
        out IntPtr immediateContext);

    [DllImport("d3d11.dll", ExactSpelling = true)]
    private static extern int CreateDirect3D11DeviceFromDXGIDevice(
        IntPtr dxgiDevice,
        out IntPtr graphicsDevice);

    private enum D3dDriverType
    {
        Hardware = 1,
    }
}
