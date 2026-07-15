using Microsoft.Data.Sqlite;
using Spectator.Core.Drafts;

namespace Spectator.Windows.Persistence;

internal sealed class DraftRepository
{
    private readonly SpectatorDatabase _database;

    internal DraftRepository(SpectatorDatabase database) => _database = database;

    internal void Save(StoredDraft draft, DateTimeOffset now)
    {
        using SqliteConnection connection = _database.OpenConnection();
        using SqliteTransaction transaction = connection.BeginTransaction();
        using (var command = connection.CreateCommand())
        {
            command.Transaction = transaction;
            command.CommandText = """
                INSERT INTO drafts (
                  id, local_session_id, client_submission_id, capture_anchor_id, body, captured_at,
                  elapsed_ms, active_ms, state, remote_impression_id, last_error, created_at, updated_at
                ) VALUES (
                  $id, $sessionId, $submissionId, $anchorId, $body, $capturedAt,
                  $elapsed, $active, $state, $remoteId, $lastError, $createdAt, $updatedAt
                )
                """;
            command.Parameters.AddWithValue("$id", draft.Id.ToString("D"));
            command.Parameters.AddWithValue("$sessionId", draft.LocalSessionId.ToString("D"));
            command.Parameters.AddWithValue("$submissionId", draft.ClientSubmissionId);
            command.Parameters.AddWithValue("$anchorId", draft.CaptureAnchorId);
            command.Parameters.AddWithValue("$body", draft.Text);
            command.Parameters.AddWithValue("$capturedAt", draft.CapturedAt.ToString("O"));
            command.Parameters.AddWithValue("$elapsed", draft.ElapsedMilliseconds);
            command.Parameters.AddWithValue("$active", draft.ActiveMilliseconds);
            command.Parameters.AddWithValue("$state", draft.State.ToString());
            command.Parameters.AddWithValue("$remoteId", (object?)draft.RemoteImpressionId ?? DBNull.Value);
            command.Parameters.AddWithValue("$lastError", (object?)draft.LastError ?? DBNull.Value);
            command.Parameters.AddWithValue("$createdAt", now.ToString("O"));
            command.Parameters.AddWithValue("$updatedAt", now.ToString("O"));
            command.ExecuteNonQuery();
        }

        foreach (StoredAsset asset in draft.Assets)
        {
            using var command = connection.CreateCommand();
            command.Transaction = transaction;
            command.CommandText = """
                INSERT INTO assets (
                  id, draft_id, kind, relative_path, mime_type, size_bytes, sha256, duration_ms,
                  captured_at, clip_started_at, clip_ended_at, state, is_enabled
                ) VALUES (
                  $id, $draftId, $kind, $path, $mime, $size, $sha, $duration,
                  $capturedAt, $clipStartedAt, $clipEndedAt, $state, $enabled
                )
                """;
            command.Parameters.AddWithValue("$id", asset.Id.ToString("D"));
            command.Parameters.AddWithValue("$draftId", draft.Id.ToString("D"));
            command.Parameters.AddWithValue("$kind", asset.Kind);
            command.Parameters.AddWithValue("$path", asset.RelativePath);
            command.Parameters.AddWithValue("$mime", asset.MimeType);
            command.Parameters.AddWithValue("$size", asset.SizeBytes);
            command.Parameters.AddWithValue("$sha", asset.Sha256);
            command.Parameters.AddWithValue("$duration", (object?)asset.DurationMilliseconds ?? DBNull.Value);
            command.Parameters.AddWithValue("$capturedAt", DateValue(asset.CapturedAt));
            command.Parameters.AddWithValue("$clipStartedAt", DateValue(asset.ClipStartedAt));
            command.Parameters.AddWithValue("$clipEndedAt", DateValue(asset.ClipEndedAt));
            command.Parameters.AddWithValue("$state", asset.State.ToString());
            command.Parameters.AddWithValue("$enabled", asset.IsEnabled ? 1 : 0);
            command.ExecuteNonQuery();
        }
        transaction.Commit();
    }

