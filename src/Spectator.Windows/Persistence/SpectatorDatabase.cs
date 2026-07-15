using Microsoft.Data.Sqlite;

namespace Spectator.Windows.Persistence;

internal sealed class SpectatorDatabase
{
    private const long CurrentSchemaVersion = 3;
    private readonly string _connectionString;

    internal SpectatorDatabase(string databasePath)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(databasePath);
        Directory.CreateDirectory(Path.GetDirectoryName(databasePath)
            ?? throw new InvalidOperationException("The database directory is invalid."));
        _connectionString = new SqliteConnectionStringBuilder
        {
            DataSource = databasePath,
            Mode = SqliteOpenMode.ReadWriteCreate,
            Cache = SqliteCacheMode.Shared,
            Pooling = true,
        }.ToString();
    }

    internal SqliteConnection OpenConnection()
    {
        var connection = new SqliteConnection(_connectionString);
        connection.Open();
        using var command = connection.CreateCommand();
        command.CommandText = "PRAGMA foreign_keys = ON; PRAGMA busy_timeout = 5000;";
        command.ExecuteNonQuery();
        return connection;
    }

    internal void Initialize()
    {
        using SqliteConnection connection = OpenConnection();
        long version;
        using (var versionCommand = connection.CreateCommand())
        {
            versionCommand.CommandText = "PRAGMA user_version;";
            version = Convert.ToInt64(versionCommand.ExecuteScalar(), CultureInfo.InvariantCulture);
            if (version > CurrentSchemaVersion)
            {
                throw new InvalidDataException(
                    $"The Spectator database schema version {version} is newer than supported version {CurrentSchemaVersion}.");
            }
        }
        using var command = connection.CreateCommand();
        command.CommandText = """
            PRAGMA journal_mode = WAL;
            PRAGMA synchronous = FULL;

            CREATE TABLE IF NOT EXISTS game_targets (
              game_id TEXT PRIMARY KEY,
              display_name TEXT NOT NULL,
              executable_name TEXT NOT NULL,
              title_pattern TEXT,
              is_topmost INTEGER NOT NULL,
              updated_at TEXT NOT NULL
            );

            CREATE TABLE IF NOT EXISTS local_sessions (
              id TEXT PRIMARY KEY,
              game_id TEXT NOT NULL,
              remote_session_id TEXT,
              started_at TEXT NOT NULL,
              ended_at TEXT,
              last_checkpoint_at TEXT NOT NULL,
              elapsed_ms INTEGER NOT NULL,
              active_ms INTEGER NOT NULL,
              CHECK (elapsed_ms >= 0 AND active_ms >= 0 AND active_ms <= elapsed_ms)
            );

            CREATE TABLE IF NOT EXISTS drafts (
              id TEXT PRIMARY KEY,
              local_session_id TEXT NOT NULL REFERENCES local_sessions(id),
              client_submission_id TEXT NOT NULL UNIQUE,
              capture_anchor_id TEXT NOT NULL,
              body TEXT NOT NULL,
              captured_at TEXT NOT NULL,
              elapsed_ms INTEGER NOT NULL,
              active_ms INTEGER NOT NULL,
              state TEXT NOT NULL,
              remote_impression_id TEXT,
              last_error TEXT,
              created_at TEXT NOT NULL,
              updated_at TEXT NOT NULL,
              CHECK (elapsed_ms >= 0 AND active_ms >= 0 AND active_ms <= elapsed_ms)
            );

            CREATE TABLE IF NOT EXISTS assets (
              id TEXT PRIMARY KEY,
              draft_id TEXT NOT NULL REFERENCES drafts(id) ON DELETE CASCADE,
              kind TEXT NOT NULL,
              relative_path TEXT NOT NULL,
              mime_type TEXT NOT NULL,
              size_bytes INTEGER NOT NULL,
              sha256 TEXT NOT NULL,
              duration_ms INTEGER,
              captured_at TEXT,
              clip_started_at TEXT,
              clip_ended_at TEXT,
              state TEXT NOT NULL,
              is_enabled INTEGER NOT NULL
            );

            CREATE TABLE IF NOT EXISTS sync_jobs (
              draft_id TEXT PRIMARY KEY REFERENCES drafts(id) ON DELETE CASCADE,
              attempts INTEGER NOT NULL DEFAULT 0,
              next_attempt_at TEXT NOT NULL,
              last_error TEXT,
              is_permanent_failure INTEGER NOT NULL DEFAULT 0
            );

            CREATE TABLE IF NOT EXISTS reaction_annotations (
              id TEXT PRIMARY KEY,
              draft_id TEXT NOT NULL REFERENCES drafts(id) ON DELETE CASCADE,
              video_offset_ms INTEGER NOT NULL,
              content TEXT NOT NULL,
              recorded_at TEXT NOT NULL,
              CHECK (video_offset_ms >= 0),
              CHECK (length(content) BETWEEN 1 AND 2000)
            );

            CREATE INDEX IF NOT EXISTS idx_sync_jobs_due
              ON sync_jobs(is_permanent_failure, next_attempt_at);
            CREATE INDEX IF NOT EXISTS idx_drafts_state
              ON drafts(state, captured_at DESC);
            CREATE INDEX IF NOT EXISTS idx_reaction_annotations_timeline
              ON reaction_annotations(draft_id, video_offset_ms, recorded_at);

            """;
        command.ExecuteNonQuery();

        using SqliteTransaction transaction = connection.BeginTransaction();
        if (version < 3)
        {
            using var migrate = connection.CreateCommand();
            migrate.Transaction = transaction;
            migrate.CommandText = """
                ALTER TABLE reaction_annotations
                ADD COLUMN kind TEXT NOT NULL DEFAULT 'Comment'
                  CHECK (kind IN ('Comment', 'Positive', 'Negative'));
                """;
            migrate.ExecuteNonQuery();
        }
        using (var updateVersion = connection.CreateCommand())
        {
            updateVersion.Transaction = transaction;
            updateVersion.CommandText = "PRAGMA user_version = 3;";
            updateVersion.ExecuteNonQuery();
        }
        transaction.Commit();
    }
}
