using System.Windows.Media;
using System.Windows.Media.Imaging;
using Spectator.Windows.Interop;
using Windows.Graphics.Capture;
using Windows.Graphics.DirectX;
using Windows.Graphics.Imaging;
using Windows.Storage.Streams;

namespace Spectator.Windows.Capture;

internal sealed class WindowScreenshotCapture
{
    private static readonly TimeSpan CaptureTimeout = TimeSpan.FromSeconds(2);

    internal async Task SaveAsync(
        IntPtr targetHandle,
        string destinationPath,
        string format,
        CancellationToken cancellationToken)
    {
        Exception? firstFailure = null;
        for (int attempt = 0; attempt < 2; attempt++)
        {
            try
            {
                byte[] encoded = await CaptureEncodedAsync(targetHandle, format, cancellationToken);
                ValidateFrame(encoded);
                await SaveAtomicAsync(encoded, destinationPath, cancellationToken);
                return;
            }
            catch (Exception exception) when (attempt == 0 && exception is not OperationCanceledException)
            {
                firstFailure = exception;
                await Task.Delay(TimeSpan.FromMilliseconds(75), cancellationToken);
            }
        }
        throw new InvalidOperationException("The WGC screenshot failed after one retry.", firstFailure);
    }

    private static async Task<byte[]> CaptureEncodedAsync(
        IntPtr targetHandle,
        string format,
        CancellationToken cancellationToken)
    {
        if (!NativeMethods.IsWindow(targetHandle))
        {
            throw new InvalidOperationException("The target window is no longer available.");
        }
        if (NativeMethods.IsIconic(targetHandle))
        {
            throw new InvalidOperationException("A minimized target window cannot be captured.");
        }

        GraphicsCaptureItem item = GraphicsCaptureItemFactory.CreateForWindow(targetHandle);
        if (item.Size.Width <= 0 || item.Size.Height <= 0)
        {
            throw new InvalidDataException("The target window has a zero-sized capture surface.");
        }

        using var device = Direct3DDeviceFactory.CreateHardwareDevice();
        using Direct3D11CaptureFramePool framePool = Direct3D11CaptureFramePool.CreateFreeThreaded(
            device,
            DirectXPixelFormat.B8G8R8A8UIntNormalized,
            2,
            item.Size);
        using GraphicsCaptureSession session = framePool.CreateCaptureSession(item);
        var completion = new TaskCompletionSource<SoftwareBitmap>(TaskCreationOptions.RunContinuationsAsynchronously);
        int acceptingFrame = 1;

        framePool.FrameArrived += FrameArrived;
        try
        {
            session.StartCapture();
            using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            timeout.CancelAfter(CaptureTimeout);
            SoftwareBitmap bitmap = await completion.Task.WaitAsync(timeout.Token);
            using (bitmap)
            {
                return await EncodeAsync(bitmap, format, cancellationToken);
            }
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            throw new TimeoutException("The WGC screenshot did not produce a valid frame within two seconds.");
        }
        finally
        {
            Interlocked.Exchange(ref acceptingFrame, 0);
            framePool.FrameArrived -= FrameArrived;
        }

        async void FrameArrived(Direct3D11CaptureFramePool sender, object arguments)
        {
            if (Interlocked.Exchange(ref acceptingFrame, 0) == 0) return;
            try
            {
                using Direct3D11CaptureFrame frame = sender.TryGetNextFrame();
                if (frame.ContentSize.Width <= 0 || frame.ContentSize.Height <= 0)
                {
                    throw new InvalidDataException("WGC returned a zero-sized frame.");
                }
                SoftwareBitmap copy = await SoftwareBitmap.CreateCopyFromSurfaceAsync(frame.Surface);
                completion.TrySetResult(copy);
            }
            catch (Exception exception)
            {
                completion.TrySetException(exception);
            }
        }
    }

    private static async Task<byte[]> EncodeAsync(
        SoftwareBitmap source,
        string format,
        CancellationToken cancellationToken)
    {
        Guid encoderId = format switch
        {
            "jpeg" => global::Windows.Graphics.Imaging.BitmapEncoder.JpegEncoderId,
            "png" => global::Windows.Graphics.Imaging.BitmapEncoder.PngEncoderId,
            _ => throw new ArgumentOutOfRangeException(nameof(format), format, "Unsupported screenshot format."),
        };
        using var stream = new InMemoryRandomAccessStream();
        global::Windows.Graphics.Imaging.BitmapEncoder encoder =
            await global::Windows.Graphics.Imaging.BitmapEncoder.CreateAsync(encoderId, stream);
        encoder.SetSoftwareBitmap(source);
        await encoder.FlushAsync();
        cancellationToken.ThrowIfCancellationRequested();
        stream.Seek(0);
        using var reader = new DataReader(stream.GetInputStreamAt(0));
        uint size = checked((uint)stream.Size);
        await reader.LoadAsync(size);
        byte[] encoded = new byte[size];
        reader.ReadBytes(encoded);
        return encoded;
    }

    private static void ValidateFrame(byte[] encoded)
    {
        using var input = new MemoryStream(encoded, false);
        System.Windows.Media.Imaging.BitmapDecoder decoder = System.Windows.Media.Imaging.BitmapDecoder.Create(
            input,
            BitmapCreateOptions.PreservePixelFormat,
            BitmapCacheOption.OnLoad);
        BitmapSource source = decoder.Frames[0];
        var converted = new FormatConvertedBitmap(source, PixelFormats.Bgra32, null, 0);
        int stride = checked(converted.PixelWidth * 4);
        byte[] pixels = new byte[checked(stride * converted.PixelHeight)];
        converted.CopyPixels(pixels, stride, 0);
        int step = Math.Max(4, pixels.Length / 4096 / 4 * 4);
        int samples = 0;
        long luminance = 0;
        byte minimumBlue = byte.MaxValue;
        byte maximumBlue = byte.MinValue;
        byte minimumGreen = byte.MaxValue;
        byte maximumGreen = byte.MinValue;
        byte minimumRed = byte.MaxValue;
        byte maximumRed = byte.MinValue;
        for (int offset = 0; offset + 3 < pixels.Length; offset += step)
        {
            byte blue = pixels[offset];
            byte green = pixels[offset + 1];
            byte red = pixels[offset + 2];
            minimumBlue = Math.Min(minimumBlue, blue);
            maximumBlue = Math.Max(maximumBlue, blue);
            minimumGreen = Math.Min(minimumGreen, green);
            maximumGreen = Math.Max(maximumGreen, green);
            minimumRed = Math.Min(minimumRed, red);
            maximumRed = Math.Max(maximumRed, red);
            luminance += red * 54L + green * 183L + blue * 19L;
            samples++;
        }
        if (samples == 0 || luminance / samples / 256 < 2)
        {
            throw new InvalidDataException("The captured frame was black.");
        }
        if (maximumBlue - minimumBlue <= 1
            && maximumGreen - minimumGreen <= 1
            && maximumRed - minimumRed <= 1)
        {
            throw new InvalidDataException("The captured frame was a single color.");
        }
    }

    private static async Task SaveAtomicAsync(
        byte[] encoded,
        string destinationPath,
        CancellationToken cancellationToken)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(destinationPath)
            ?? throw new InvalidOperationException("The screenshot directory is invalid."));
        string temporaryPath = destinationPath + ".tmp";
        try
        {
            await File.WriteAllBytesAsync(temporaryPath, encoded, cancellationToken);
            File.Move(temporaryPath, destinationPath, true);
        }
        finally
        {
            if (File.Exists(temporaryPath)) File.Delete(temporaryPath);
        }
    }
}
