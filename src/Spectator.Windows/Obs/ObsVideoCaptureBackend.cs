using System.Text.Json;
using Spectator.Core.Capture;
using Spectator.Core.Time;
using Spectator.Windows.Capture;

namespace Spectator.Windows.Obs;

internal sealed class ObsVideoCaptureBackend : IVideoCaptureBackend
{
    private readonly ObsWebSocketClient _client;
    private readonly ITimeSource _timeSource;
    private string? _recordDirectory;
    private string? _currentScene;

    internal ObsVideoCaptureBackend(ObsWebSocketClient client, ITimeSource timeSource)
    {
        _client = client;
        _timeSource = timeSource;
    }

    public string Name => "OBS Replay Buffer";

    internal async Task StartReplayBufferAsync(CancellationToken cancellationToken)
    {
        JsonElement version = await _client.SendRequestAsync("GetVersion", null, cancellationToken);
        string availableRequests = version.GetProperty("availableRequests").GetString() ?? string.Empty;
        string[] requests = availableRequests.Split(',').Select(request => request.Trim()).ToArray();
        foreach (string required in new[]
                 {
                     "StartReplayBuffer", "SaveReplayBuffer", "GetReplayBufferStatus",
                     "GetLastReplayBufferReplay", "GetRecordDirectory", "GetCurrentProgramScene", "GetSceneItemList",
                 })
        {
            if (!requests.Contains(required, StringComparer.Ordinal))
            {
                throw new NotSupportedException($"OBS does not support {required}.");
            }
        }
        JsonElement directoryResponse = await _client.SendRequestAsync("GetRecordDirectory", null, cancellationToken);
        _recordDirectory = Path.GetFullPath(directoryResponse.GetProperty("recordDirectory").GetString()
            ?? throw new InvalidDataException("OBS did not report its recording directory."));
        VerifyDirectoryWritable(_recordDirectory);

        JsonElement sceneResponse = await _client.SendRequestAsync("GetCurrentProgramScene", null, cancellationToken);
        _currentScene = sceneResponse.GetProperty("currentProgramSceneName").GetString();
        if (string.IsNullOrWhiteSpace(_currentScene))
        {
            throw new InvalidDataException("OBS has no current program scene.");
        }
        JsonElement sceneItems = await _client.SendRequestAsync(
            "GetSceneItemList",
            new { sceneName = _currentScene },
            cancellationToken);
        if (sceneItems.GetProperty("sceneItems").GetArrayLength() == 0)
        {
            throw new InvalidDataException("The current OBS program scene has no sources.");
        }

        VideoBackendStatus status = await GetStatusAsync(cancellationToken);
        if (!status.IsReady)
        {
            _ = await _client.SendRequestAsync("StartReplayBuffer", null, cancellationToken);
        }
        status = await GetStatusAsync(cancellationToken);
        if (!status.IsReady) throw new InvalidOperationException("OBS Replay Buffer did not start.");
    }

    public async Task<VideoBackendStatus> GetStatusAsync(CancellationToken cancellationToken)
    {
        if (!_client.IsConnected)
        {
            return new VideoBackendStatus(false, "OBS WebSocket is disconnected.");
        }

        JsonElement response = await _client.SendRequestAsync("GetReplayBufferStatus", null, cancellationToken);
        bool active = response.GetProperty("outputActive").GetBoolean();
        string detail = active ? "Replay Buffer is active." : "Replay Buffer is stopped.";
        if (_currentScene is not null) detail += $" Scene: {_currentScene}.";
        if (_recordDirectory is not null) detail += $" Output: {_recordDirectory}";
        return new VideoBackendStatus(active, detail);
    }

    public async Task<VideoClip> SaveReplayAsync(CaptureAnchor anchor, CancellationToken cancellationToken)
    {
        var saved = new TaskCompletionSource<string>(TaskCreationOptions.RunContinuationsAsynchronously);
        void OnSaved(object? sender, string path) => saved.TrySetResult(path);

        _client.ReplayBufferSaved += OnSaved;
        try
        {
            DateTimeOffset clipEndedAt = anchor.CapturedAt;
            _ = await _client.SendRequestAsync("SaveReplayBuffer", null, cancellationToken);
            string path;
            try
            {
                path = await saved.Task.WaitAsync(TimeSpan.FromSeconds(15), cancellationToken);
            }
            catch (TimeoutException)
            {
                JsonElement lastReplay = await _client.SendRequestAsync("GetLastReplayBufferReplay", null, cancellationToken);
                path = lastReplay.GetProperty("savedReplayPath").GetString()
                    ?? throw new InvalidDataException("OBS did not report the saved replay path.");
            }
            path = ValidateReplayPath(path, _recordDirectory
                ?? throw new InvalidOperationException("OBS diagnostics have not completed."));
            TimeSpan duration = MediaDurationProbe.GetDuration(path);
            if (duration <= TimeSpan.Zero || duration > TimeSpan.FromSeconds(30.5))
            {
                throw new InvalidDataException("The OBS replay duration is missing or exceeds 30 seconds.");
            }
            return new VideoClip(path, clipEndedAt - duration, clipEndedAt, _timeSource.GetUtcNow(), duration);
        }
        finally
        {
            _client.ReplayBufferSaved -= OnSaved;
        }
    }

    public ValueTask DisposeAsync() => _client.DisposeAsync();

    internal static string ValidateReplayPath(string path, string recordDirectory)
    {
        string fullPath = Path.GetFullPath(path);
        string root = Path.GetFullPath(recordDirectory)
            .TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
            + Path.DirectorySeparatorChar;
        if (!fullPath.StartsWith(root, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException("OBS reported a replay outside its configured recording directory.");
        }
        var file = new FileInfo(fullPath);
        if (!file.Exists || file.Length <= 0) throw new InvalidDataException("The OBS replay file is missing or empty.");
        if (file.Length > 200L * 1024 * 1024) throw new InvalidDataException("The OBS replay exceeds 200 MiB.");
        return fullPath;
    }

    internal static void VerifyDirectoryWritable(string directory)
    {
        if (!Directory.Exists(directory)) throw new DirectoryNotFoundException("The OBS recording directory does not exist.");
        string probe = Path.Combine(directory, $".spectator-write-test-{Guid.NewGuid():N}.tmp");
        try
        {
            using FileStream stream = new(probe, FileMode.CreateNew, FileAccess.Write, FileShare.None, 1, FileOptions.WriteThrough);
            stream.WriteByte(0);
        }
        finally
        {
            if (File.Exists(probe)) File.Delete(probe);
        }
    }
}