    internal StoredDraft? Get(Guid draftId)
    {
        using SqliteConnection connection = _database.OpenConnection();
        return ReadDraft(connection, draftId);
    }

    internal IReadOnlyList<StoredDraft> ListDue(DateTimeOffset now, int limit)
    {
        using SqliteConnection connection = _database.OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = """
            SELECT draft_id FROM sync_jobs
            WHERE is_permanent_failure = 0 AND next_attempt_at <= $now
            ORDER BY next_attempt_at ASC LIMIT $limit
            """;
        command.Parameters.AddWithValue("$now", now.ToString("O"));
        command.Parameters.AddWithValue("$limit", limit);
        var identifiers = new List<Guid>();
        using (SqliteDataReader reader = command.ExecuteReader())
        {
            while (reader.Read()) identifiers.Add(Guid.Parse(reader.GetString(0)));
        }
        return identifiers.Select(id => ReadDraft(connection, id)).OfType<StoredDraft>().ToArray();
    }

    internal IReadOnlyList<StoredDraft> ListRecent(int limit = 50)
    {
        if (limit is < 1 or > 200) throw new ArgumentOutOfRangeException(nameof(limit));
        using SqliteConnection connection = _database.OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = "SELECT id FROM drafts ORDER BY captured_at DESC LIMIT $limit";
        command.Parameters.AddWithValue("$limit", limit);
        var identifiers = new List<Guid>();
        using (SqliteDataReader reader = command.ExecuteReader())
        {
            while (reader.Read()) identifiers.Add(Guid.Parse(reader.GetString(0)));
        }
        return identifiers.Select(id => ReadDraft(connection, id)).OfType<StoredDraft>().ToArray();
    }

    internal IReadOnlySet<string> ListAssetRelativePaths()
    {
        using SqliteConnection connection = _database.OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = "SELECT relative_path FROM assets";
        using SqliteDataReader reader = command.ExecuteReader();
        var paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        while (reader.Read()) paths.Add(reader.GetString(0));
        return paths;
    }

    internal void Queue(Guid draftId, DateTimeOffset now)
    {
        using SqliteConnection connection = _database.OpenConnection();
        using SqliteTransaction transaction = connection.BeginTransaction();
        UpdateDraft(connection, transaction, draftId, DraftState.Queued, null, null, now);
        using var command = connection.CreateCommand();
        command.Transaction = transaction;
        command.CommandText = """
            INSERT INTO sync_jobs (draft_id, next_attempt_at)
            VALUES ($id, $next)
            ON CONFLICT(draft_id) DO UPDATE SET
              next_attempt_at = excluded.next_attempt_at,
              is_permanent_failure = 0,
              last_error = NULL
            """;
        command.Parameters.AddWithValue("$id", draftId.ToString("D"));
        command.Parameters.AddWithValue("$next", now.ToString("O"));
        command.ExecuteNonQuery();
        transaction.Commit();
    }

    internal void UpdateTextAndQueue(Guid draftId, string text, DateTimeOffset now)
    {
        ValidateText(text);
        using SqliteConnection connection = _database.OpenConnection();
        using SqliteTransaction transaction = connection.BeginTransaction();
        using (var update = connection.CreateCommand())
        {
            update.Transaction = transaction;
            update.CommandText = """
                UPDATE drafts SET body = $body, state = 'Queued', updated_at = $updatedAt
                WHERE id = $id AND state IN ('Ready', 'Degraded', 'Rejected', 'Queued')
                """;
            update.Parameters.AddWithValue("$body", text);
            update.Parameters.AddWithValue("$updatedAt", now.ToString("O"));
            update.Parameters.AddWithValue("$id", draftId.ToString("D"));
            if (update.ExecuteNonQuery() != 1)
            {
                throw new InvalidOperationException("The draft cannot be queued from its current state.");
            }
        }
        using (var job = connection.CreateCommand())
        {
            job.Transaction = transaction;
            job.CommandText = """
                INSERT INTO sync_jobs (draft_id, next_attempt_at)
                VALUES ($id, $next)
                ON CONFLICT(draft_id) DO UPDATE SET
                  next_attempt_at = excluded.next_attempt_at,
                  is_permanent_failure = 0,
                  last_error = NULL
                """;
            job.Parameters.AddWithValue("$id", draftId.ToString("D"));
            job.Parameters.AddWithValue("$next", now.ToString("O"));
            job.ExecuteNonQuery();
        }
        transaction.Commit();
    }

