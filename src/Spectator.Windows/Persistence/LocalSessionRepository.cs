using Microsoft.Data.Sqlite;
using Spectator.Core.Sessions;

namespace Spectator.Windows.Persistence;

internal sealed class LocalSessionRepository
{
    private readonly SpectatorDatabase _database;

    internal LocalSessionRepository(SpectatorDatabase database) => _database = database;

    internal void Create(LocalSession session)
    {
        using SqliteConnection connection = _database.OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = """
            INSERT INTO local_sessions (
              id, game_id, remote_session_id, started_at, ended_at, last_checkpoint_at, elapsed_ms, active_ms
            ) VALUES ($id, $gameId, $remoteId, $startedAt, $endedAt, $checkpoint, $elapsed, $active)
            """;
        AddSessionParameters(command, session);
        command.ExecuteNonQuery();
    }

    internal void Checkpoint(LocalSession session)
    {
        using SqliteConnection connection = _database.OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = """
            UPDATE local_sessions SET
              remote_session_id = $remoteId,
              ended_at = $endedAt,
              last_checkpoint_at = $checkpoint,
              elapsed_ms = MAX(elapsed_ms, $elapsed),
              active_ms = MAX(active_ms, $active)
            WHERE id = $id
            """;
        AddSessionParameters(command, session);
        if (command.ExecuteNonQuery() != 1)
        {
            throw new InvalidOperationException("The local session checkpoint target does not exist.");
        }
    }

    internal LocalSession? Get(Guid sessionId)
    {
        using SqliteConnection connection = _database.OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = "SELECT * FROM local_sessions WHERE id = $id";
        command.Parameters.AddWithValue("$id", sessionId.ToString("D"));
        using SqliteDataReader reader = command.ExecuteReader();
        return reader.Read() ? Read(reader) : null;
    }

    internal IReadOnlyList<LocalSession> RecoverOpenSessions()
    {
        using SqliteConnection connection = _database.OpenConnection();
        using SqliteTransaction transaction = connection.BeginTransaction();
        using var select = connection.CreateCommand();
        select.Transaction = transaction;
        select.CommandText = "SELECT * FROM local_sessions WHERE ended_at IS NULL";
        using SqliteDataReader reader = select.ExecuteReader();
        var sessions = new List<LocalSession>();
        while (reader.Read()) sessions.Add(Read(reader));
        reader.Close();

        using var update = connection.CreateCommand();
        update.Transaction = transaction;
        update.CommandText = "UPDATE local_sessions SET ended_at = last_checkpoint_at WHERE ended_at IS NULL";
        update.ExecuteNonQuery();
        transaction.Commit();
        return sessions.Select(session => session with { EndedAt = session.LastCheckpointAt }).ToArray();
    }

    internal void Delete(Guid sessionId)
    {
        using SqliteConnection connection = _database.OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = """
            DELETE FROM local_sessions
            WHERE id = $id AND NOT EXISTS (
              SELECT 1 FROM drafts WHERE local_session_id = $id
            )
            """;
        command.Parameters.AddWithValue("$id", sessionId.ToString("D"));
        if (command.ExecuteNonQuery() != 1)
        {
            throw new InvalidOperationException("Only an unreferenced local session can be deleted.");
        }
    }

    private static void AddSessionParameters(SqliteCommand command, LocalSession session)
    {
        command.Parameters.AddWithValue("$id", session.Id.ToString("D"));
        command.Parameters.AddWithValue("$gameId", session.GameId);
        command.Parameters.AddWithValue("$remoteId", (object?)session.RemoteSessionId ?? DBNull.Value);
        command.Parameters.AddWithValue("$startedAt", session.StartedAt.ToString("O"));
        command.Parameters.AddWithValue("$endedAt", session.EndedAt is null ? DBNull.Value : session.EndedAt.Value.ToString("O"));
        command.Parameters.AddWithValue("$checkpoint", session.LastCheckpointAt.ToString("O"));
        command.Parameters.AddWithValue("$elapsed", session.ElapsedMilliseconds);
        command.Parameters.AddWithValue("$active", session.ActiveMilliseconds);
    }

    private static LocalSession Read(SqliteDataReader reader) => new(
        Guid.Parse(reader.GetString(reader.GetOrdinal("id"))),
        reader.GetString(reader.GetOrdinal("game_id")),
        reader.IsDBNull(reader.GetOrdinal("remote_session_id")) ? null : reader.GetString(reader.GetOrdinal("remote_session_id")),
        DateTimeOffset.Parse(reader.GetString(reader.GetOrdinal("started_at")), CultureInfo.InvariantCulture),
        reader.IsDBNull(reader.GetOrdinal("ended_at"))
            ? null
            : DateTimeOffset.Parse(reader.GetString(reader.GetOrdinal("ended_at")), CultureInfo.InvariantCulture),
        DateTimeOffset.Parse(reader.GetString(reader.GetOrdinal("last_checkpoint_at")), CultureInfo.InvariantCulture),
        reader.GetInt64(reader.GetOrdinal("elapsed_ms")),
        reader.GetInt64(reader.GetOrdinal("active_ms")));
}
