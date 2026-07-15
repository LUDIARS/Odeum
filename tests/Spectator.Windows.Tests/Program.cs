using Microsoft.Data.Sqlite;
using System.Globalization;
using System.IO;
using System.Text.Json;
using Spectator.Core.Drafts;
using Spectator.Core.Reactions;
using Spectator.Core.Sessions;
using Spectator.Windows.Configuration;
using Spectator.Windows.Capture;
using Spectator.Windows.Persistence;
using Spectator.Windows.Reactions;
using Spectator.Windows.Obs;
using Spectator.Windows.Importing;
using Spectator.Windows.Storage;

namespace Spectator.Windows.Tests;

internal static class Program
{
    public static int Main(string[] arguments)
    {
        if (arguments.Length == 2 && string.Equals(arguments[0], "--standalone-smoke", StringComparison.Ordinal))
        {
            Console.WriteLine("RUN  Standalone real-video review smoke");
            StandaloneRealVideoSmoke(arguments[1]);
            Console.WriteLine("PASS Standalone real-video review smoke");
            return 0;
        }
        var tests = new (string Name, Action Run)[]
        {
            ("SQLite draft lifecycle preserves remote ownership", DraftLifecycle),
            ("SQLite v1 upgrades to the reaction schema", OlderSchemaUpgrades),
            ("SQLite rejects a newer schema", NewerSchemaIsRejected),
            ("Self-reported reactions persist and export raw data", ReactionRawDataLifecycle),
            ("Local recorded videos import into a review draft", LocalVideoImport),
            ("Capture cleanup preserves referenced and recent files", CaptureCleanup),
            ("Settings allow loopback HTTP and require remote HTTPS", SettingsSecurity),
            ("OBS replay paths stay inside the configured directory", ObsReplayPathSecurity),
        };
        foreach ((string name, Action run) in tests.Where(test =>
                     arguments.Length == 0 || test.Name.Contains(arguments[0], StringComparison.OrdinalIgnoreCase)))
        {
            Console.WriteLine($"RUN  {name}");
            Console.Out.Flush();
            run();
            Console.WriteLine($"PASS {name}");
        }
        return 0;
    }

    private static void StandaloneRealVideoSmoke(string sourcePath)
    {
        using var temporary = new TemporaryDirectory();
        string databasePath = System.IO.Path.Combine(temporary.Path, "spectator.db");
        string capturePath = System.IO.Path.Combine(temporary.Path, "captures");
        var database = new SpectatorDatabase(databasePath);
        database.Initialize();
        var sessions = new LocalSessionRepository(database);
        var drafts = new DraftRepository(database);
        var reactions = new ReactionRepository(database);
        var store = new CaptureStore(capturePath);
        Guid sessionId = Guid.Parse("7397441d-f242-4d40-ab41-da6cba2c13d0");
        Guid draftId = Guid.Parse("ac5b60df-609b-4c24-92ae-078fdbe5abe6");
        Guid assetId = Guid.Parse("c5047c26-056e-4017-b216-64a2fdc614c3");
        DateTimeOffset now = new(2026, 7, 15, 1, 0, 0, TimeSpan.Zero);
        var importer = new LocalVideoImporter(
            store,
            drafts,
            sessions,
            new SequenceIdentifierSource(sessionId, draftId, assetId),
            new FixedTimeSource(now),
            new WindowsMediaDurationSource());

        StoredDraft imported = importer.ImportAsync(sourcePath, "standalone-smoke", CancellationToken.None)
            .GetAwaiter()
            .GetResult();
        int duration = imported.Assets.Single().DurationMilliseconds
            ?? throw new InvalidDataException("The real video duration was not persisted.");
        Check.True(duration >= 1_500, "The generated smoke video is too short");
        reactions.Add(new ReactionAnnotation(
            Guid.Parse("e9b7de18-ed09-44c6-9f2e-99a5d769e607"),
            draftId,
            500,
            ReactionKind.Positive,
            "ここ良かった",
            now.AddSeconds(1)));
        reactions.Add(new ReactionAnnotation(
            Guid.Parse("94253978-2bf4-493c-9042-3b625f2fc709"),
            draftId,
            1_500,
            ReactionKind.Negative,
            "ここ悪かった",
            now.AddSeconds(2)));

        var reopenedDatabase = new SpectatorDatabase(databasePath);
        reopenedDatabase.Initialize();
        StoredDraft reopened = new DraftRepository(reopenedDatabase).Get(draftId)
            ?? throw new InvalidOperationException("The imported draft was not restored after reopening SQLite.");
        IReadOnlyList<ReactionAnnotation> reopenedReactions = new ReactionRepository(reopenedDatabase).List(draftId);
        string outputPath = System.IO.Path.Combine(temporary.Path, "reaction-raw.json");
        new ReactionRawDataExporter().Write(outputPath, "standalone-smoke", reopened, reopenedReactions);
        using JsonDocument output = JsonDocument.Parse(File.ReadAllText(outputPath));
        JsonElement root = output.RootElement;
        Check.Equal(2, root.GetProperty("utterances").GetArrayLength(), "The reopened raw data count differs");
        Check.Equal("positive", root.GetProperty("utterances")[0].GetProperty("reactionKind").GetString(), "The positive stamp was lost");
        Check.Equal("negative", root.GetProperty("utterances")[1].GetProperty("reactionKind").GetString(), "The negative stamp was lost");
        Check.Equal(64, root.GetProperty("sourceRef").GetString()?.Length ?? 0, "The real video SHA-256 was not exported");
        Check.True(File.Exists(sourcePath), "The source video was moved during standalone import");
        Check.True(File.Exists(System.IO.Path.Combine(capturePath, reopened.Assets.Single().RelativePath)), "The managed video copy is missing after reopen");
    }