    internal void UpdateText(Guid draftId, string text, DateTimeOffset now)
    {
        ValidateText(text);
        using SqliteConnection connection = _database.OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = """
            UPDATE drafts SET body = $body, updated_at = $updatedAt
            WHERE id = $id AND remote_impression_id IS NULL
            """;
        command.Parameters.AddWithValue("$body", text);
        command.Parameters.AddWithValue("$updatedAt", now.ToUniversalTime().ToString("O"));
        command.Parameters.AddWithValue("$id", draftId.ToString("D"));
        if (command.ExecuteNonQuery() != 1)
        {
            throw new InvalidOperationException("Only a local draft can be edited.");
        }
    }

    internal void MarkUploading(Guid draftId, string remoteImpressionId, DateTimeOffset now) =>
        SetState(draftId, DraftState.Uploading, remoteImpressionId, null, now);

    internal void MarkProcessing(Guid draftId, DateTimeOffset now) =>
        SetState(draftId, DraftState.Processing, null, null, now);

    internal void MarkSubmitted(Guid draftId, DateTimeOffset now)
    {
        using SqliteConnection connection = _database.OpenConnection();
        using SqliteTransaction transaction = connection.BeginTransaction();
        UpdateDraft(connection, transaction, draftId, DraftState.Submitted, null, null, now);
        using var command = connection.CreateCommand();
        command.Transaction = transaction;
        command.CommandText = "DELETE FROM sync_jobs WHERE draft_id = $id";
        command.Parameters.AddWithValue("$id", draftId.ToString("D"));
        command.ExecuteNonQuery();
        transaction.Commit();
    }

    internal int GetAttemptCount(Guid draftId)
    {
        using SqliteConnection connection = _database.OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = "SELECT attempts FROM sync_jobs WHERE draft_id = $id";
        command.Parameters.AddWithValue("$id", draftId.ToString("D"));
        object? value = command.ExecuteScalar();
        return value is null ? 0 : Convert.ToInt32(value, CultureInfo.InvariantCulture);
    }

    internal void ScheduleRetry(Guid draftId, int attempts, DateTimeOffset nextAttempt, string error, DateTimeOffset now)
    {
        using SqliteConnection connection = _database.OpenConnection();
        using SqliteTransaction transaction = connection.BeginTransaction();
        UpdateDraft(connection, transaction, draftId, DraftState.Queued, null, error, now);
        using var command = connection.CreateCommand();
        command.Transaction = transaction;
        command.CommandText = """
            UPDATE sync_jobs SET attempts = $attempts, next_attempt_at = $next, last_error = $error
            WHERE draft_id = $id
            """;
        command.Parameters.AddWithValue("$attempts", attempts);
        command.Parameters.AddWithValue("$next", nextAttempt.ToString("O"));
        command.Parameters.AddWithValue("$error", error);
        command.Parameters.AddWithValue("$id", draftId.ToString("D"));
        command.ExecuteNonQuery();
        transaction.Commit();
    }

