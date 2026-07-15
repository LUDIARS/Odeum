using Microsoft.Data.Sqlite;
using Spectator.Core.Games;

namespace Spectator.Windows.Persistence;

internal sealed class GameTargetRepository
{
    private readonly SpectatorDatabase _database;

    internal GameTargetRepository(SpectatorDatabase database) => _database = database;

    internal void Save(GameTarget target)
    {
        using SqliteConnection connection = _database.OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = """
            INSERT INTO game_targets (
              game_id, display_name, executable_name, title_pattern, is_topmost, updated_at
            ) VALUES ($gameId, $displayName, $executableName, $titlePattern, $isTopmost, $updatedAt)
            ON CONFLICT(game_id) DO UPDATE SET
              display_name = excluded.display_name,
              executable_name = excluded.executable_name,
              title_pattern = excluded.title_pattern,
              is_topmost = excluded.is_topmost,
              updated_at = excluded.updated_at;
            """;
        command.Parameters.AddWithValue("$gameId", target.GameId);
        command.Parameters.AddWithValue("$displayName", target.DisplayName);
        command.Parameters.AddWithValue("$executableName", target.ExecutableName);
        command.Parameters.AddWithValue("$titlePattern", (object?)target.TitlePattern ?? DBNull.Value);
        command.Parameters.AddWithValue("$isTopmost", target.IsTopmost ? 1 : 0);
        command.Parameters.AddWithValue("$updatedAt", target.UpdatedAt.ToString("O"));
        command.ExecuteNonQuery();
    }

    internal IReadOnlyList<GameTarget> List()
    {
        using SqliteConnection connection = _database.OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = "SELECT * FROM game_targets ORDER BY display_name COLLATE NOCASE";
        using SqliteDataReader reader = command.ExecuteReader();
        var targets = new List<GameTarget>();
        while (reader.Read())
        {
            targets.Add(new GameTarget(
                reader.GetString(reader.GetOrdinal("game_id")),
                reader.GetString(reader.GetOrdinal("display_name")),
                reader.GetString(reader.GetOrdinal("executable_name")),
                reader.IsDBNull(reader.GetOrdinal("title_pattern")) ? null : reader.GetString(reader.GetOrdinal("title_pattern")),
                reader.GetInt64(reader.GetOrdinal("is_topmost")) != 0,
                DateTimeOffset.Parse(reader.GetString(reader.GetOrdinal("updated_at")), CultureInfo.InvariantCulture)));
        }
        return targets;
    }
}
