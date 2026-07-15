using System.Text;
using System.Text.Json;
using Spectator.Core.Drafts;
using Spectator.Core.Reactions;

namespace Spectator.Windows.Reactions;

internal sealed class ReactionRawDataExporter
{
    internal const string CurrentSchemaVersion = "spectator.reaction-raw/v2";
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true,
    };

    internal ReactionRawDataDocument CreateDocument(
        string gameId,
        StoredDraft draft,
        IReadOnlyList<ReactionAnnotation> annotations)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(gameId);
        ArgumentNullException.ThrowIfNull(draft);
        ArgumentNullException.ThrowIfNull(annotations);
        if (annotations.Count == 0)
        {
            throw new InvalidOperationException("At least one self-reported reaction is required for raw data export.");
        }
        StoredAsset video = draft.Assets.SingleOrDefault(asset => asset.Kind == "video")
            ?? throw new InvalidOperationException("The draft does not contain a video.");
        int duration = video.DurationMilliseconds
            ?? throw new InvalidDataException("The video duration is required for reaction raw data.");
        if (duration < 0) throw new InvalidDataException("The video duration must not be negative.");

        ReactionRawDataEntry[] entries = annotations
            .OrderBy(annotation => annotation.VideoOffsetMilliseconds)
            .ThenBy(annotation => annotation.RecordedAt)
            .ThenBy(annotation => annotation.Id)
            .Select(annotation => CreateEntry(annotation, draft.Id, duration))
            .ToArray();
        return new ReactionRawDataDocument(
            CurrentSchemaVersion,
            "self_report",
            gameId,
            video.Sha256,
            new ReactionRawDataSource(
                draft.Id.ToString("D"),
                draft.CaptureAnchorId,
                video.Sha256,
                video.MimeType,
                duration,
                video.ClipStartedAt?.ToUniversalTime(),
                video.ClipEndedAt?.ToUniversalTime()),
            entries);
    }

    internal void Write(
        string path,
        string gameId,
        StoredDraft draft,
        IReadOnlyList<ReactionAnnotation> annotations)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        string fullPath = Path.GetFullPath(path);
        string directory = Path.GetDirectoryName(fullPath)
            ?? throw new InvalidOperationException("The raw data output directory is invalid.");
        if (!Directory.Exists(directory))
        {
            throw new DirectoryNotFoundException($"The raw data output directory does not exist: {directory}");
        }

        ReactionRawDataDocument document = CreateDocument(gameId, draft, annotations);
        string temporary = fullPath + ".tmp";
        try
        {
            string json = JsonSerializer.Serialize(document, JsonOptions);
            File.WriteAllText(temporary, json + Environment.NewLine, new UTF8Encoding(false));
            File.Move(temporary, fullPath, true);
        }
        finally
        {
            if (File.Exists(temporary)) File.Delete(temporary);
        }
    }

    private static ReactionRawDataEntry CreateEntry(
        ReactionAnnotation annotation,
        Guid expectedDraftId,
        int videoDurationMilliseconds)
    {
        if (annotation.DraftId != expectedDraftId)
        {
            throw new InvalidDataException("A reaction annotation belongs to a different draft.");
        }
        if (annotation.VideoOffsetMilliseconds < 0
            || annotation.VideoOffsetMilliseconds > videoDurationMilliseconds)
        {
            throw new InvalidDataException("A reaction annotation is outside the video duration.");
        }
        return new ReactionRawDataEntry(
            annotation.Id.ToString("D"),
            annotation.VideoOffsetMilliseconds,
            annotation.Kind.ToString().ToLowerInvariant(),
            annotation.Content,
            annotation.RecordedAt.ToUniversalTime());
    }
}