    internal void MarkRejected(Guid draftId, string error, DateTimeOffset now)
    {
        using SqliteConnection connection = _database.OpenConnection();
        using SqliteTransaction transaction = connection.BeginTransaction();
        UpdateDraft(connection, transaction, draftId, DraftState.Rejected, null, error, now);
        using var command = connection.CreateCommand();
        command.Transaction = transaction;
        command.CommandText = """
            UPDATE sync_jobs SET is_permanent_failure = 1, last_error = $error WHERE draft_id = $id
            """;
        command.Parameters.AddWithValue("$error", error);
        command.Parameters.AddWithValue("$id", draftId.ToString("D"));
        command.ExecuteNonQuery();
        transaction.Commit();
    }

    internal void SetAssetEnabled(Guid assetId, bool isEnabled, DateTimeOffset now)
    {
        using SqliteConnection connection = _database.OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = """
            UPDATE assets SET is_enabled = $enabled
            WHERE id = $id AND state = 'Local'
            """;
        command.Parameters.AddWithValue("$enabled", isEnabled ? 1 : 0);
        command.Parameters.AddWithValue("$id", assetId.ToString("D"));
        if (command.ExecuteNonQuery() != 1)
        {
            throw new InvalidOperationException("Only a local asset can be attached or detached.");
        }
    }

    internal StoredDraft DeleteLocal(Guid draftId)
    {
        using SqliteConnection connection = _database.OpenConnection();
        StoredDraft draft = ReadDraft(connection, draftId)
            ?? throw new InvalidOperationException("The draft does not exist.");
        if (draft.RemoteImpressionId is not null)
        {
            throw new InvalidOperationException("A remotely registered draft must be deleted through Volputas.");
        }
        using var command = connection.CreateCommand();
        command.CommandText = "DELETE FROM drafts WHERE id = $id";
        command.Parameters.AddWithValue("$id", draftId.ToString("D"));
        command.ExecuteNonQuery();
        return draft;
    }

