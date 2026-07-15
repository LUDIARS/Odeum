using System.Collections.Concurrent;
using System.Net.WebSockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace Spectator.Windows.Obs;

internal sealed class ObsWebSocketClient : IAsyncDisposable
{
    private const int OutputsEventSubscription = 1 << 6;
    private readonly ClientWebSocket _socket;
    private readonly SemaphoreSlim _sendGate = new(1, 1);
    private readonly CancellationTokenSource _lifetime = new();
    private readonly ConcurrentDictionary<string, TaskCompletionSource<JsonElement>> _pending = new();
    private Task? _receiveTask;
    private bool _disposed;

    private ObsWebSocketClient(ClientWebSocket socket)
    {
        _socket = socket;
    }

    internal event EventHandler<string>? ReplayBufferSaved;

    internal bool IsConnected => !_disposed && _socket.State == WebSocketState.Open;

    internal static async Task<ObsWebSocketClient> ConnectAsync(
        Uri endpoint,
        string password,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(endpoint);
        var socket = new ClientWebSocket();
        socket.Options.KeepAliveInterval = TimeSpan.FromSeconds(20);
        var client = new ObsWebSocketClient(socket);
        try
        {
            await socket.ConnectAsync(endpoint, cancellationToken);
            using JsonDocument hello = await client.ReceiveDocumentAsync(cancellationToken);
            JsonElement helloRoot = hello.RootElement;
            if (helloRoot.GetProperty("op").GetInt32() != 0)
            {
                throw new InvalidDataException("OBS did not send a WebSocket Hello message.");
            }

            JsonElement data = helloRoot.GetProperty("d");
            int rpcVersion = Math.Min(1, data.GetProperty("rpcVersion").GetInt32());
            string? authentication = data.TryGetProperty("authentication", out JsonElement challenge)
                ? CreateAuthentication(password, challenge)
                : null;

            await client.SendJsonAsync(
                new
                {
                    op = 1,
                    d = new
                    {
                        rpcVersion,
                        authentication,
                        eventSubscriptions = OutputsEventSubscription,
                    },
                },
                cancellationToken);

            using JsonDocument identified = await client.ReceiveDocumentAsync(cancellationToken);
            if (identified.RootElement.GetProperty("op").GetInt32() != 2)
            {
                throw new InvalidDataException("OBS rejected the WebSocket identification.");
            }

            client._receiveTask = client.ReceiveLoopAsync(client._lifetime.Token);
            return client;
        }
        catch
        {
            await client.DisposeAsync();
            throw;
        }
    }

    internal async Task<JsonElement> SendRequestAsync(
        string requestType,
        object? requestData,
        CancellationToken cancellationToken)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (!IsConnected)
        {
            throw new InvalidOperationException("OBS WebSocket is not connected.");
        }

        string requestId = Guid.NewGuid().ToString("N");
        var completion = new TaskCompletionSource<JsonElement>(TaskCreationOptions.RunContinuationsAsynchronously);
        if (!_pending.TryAdd(requestId, completion))
        {
            throw new InvalidOperationException("An OBS request identifier collision occurred.");
        }