    private static void DraftLifecycle()
    {
        using var temporary = new TemporaryDirectory();
        var database = new SpectatorDatabase(System.IO.Path.Combine(temporary.Path, "spectator.db"));
        database.Initialize();
        var sessions = new LocalSessionRepository(database);
        var drafts = new DraftRepository(database);
        Guid sessionId = Guid.NewGuid();
        DateTimeOffset now = new(2026, 7, 13, 12, 0, 0, TimeSpan.Zero);
        sessions.Create(new LocalSession(sessionId, "game", "remote-session", now, now, now, 20_000, 18_000));
        Guid draftId = Guid.NewGuid();
        string remoteId = Guid.NewGuid().ToString("D");
        drafts.Save(new StoredDraft(
            draftId,
            sessionId,
            "submission",
            "anchor",
            "感想",
            now,
            20_000,
            18_000,
            DraftState.Submitted,
            remoteId,
            null,
            []), now);

        StoredDraft loaded = drafts.ListRecent().Single();
        Check.Equal(remoteId, loaded.RemoteImpressionId, "Remote impression identifier was not restored");
        Check.Throws<InvalidOperationException>(
            () => drafts.DeleteAfterRemote(draftId, Guid.NewGuid().ToString("D")),
            "A mismatched remote deletion was accepted");
        StoredDraft deleted = drafts.DeleteAfterRemote(draftId, remoteId);
        Check.Equal(draftId, deleted.Id, "The wrong draft was deleted");
        Check.Equal(0, drafts.ListRecent().Count, "The draft remained after remote deletion");
    }

    private static void NewerSchemaIsRejected()
    {
        using var temporary = new TemporaryDirectory();
        string path = System.IO.Path.Combine(temporary.Path, "spectator.db");
        var database = new SpectatorDatabase(path);
        database.Initialize();
        using (SqliteConnection connection = database.OpenConnection())
        using (SqliteCommand command = connection.CreateCommand())
        {
            command.CommandText = "PRAGMA user_version = 4;";
            command.ExecuteNonQuery();
        }
        Check.Throws<InvalidDataException>(database.Initialize, "A newer database schema was accepted");
    }

