using System.Security.Cryptography;
using Spectator.Core.Drafts;
using Spectator.Core.Identifiers;
using Spectator.Windows.Storage;

namespace Spectator.Windows.Capture;

internal sealed class CaptureAssetFactory
{
    private readonly IIdentifierSource _identifierSource;
    private readonly CaptureStore _store;

    internal CaptureAssetFactory(IIdentifierSource identifierSource, CaptureStore store)
    {
        _identifierSource = identifierSource;
        _store = store;
    }

    internal async Task<StoredAsset> CreateAsync(
        Guid draftId,
        string kind,
        string absolutePath,
        string mimeType,
        int? durationMilliseconds,
        DateTimeOffset? capturedAt,
        DateTimeOffset? clipStartedAt,
        DateTimeOffset? clipEndedAt,
        CancellationToken cancellationToken)
    {
        var info = new FileInfo(absolutePath);
        if (!info.Exists || info.Length <= 0)
        {
            throw new InvalidDataException($"The captured {kind} file is empty.");
        }
        await using var stream = new FileStream(
            absolutePath,
            FileMode.Open,
            FileAccess.Read,
            FileShare.Read,
            1024 * 1024,
            FileOptions.Asynchronous | FileOptions.SequentialScan);
        byte[] hash = await SHA256.HashDataAsync(stream, cancellationToken);
        return new StoredAsset(
            _identifierSource.NewIdentifier(),
            draftId,
            kind,
            _store.ToRelativePath(absolutePath),
            mimeType,
            info.Length,
            Convert.ToHexString(hash).ToLowerInvariant(),
            durationMilliseconds,
            capturedAt,
            clipStartedAt,
            clipEndedAt,
            AssetState.Local,
            true);
    }
}