        try
        {
            await SendJsonAsync(
                new
                {
                    op = 6,
                    d = new { requestType, requestId, requestData },
                },
                cancellationToken);
            return await completion.Task.WaitAsync(cancellationToken);
        }
        finally
        {
            _pending.TryRemove(requestId, out _);
        }
    }

    private static string CreateAuthentication(string password, JsonElement authentication)
    {
        string challenge = authentication.GetProperty("challenge").GetString()
            ?? throw new InvalidDataException("OBS authentication challenge is missing.");
        string salt = authentication.GetProperty("salt").GetString()
            ?? throw new InvalidDataException("OBS authentication salt is missing.");
        string secret = Convert.ToBase64String(SHA256.HashData(Encoding.UTF8.GetBytes(password + salt)));
        return Convert.ToBase64String(SHA256.HashData(Encoding.UTF8.GetBytes(secret + challenge)));
    }

    private async Task ReceiveLoopAsync(CancellationToken cancellationToken)
    {
        Exception? failure = null;
        try
        {
            while (!cancellationToken.IsCancellationRequested && _socket.State == WebSocketState.Open)
            {
                using JsonDocument message = await ReceiveDocumentAsync(cancellationToken);
                RouteMessage(message.RootElement);
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
        }
        catch (Exception exception)
        {
            failure = exception;
        }
        finally
        {
            Exception terminal = failure ?? new WebSocketException("The OBS WebSocket connection closed.");
            foreach (TaskCompletionSource<JsonElement> completion in _pending.Values)
            {
                completion.TrySetException(terminal);
            }
        }
    }

    private void RouteMessage(JsonElement root)
    {
        int operation = root.GetProperty("op").GetInt32();
        JsonElement data = root.GetProperty("d");
        if (operation == 7)
        {
            RouteResponse(data);
            return;
        }

        if (operation == 5
            && string.Equals(data.GetProperty("eventType").GetString(), "ReplayBufferSaved", StringComparison.Ordinal)
            && data.TryGetProperty("eventData", out JsonElement eventData)
            && (eventData.TryGetProperty("savedReplayPath", out JsonElement outputPath)
                || eventData.TryGetProperty("outputPath", out outputPath))
            && outputPath.GetString() is { Length: > 0 } path)
        {
            ReplayBufferSaved?.Invoke(this, path);
        }
    }

    private void RouteResponse(JsonElement data)
    {
        string requestId = data.GetProperty("requestId").GetString()
            ?? throw new InvalidDataException("An OBS response has no request identifier.");
        if (!_pending.TryGetValue(requestId, out TaskCompletionSource<JsonElement>? completion))
        {
            return;
        }

        JsonElement status = data.GetProperty("requestStatus");
        if (!status.GetProperty("result").GetBoolean())
        {
            int code = status.GetProperty("code").GetInt32();
            string comment = status.TryGetProperty("comment", out JsonElement value)
                ? value.GetString() ?? "No detail was supplied."
                : "No detail was supplied.";
            completion.TrySetException(new InvalidOperationException($"OBS request failed ({code}): {comment}"));
            return;
        }

        JsonElement response = data.TryGetProperty("responseData", out JsonElement responseData)
            ? responseData.Clone()
            : default;
        completion.TrySetResult(response);
    }

    private async Task SendJsonAsync(object payload, CancellationToken cancellationToken)
    {
        byte[] bytes = JsonSerializer.SerializeToUtf8Bytes(payload);
        await _sendGate.WaitAsync(cancellationToken);
        try
        {
            await _socket.SendAsync(bytes, WebSocketMessageType.Text, true, cancellationToken);
        }
        finally
        {
            _sendGate.Release();
        }
    }

    private async Task<JsonDocument> ReceiveDocumentAsync(CancellationToken cancellationToken)
    {
        byte[] buffer = new byte[16 * 1024];
        using var stream = new MemoryStream();
        WebSocketReceiveResult result;
        do
        {
            result = await _socket.ReceiveAsync(buffer, cancellationToken);
            if (result.MessageType == WebSocketMessageType.Close)
            {
                throw new WebSocketException("OBS closed the WebSocket connection.");
            }

            stream.Write(buffer, 0, result.Count);
        }
        while (!result.EndOfMessage);

        stream.Position = 0;
        return await JsonDocument.ParseAsync(stream, cancellationToken: cancellationToken);
    }

    public async ValueTask DisposeAsync()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        _lifetime.Cancel();
        if (_socket.State == WebSocketState.Open)
        {
            try
            {
                await _socket.CloseOutputAsync(WebSocketCloseStatus.NormalClosure, "Spectator is closing.", CancellationToken.None);
            }
            catch (WebSocketException)
            {
            }
        }

        if (_receiveTask is not null)
        {
            try
            {
                await _receiveTask;
            }
            catch (OperationCanceledException)
            {
            }
        }

        _socket.Dispose();
        _lifetime.Dispose();
        _sendGate.Dispose();
        GC.SuppressFinalize(this);
    }
}