    private static void OlderSchemaUpgrades()
    {
        using var temporary = new TemporaryDirectory();
        string path = System.IO.Path.Combine(temporary.Path, "spectator.db");
        var database = new SpectatorDatabase(path);
        database.Initialize();
        using (SqliteConnection connection = database.OpenConnection())
        using (SqliteCommand command = connection.CreateCommand())
        {
            command.CommandText = "DROP TABLE reaction_annotations; PRAGMA user_version = 1;";
            command.ExecuteNonQuery();
        }

        database.Initialize();
        using SqliteConnection upgraded = database.OpenConnection();
        using SqliteCommand versionCommand = upgraded.CreateCommand();
        versionCommand.CommandText = "PRAGMA user_version;";
        Check.Equal(3L, Convert.ToInt64(versionCommand.ExecuteScalar(), CultureInfo.InvariantCulture), "The schema version was not upgraded");
        using SqliteCommand tableCommand = upgraded.CreateCommand();
        tableCommand.CommandText = "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'reaction_annotations'";
        Check.Equal(1L, Convert.ToInt64(tableCommand.ExecuteScalar(), CultureInfo.InvariantCulture), "The reaction table was not created");
        using SqliteCommand columnCommand = upgraded.CreateCommand();
        columnCommand.CommandText = "SELECT COUNT(*) FROM pragma_table_info('reaction_annotations') WHERE name = 'kind'";
        Check.Equal(1L, Convert.ToInt64(columnCommand.ExecuteScalar(), CultureInfo.InvariantCulture), "The reaction kind column was not created");
    }

    private static void ReactionRawDataLifecycle()
    {
        using var temporary = new TemporaryDirectory();
        var database = new SpectatorDatabase(System.IO.Path.Combine(temporary.Path, "spectator.db"));
        database.Initialize();
        var sessions = new LocalSessionRepository(database);
        var drafts = new DraftRepository(database);
        var reactions = new ReactionRepository(database);
        DateTimeOffset now = new(2026, 7, 14, 3, 0, 0, TimeSpan.Zero);
        Guid sessionId = Guid.Parse("d98ab2cb-58e5-41f3-bdb6-9c65893adfe8");
        Guid draftId = Guid.Parse("c47fe09c-235c-4d35-bf8e-d7c18ff593e0");
        sessions.Create(new LocalSession(sessionId, "game", null, now, now, now, 10_000, 10_000));
        drafts.Save(new StoredDraft(
            draftId,
            sessionId,
            "submission",
            "anchor",
            string.Empty,
            now,
            10_000,
            10_000,
            DraftState.Ready,
            null,
            null,
            [new StoredAsset(
                Guid.Parse("da1c7fef-7a72-4f25-a681-594c311b10c1"),
                draftId,
                "video",
                "video.mp4",
                "video/mp4",
                3,
                "abc123",
                10_000,
                null,
                now,
                now.AddSeconds(10),
                AssetState.Local,
                true)]), now);
        var later = new ReactionAnnotation(
            Guid.Parse("157adf7d-357e-4316-b70b-9d3dfe6f4ff9"),
            draftId,
            8_000,
            ReactionKind.Negative,
            "悔しい",
            now.AddSeconds(2));
        var earlier = new ReactionAnnotation(
            Guid.Parse("ea2e53b0-9bfc-4f91-893e-504616300bd7"),
            draftId,
            2_000,
            ReactionKind.Positive,
            "驚いた！",
            now.AddSeconds(1));
        reactions.Add(later);
        reactions.Add(earlier);

        IReadOnlyList<ReactionAnnotation> loaded = reactions.List(draftId);
        Check.Equal(2_000L, loaded[0].VideoOffsetMilliseconds, "Reactions were not ordered by video time");
        Check.Equal("驚いた！", loaded[0].Content, "The self-reported content was changed");
        Check.Equal(ReactionKind.Positive, loaded[0].Kind, "The positive stamp kind was not restored");

        StoredDraft draft = drafts.Get(draftId)
            ?? throw new InvalidOperationException("The test draft was not restored.");
        var exporter = new ReactionRawDataExporter();
        Check.Throws<InvalidOperationException>(
            () => exporter.CreateDocument("game", draft, []),
            "Empty reaction raw data was exported");
        string output = System.IO.Path.Combine(temporary.Path, "reactions.json");
        exporter.Write(output, "game", draft, loaded);
        using JsonDocument document = JsonDocument.Parse(File.ReadAllText(output));
        JsonElement root = document.RootElement;
        Check.Equal(
            ReactionRawDataExporter.CurrentSchemaVersion,
            root.GetProperty("schemaVersion").GetString(),
            "The raw data schema version differs");
        Check.Equal("self_report", root.GetProperty("sourceKind").GetString(), "The source kind differs");
        Check.Equal("game", root.GetProperty("gameId").GetString(), "The exported game identifier differs");
        Check.Equal("abc123", root.GetProperty("sourceRef").GetString(), "The exported source reference differs");
        JsonElement entries = root.GetProperty("utterances");
        Check.Equal(2, entries.GetArrayLength(), "The raw data reaction count differs");
        Check.Equal(2_000L, entries[0].GetProperty("videoOffsetMs").GetInt64(), "The exported offset differs");
        Check.Equal("positive", entries[0].GetProperty("reactionKind").GetString(), "The exported reaction kind differs");
        Check.Equal("驚いた！", entries[0].GetProperty("content").GetString(), "The exported content differs");

        var outsideVideo = earlier with { VideoOffsetMilliseconds = 10_001 };
        Check.Throws<InvalidDataException>(
            () => exporter.CreateDocument("game", draft, [outsideVideo]),
            "A reaction outside the video duration was exported");

        drafts.DeleteLocal(draftId);
        Check.Equal(0, reactions.List(draftId).Count, "Reaction annotations remained after deleting the draft");
    }

