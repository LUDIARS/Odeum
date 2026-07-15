using System.Runtime.InteropServices;

namespace Spectator.Windows.Capture;

internal static class MediaDurationProbe
{
    private const uint GpsBestEffort = 0x00000040;
    private static readonly PropertyKey DurationKey = new(
        new Guid("64440490-4C8B-11D1-8B70-080036B11A03"),
        3);

    internal static TimeSpan GetDuration(string path)
    {
        Guid interfaceId = typeof(IPropertyStore).GUID;
        int result = SHGetPropertyStoreFromParsingName(path, IntPtr.Zero, GpsBestEffort, ref interfaceId, out IPropertyStore store);
        Marshal.ThrowExceptionForHR(result);
        try
        {
            PropertyKey durationKey = DurationKey;
            result = store.GetValue(ref durationKey, out PropVariant value);
            Marshal.ThrowExceptionForHR(result);
            try
            {
                if (value.VariantType != 21 || value.UnsignedValue == 0)
                {
                    throw new InvalidDataException("Windows could not determine the replay's actual duration.");
                }
                return TimeSpan.FromTicks(checked((long)value.UnsignedValue));
            }
            finally
            {
                _ = PropVariantClear(ref value);
            }
        }
        finally
        {
            _ = Marshal.ReleaseComObject(store);
        }
    }

    [DllImport("shell32.dll", CharSet = CharSet.Unicode, PreserveSig = true)]
    private static extern int SHGetPropertyStoreFromParsingName(
        string path,
        IntPtr bindContext,
        uint flags,
        ref Guid interfaceId,
        [MarshalAs(UnmanagedType.Interface)] out IPropertyStore propertyStore);

    [DllImport("ole32.dll")]
    private static extern int PropVariantClear(ref PropVariant variant);

    [ComImport]
    [Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IPropertyStore
    {
        [PreserveSig]
        int GetCount(out uint propertyCount);

        [PreserveSig]
        int GetAt(uint propertyIndex, out PropertyKey key);

        [PreserveSig]
        int GetValue(ref PropertyKey key, out PropVariant value);

        [PreserveSig]
        int SetValue(ref PropertyKey key, ref PropVariant value);

        [PreserveSig]
        int Commit();
    }

    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    private readonly struct PropertyKey
    {
        internal PropertyKey(Guid formatId, uint propertyId)
        {
            FormatId = formatId;
            PropertyId = propertyId;
        }

        private readonly Guid FormatId;
        private readonly uint PropertyId;
    }

    [StructLayout(LayoutKind.Explicit)]
    private struct PropVariant
    {
        [FieldOffset(0)]
        internal ushort VariantType;

        [FieldOffset(8)]
        internal ulong UnsignedValue;
    }
}