    internal StoredDraft DeleteAfterRemote(Guid draftId, string remoteImpressionId)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(remoteImpressionId);
        using SqliteConnection connection = _database.OpenConnection();
        StoredDraft draft = ReadDraft(connection, draftId)
            ?? throw new InvalidOperationException("The draft does not exist.");
        if (!string.Equals(draft.RemoteImpressionId, remoteImpressionId, StringComparison.Ordinal))
        {
            throw new InvalidOperationException("The remote impression identifier changed before deletion.");
        }
        using var command = connection.CreateCommand();
        command.CommandText = "DELETE FROM drafts WHERE id = $id AND remote_impression_id = $remoteId";
        command.Parameters.AddWithValue("$id", draftId.ToString("D"));
        command.Parameters.AddWithValue("$remoteId", remoteImpressionId);
        if (command.ExecuteNonQuery() != 1)
        {
            throw new InvalidOperationException("The remotely deleted draft could not be removed locally.");
        }
        return draft;
    }

    private void SetState(Guid draftId, DraftState state, string? remoteId, string? error, DateTimeOffset now)
    {
        using SqliteConnection connection = _database.OpenConnection();
        using SqliteTransaction transaction = connection.BeginTransaction();
        UpdateDraft(connection, transaction, draftId, state, remoteId, error, now);
        transaction.Commit();
    }

    private static void UpdateDraft(
        SqliteConnection connection,
        SqliteTransaction transaction,
        Guid draftId,
        DraftState state,
        string? remoteId,
        string? error,
        DateTimeOffset now)
    {
        using var command = connection.CreateCommand();
        command.Transaction = transaction;
        command.CommandText = """
            UPDATE drafts SET
              state = $state,
              remote_impression_id = COALESCE($remoteId, remote_impression_id),
              last_error = $error,
              updated_at = $updatedAt
            WHERE id = $id
            """;
        command.Parameters.AddWithValue("$state", state.ToString());
        command.Parameters.AddWithValue("$remoteId", (object?)remoteId ?? DBNull.Value);
        command.Parameters.AddWithValue("$error", (object?)error ?? DBNull.Value);
        command.Parameters.AddWithValue("$updatedAt", now.ToString("O"));
        command.Parameters.AddWithValue("$id", draftId.ToString("D"));
        if (command.ExecuteNonQuery() != 1)
        {
            throw new InvalidOperationException("The draft does not exist.");
        }
    }

    private static StoredDraft? ReadDraft(SqliteConnection connection, Guid draftId)
    {
        using var command = connection.CreateCommand();
        command.CommandText = "SELECT * FROM drafts WHERE id = $id";
        command.Parameters.AddWithValue("$id", draftId.ToString("D"));
        using SqliteDataReader reader = command.ExecuteReader();
        if (!reader.Read()) return null;
        var draft = new StoredDraft(
            Guid.Parse(reader.GetString(reader.GetOrdinal("id"))),
            Guid.Parse(reader.GetString(reader.GetOrdinal("local_session_id"))),
            reader.GetString(reader.GetOrdinal("client_submission_id")),
            reader.GetString(reader.GetOrdinal("capture_anchor_id")),
            reader.GetString(reader.GetOrdinal("body")),
            ParseDate(reader.GetString(reader.GetOrdinal("captured_at"))),
            reader.GetInt64(reader.GetOrdinal("elapsed_ms")),
            reader.GetInt64(reader.GetOrdinal("active_ms")),
            Enum.Parse<DraftState>(reader.GetString(reader.GetOrdinal("state"))),
            OptionalString(reader, "remote_impression_id"),
            OptionalString(reader, "last_error"),
            []);
        reader.Close();
        return draft with { Assets = ReadAssets(connection, draftId) };
    }

    private static IReadOnlyList<StoredAsset> ReadAssets(SqliteConnection connection, Guid draftId)
    {
        using var command = connection.CreateCommand();
        command.CommandText = "SELECT * FROM assets WHERE draft_id = $id ORDER BY kind";
        command.Parameters.AddWithValue("$id", draftId.ToString("D"));
        using SqliteDataReader reader = command.ExecuteReader();
        var assets = new List<StoredAsset>();
        while (reader.Read())
        {
            assets.Add(new StoredAsset(
                Guid.Parse(reader.GetString(reader.GetOrdinal("id"))),
                draftId,
                reader.GetString(reader.GetOrdinal("kind")),
                reader.GetString(reader.GetOrdinal("relative_path")),
                reader.GetString(reader.GetOrdinal("mime_type")),
                reader.GetInt64(reader.GetOrdinal("size_bytes")),
                reader.GetString(reader.GetOrdinal("sha256")),
                OptionalInt(reader, "duration_ms"),
                OptionalDate(reader, "captured_at"),
                OptionalDate(reader, "clip_started_at"),
                OptionalDate(reader, "clip_ended_at"),
                Enum.Parse<AssetState>(reader.GetString(reader.GetOrdinal("state"))),
                reader.GetInt64(reader.GetOrdinal("is_enabled")) != 0));
        }
        return assets;
    }

    private static object DateValue(DateTimeOffset? value) => value is null ? DBNull.Value : value.Value.ToString("O");

    private static void ValidateText(string text)
    {
        ArgumentNullException.ThrowIfNull(text);
        if (text.Length > 2000)
        {
            throw new ArgumentException("The impression must not exceed 2000 characters.", nameof(text));
        }
    }

    private static string? OptionalString(SqliteDataReader reader, string name)
    {
        int ordinal = reader.GetOrdinal(name);
        return reader.IsDBNull(ordinal) ? null : reader.GetString(ordinal);
    }

    private static int? OptionalInt(SqliteDataReader reader, string name)
    {
        int ordinal = reader.GetOrdinal(name);
        return reader.IsDBNull(ordinal) ? null : reader.GetInt32(ordinal);
    }

    private static DateTimeOffset? OptionalDate(SqliteDataReader reader, string name)
    {
        string? value = OptionalString(reader, name);
        return value is null ? null : ParseDate(value);
    }

    private static DateTimeOffset ParseDate(string value) => DateTimeOffset.Parse(value, CultureInfo.InvariantCulture);
}
