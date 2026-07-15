using System.Runtime.InteropServices;
using System.Windows.Media;
using System.Windows.Threading;

namespace Spectator.Windows.Capture;

internal static class MediaDurationProbe
{
    private const uint GpsBestEffort = 0x00000040;
    private static readonly PropertyKey DurationKey = new(
        new Guid("64440490-4C8B-11D1-8B70-080036B11A03"),
        3);

    internal static TimeSpan GetDuration(string path)
    {
        Exception? propertyError = null;
        try
        {
            return GetPropertyDuration(path);
        }
        catch (Exception error) when (error is COMException or InvalidDataException or OverflowException)
        {
            propertyError = error;
        }

        try
        {
            return GetMediaPlayerDuration(path);
        }
        catch (Exception mediaError) when (mediaError is InvalidDataException or TimeoutException)
        {
            throw new InvalidDataException(
                "Windows could not determine the replay's actual duration with either media probe.",
                new AggregateException(propertyError, mediaError));
        }
    }

    private static TimeSpan GetPropertyDuration(string path)
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
                    throw new InvalidDataException("The Windows property handler did not expose a media duration.");
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

    private static TimeSpan GetMediaPlayerDuration(string path)
    {
        using var completed = new ManualResetEventSlim();
        TimeSpan? duration = null;
        Exception? failure = null;
        var thread = new Thread(() =>
        {
            MediaPlayer? player = null;
            DispatcherTimer? timeout = null;
            try
            {
                Dispatcher dispatcher = Dispatcher.CurrentDispatcher;
                player = new MediaPlayer();
                timeout = new DispatcherTimer(TimeSpan.FromSeconds(10), DispatcherPriority.Send, (_, _) =>
                {
                    failure = new TimeoutException("Windows MediaPlayer did not open the video within 10 seconds.");
                    Finish();
                }, dispatcher);
                player.MediaOpened += (_, _) =>
                {
                    if (player.NaturalDuration.HasTimeSpan && player.NaturalDuration.TimeSpan > TimeSpan.Zero)
                    {
                        duration = player.NaturalDuration.TimeSpan;
                    }
                    else
                    {
                        failure = new InvalidDataException("Windows MediaPlayer opened the video without a duration.");
                    }
                    Finish();
                };
                player.MediaFailed += (_, eventArgs) =>
                {
                    failure = new InvalidDataException("Windows MediaPlayer rejected the video.", eventArgs.ErrorException);
                    Finish();
                };
                timeout.Start();
                player.Open(new Uri(Path.GetFullPath(path), UriKind.Absolute));
                Dispatcher.Run();

                void Finish()
                {
                    timeout?.Stop();
                    player?.Close();
                    completed.Set();
                    dispatcher.BeginInvokeShutdown(DispatcherPriority.Send);
                }
            }
            catch (Exception error)
            {
                failure = error;
                completed.Set();
                Dispatcher.CurrentDispatcher.BeginInvokeShutdown(DispatcherPriority.Send);
            }
            finally
            {
                timeout?.Stop();
                player?.Close();
            }
        })
        {
            IsBackground = true,
            Name = "Spectator media duration probe",
        };
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        if (!completed.Wait(TimeSpan.FromSeconds(15)))
        {
            throw new TimeoutException("The Windows media duration probe did not finish within 15 seconds.");
        }
        if (!thread.Join(TimeSpan.FromSeconds(2)))
        {
            throw new TimeoutException("The Windows media duration probe did not release its dispatcher.");
        }
        if (failure is not null) throw new InvalidDataException("Windows MediaPlayer could not read the duration.", failure);
        return duration ?? throw new InvalidDataException("Windows MediaPlayer returned no duration.");
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