    private static void LocalVideoImport()
    {
        using var temporary = new TemporaryDirectory();
        string databasePath = System.IO.Path.Combine(temporary.Path, "spectator.db");
        string capturePath = System.IO.Path.Combine(temporary.Path, "captures");
        string sourcePath = System.IO.Path.Combine(temporary.Path, "recording.mp4");
        File.WriteAllBytes(sourcePath, [1, 2, 3, 4]);
        var database = new SpectatorDatabase(databasePath);
        database.Initialize();
        var sessions = new LocalSessionRepository(database);
        var drafts = new DraftRepository(database);
        var store = new CaptureStore(capturePath);
        Guid sessionId = Guid.Parse("d502c3c2-d819-482a-b6b2-85234952a815");
        Guid draftId = Guid.Parse("526a35bb-8c81-40d8-b5f5-802941332d70");
        Guid assetId = Guid.Parse("ffbc29f0-a367-43d2-91b0-8a22fc85eb3e");
        DateTimeOffset now = new(2026, 7, 14, 5, 0, 0, TimeSpan.Zero);
        var importer = new LocalVideoImporter(
            store,
            drafts,
            sessions,
            new SequenceIdentifierSource(sessionId, draftId, assetId),
            new FixedTimeSource(now),
            new FixedMediaDurationSource(TimeSpan.FromSeconds(42)));

        StoredDraft imported = importer.ImportAsync(sourcePath, "local-game", CancellationToken.None)
            .GetAwaiter()
            .GetResult();
        Check.Equal(draftId, imported.Id, "The imported draft identifier differs");
        StoredAsset video = imported.Assets.Single();
        Check.Equal(42_000, video.DurationMilliseconds, "The imported duration differs");
        Check.Equal("video/mp4", video.MimeType, "The imported MIME type differs");
        Check.True(File.Exists(System.IO.Path.Combine(store.RootDirectory, video.RelativePath)), "The managed video copy is missing");
        Check.True(File.Exists(sourcePath), "The source video was moved instead of copied");
        Check.Equal("local-game", sessions.Get(sessionId)?.GameId, "The imported game identifier differs");
        Check.Equal(draftId, drafts.ListRecent().Single().Id, "The imported draft was not persisted");
    }

