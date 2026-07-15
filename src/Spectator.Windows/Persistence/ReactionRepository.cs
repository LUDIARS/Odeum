using Microsoft.Data.Sqlite;
using Spectator.Core.Reactions;

namespace Spectator.Windows.Persistence;

internal sealed class ReactionRepository
{
    internal const int MaximumContentLength = 2000;
    private readonly SpectatorDatabase _database;

    internal ReactionRepository(SpectatorDatabase database) => _database = database;

    internal void Add(ReactionAnnotation annotation)
    {
        Validate(annotation);
        using SqliteConnection connection = _database.OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = """
            INSERT INTO reaction_annotations (id, draft_id, video_offset_ms, content, recorded_at, kind)
            VALUES ($id, $draftId, $offset, $content, $recordedAt, $kind)
            """;
        command.Parameters.AddWithValue("$id", annotation.Id.ToString("D"));
        command.Parameters.AddWithValue("$draftId", annotation.DraftId.ToString("D"));
        command.Parameters.AddWithValue("$offset", annotation.VideoOffsetMilliseconds);
        command.Parameters.AddWithValue("$content", annotation.Content);
        command.Parameters.AddWithValue("$recordedAt", annotation.RecordedAt.ToUniversalTime().ToString("O"));
        command.Parameters.AddWithValue("$kind", annotation.Kind.ToString());
        command.ExecuteNonQuery();
    }

    internal IReadOnlyList<ReactionAnnotation> List(Guid draftId)
    {
        if (draftId == Guid.Empty) throw new ArgumentException("A draft identifier is required.", nameof(draftId));
        using SqliteConnection connection = _database.OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = """
            SELECT id, draft_id, video_offset_ms, kind, content, recorded_at
            FROM reaction_annotations
            WHERE draft_id = $draftId
            ORDER BY video_offset_ms, recorded_at, id
            """;
        command.Parameters.AddWithValue("$draftId", draftId.ToString("D"));
        using SqliteDataReader reader = command.ExecuteReader();
        var annotations = new List<ReactionAnnotation>();
        while (reader.Read())
        {
            annotations.Add(new ReactionAnnotation(
                Guid.Parse(reader.GetString(0)),
                Guid.Parse(reader.GetString(1)),
                reader.GetInt64(2),
                Enum.Parse<ReactionKind>(reader.GetString(3)),
                reader.GetString(4),
                DateTimeOffset.Parse(reader.GetString(5), CultureInfo.InvariantCulture)));
        }
        return annotations;
    }

    internal void Delete(Guid annotationId, Guid draftId)
    {
        if (annotationId == Guid.Empty) throw new ArgumentException("An annotation identifier is required.", nameof(annotationId));
        if (draftId == Guid.Empty) throw new ArgumentException("A draft identifier is required.", nameof(draftId));
        using SqliteConnection connection = _database.OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = "DELETE FROM reaction_annotations WHERE id = $id AND draft_id = $draftId";
        command.Parameters.AddWithValue("$id", annotationId.ToString("D"));
        command.Parameters.AddWithValue("$draftId", draftId.ToString("D"));
        if (command.ExecuteNonQuery() != 1)
        {
            throw new InvalidOperationException("The reaction annotation does not exist in this draft.");
        }
    }

    private static void Validate(ReactionAnnotation annotation)
    {
        ArgumentNullException.ThrowIfNull(annotation);
        if (annotation.Id == Guid.Empty) throw new ArgumentException("An annotation identifier is required.", nameof(annotation));
        if (annotation.DraftId == Guid.Empty) throw new ArgumentException("A draft identifier is required.", nameof(annotation));
        if (annotation.VideoOffsetMilliseconds < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(annotation), "The video offset must not be negative.");
        }
        if (!Enum.IsDefined(annotation.Kind))
        {
            throw new ArgumentOutOfRangeException(nameof(annotation), "The reaction kind is invalid.");
        }
        if (string.IsNullOrWhiteSpace(annotation.Content) || annotation.Content.Length > MaximumContentLength)
        {
            throw new ArgumentException(
                $"The reaction must contain 1 to {MaximumContentLength} characters.",
                nameof(annotation));
        }
    }
}
