using System.Text;
using System.Text.Json;
using Spectator.Core.Drafts;

namespace Spectator.Windows.Storage;

internal sealed class CaptureStore
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web)
    {
        WriteIndented = true,
    };

    private readonly string _rootDirectory;

    internal CaptureStore(string? rootDirectory = null)
    {
        if (!string.IsNullOrWhiteSpace(rootDirectory))
        {
            _rootDirectory = Path.GetFullPath(rootDirectory);
            return;
        }
        string localData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        if (string.IsNullOrWhiteSpace(localData))
        {
            throw new InvalidOperationException("The local application data directory is unavailable.");
        }

        _rootDirectory = Path.Combine(localData, "Spectator", "captures");
    }

    internal string RootDirectory => _rootDirectory;

    internal string GetMediaPath(Guid captureId, string extension)
    {
        if (string.IsNullOrWhiteSpace(extension) || extension.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
        {
            throw new ArgumentException("A valid media extension is required.", nameof(extension));
        }

        Directory.CreateDirectory(_rootDirectory);
        return Path.Combine(_rootDirectory, $"{captureId:N}{extension}");
    }

    internal string ToRelativePath(string absolutePath) => Path.GetRelativePath(_rootDirectory, absolutePath);

    internal async Task<string> ImportMediaAsync(
        string sourcePath,
        Guid captureId,
        string extension,
        CancellationToken cancellationToken)
    {
        string fullSourcePath = Path.GetFullPath(sourcePath);
        if (!File.Exists(fullSourcePath)) throw new FileNotFoundException("The imported media file does not exist.", fullSourcePath);
        string destination = GetMediaPath(captureId, extension);
        string temporary = destination + ".tmp";
        try
        {
            await using (var input = new FileStream(
                fullSourcePath,
                FileMode.Open,
                FileAccess.Read,
                FileShare.Read,
                1024 * 1024,
                FileOptions.Asynchronous | FileOptions.SequentialScan))
            await using (var output = new FileStream(
                temporary,
                FileMode.Create,
                FileAccess.Write,
                FileShare.None,
                1024 * 1024,
                FileOptions.Asynchronous | FileOptions.WriteThrough))
            {
                await input.CopyToAsync(output, cancellationToken);
                await output.FlushAsync(cancellationToken);
            }
            File.Move(temporary, destination, true);
            return destination;
        }
        finally
        {
            if (File.Exists(temporary)) File.Delete(temporary);
        }
    }

    internal int CleanupOrphans(IReadOnlySet<string> knownRelativePaths, DateTimeOffset olderThan)
    {
        ArgumentNullException.ThrowIfNull(knownRelativePaths);
        if (!Directory.Exists(_rootDirectory)) return 0;
        int deleted = 0;
        foreach (string filePath in Directory.EnumerateFiles(_rootDirectory, "*", SearchOption.TopDirectoryOnly))
        {
            string relativePath = Path.GetRelativePath(_rootDirectory, filePath);
            if (knownRelativePaths.Contains(relativePath)) continue;
            if (File.GetLastWriteTimeUtc(filePath) >= olderThan.UtcDateTime) continue;
            File.Delete(filePath);
            deleted += 1;
        }
        return deleted;
    }

    internal async Task<string> SaveDraftAsync(CaptureDraft draft, CancellationToken cancellationToken)
    {
        Directory.CreateDirectory(_rootDirectory);
        string destinationPath = Path.Combine(_rootDirectory, $"{draft.Anchor.Id:N}.json");
        string temporaryPath = destinationPath + ".tmp";
        try
        {
            await using var stream = new FileStream(
                temporaryPath,
                FileMode.Create,
                FileAccess.Write,
                FileShare.None,
                64 * 1024,
                FileOptions.Asynchronous | FileOptions.WriteThrough);
            await JsonSerializer.SerializeAsync(stream, draft, JsonOptions, cancellationToken);
            await stream.FlushAsync(cancellationToken);
            stream.Close();
            File.Move(temporaryPath, destinationPath, true);
            return destinationPath;
        }
        finally
        {
            if (File.Exists(temporaryPath))
            {
                File.Delete(temporaryPath);
            }
        }
    }
}