    private static void CaptureCleanup()
    {
        using var temporary = new TemporaryDirectory();
        var store = new CaptureStore(temporary.Path);
        string known = System.IO.Path.Combine(temporary.Path, "known.jpg");
        string orphan = System.IO.Path.Combine(temporary.Path, "orphan.tmp");
        string recent = System.IO.Path.Combine(temporary.Path, "recent.tmp");
        File.WriteAllText(known, "known");
        File.WriteAllText(orphan, "old");
        File.WriteAllText(recent, "recent");
        File.SetLastWriteTimeUtc(known, DateTime.UtcNow.AddHours(-2));
        File.SetLastWriteTimeUtc(orphan, DateTime.UtcNow.AddHours(-2));
        int deleted = store.CleanupOrphans(
            new HashSet<string>(StringComparer.OrdinalIgnoreCase) { "known.jpg" },
            DateTimeOffset.UtcNow.AddHours(-1));
        Check.Equal(1, deleted, "The orphan cleanup count differs");
        Check.True(File.Exists(known), "A referenced capture was deleted");
        Check.True(File.Exists(recent), "A recent temporary capture was deleted");
        Check.True(!File.Exists(orphan), "An old orphan was not deleted");
    }

    private static void SettingsSecurity()
    {
        using var temporary = new TemporaryDirectory();
        string standalone = System.IO.Path.Combine(temporary.Path, "standalone.json");
        File.WriteAllText(standalone, """
            {
              "integrationMode": "standalone",
              "obsWebSocketUrl": "ws://127.0.0.1:4455",
              "captureHotKey": "Ctrl+Shift+F8",
              "screenshotFormat": "jpeg",
              "videoBackend": "obs",
              "replaySeconds": 20
            }
            """);
        AppSettings standaloneSettings = AppSettings.Load(standalone);
        Check.True(!standaloneSettings.IsVolputasEnabled, "Standalone mode enabled Volputas");
        Check.Equal<Uri?>(null, standaloneSettings.VolputasApiBaseUrl, "Standalone mode required a Volputas API URL");

        string valid = System.IO.Path.Combine(temporary.Path, "valid.json");
        File.WriteAllText(valid, """
            {
              "integrationMode": "volputas",
              "volputasApiBaseUrl": "http://127.0.0.1:8892",
              "volputasWebBaseUrl": "https://volputas.example",
              "loginProvider": "google",
              "obsWebSocketUrl": "ws://127.0.0.1:4455",
              "captureHotKey": "Ctrl+Shift+F8",
              "screenshotFormat": "jpeg",
              "videoBackend": "obs",
              "replaySeconds": 20
            }
            """);
        AppSettings settings = AppSettings.Load(valid);
        Check.Equal("volputas.example", settings.VolputasWebBaseUrl?.Host, "The secure web URL was not loaded");

        string invalid = System.IO.Path.Combine(temporary.Path, "invalid.json");
        File.WriteAllText(invalid, File.ReadAllText(valid).Replace(
            "http://127.0.0.1:8892",
            "http://volputas.example",
            StringComparison.Ordinal));
        Check.Throws<InvalidDataException>(
            () => AppSettings.Load(invalid),
            "Plain HTTP was accepted for a non-loopback API");

        string unsupported = System.IO.Path.Combine(temporary.Path, "unsupported.json");
        File.WriteAllText(unsupported, File.ReadAllText(valid).Replace(
            "\"videoBackend\": \"obs\"",
            "\"videoBackend\": \"builtin\"",
            StringComparison.Ordinal));
        Check.Throws<InvalidDataException>(
            () => AppSettings.Load(unsupported),
            "An unqualified built-in video backend was accepted");
    }

    private static void ObsReplayPathSecurity()
    {
        using var temporary = new TemporaryDirectory();
        string outputDirectory = System.IO.Path.Combine(temporary.Path, "obs");
        Directory.CreateDirectory(outputDirectory);
        ObsVideoCaptureBackend.VerifyDirectoryWritable(outputDirectory);
        string replay = System.IO.Path.Combine(outputDirectory, "replay.mp4");
        File.WriteAllBytes(replay, [1, 2, 3]);
        Check.Equal(
            System.IO.Path.GetFullPath(replay),
            ObsVideoCaptureBackend.ValidateReplayPath(replay, outputDirectory),
            "A valid OBS replay path was changed");
        string escaped = System.IO.Path.Combine(temporary.Path, "private.txt");
        File.WriteAllText(escaped, "private");
        Check.Throws<InvalidDataException>(
            () => ObsVideoCaptureBackend.ValidateReplayPath(escaped, outputDirectory),
            "A replay path outside the OBS directory was accepted");
    }
}
