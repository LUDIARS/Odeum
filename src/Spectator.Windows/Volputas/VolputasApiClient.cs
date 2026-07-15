using System.Net.Http.Headers;
using System.Text.Json;
using Spectator.Core.Drafts;
using Spectator.Core.Security;

namespace Spectator.Windows.Volputas;

internal sealed class VolputasApiClient : IDisposable
{
    private readonly HttpClient _httpClient;
    private readonly Uri _baseUri;
    private readonly ISecretStore _secretStore;
    private readonly SemaphoreSlim _refreshGate = new(1, 1);
    private string? _accessToken;
    private bool _disposed;

    internal VolputasApiClient(HttpClient httpClient, Uri baseUri, ISecretStore secretStore)
    {
        _httpClient = httpClient;
        _baseUri = baseUri;
        _secretStore = secretStore;
    }

    internal bool IsAuthenticated => _accessToken is not null;

    internal void SetTokens(TokenPair tokens)
    {
        _accessToken = tokens.AccessToken;
        _secretStore.Write(NativeLoginCoordinator.RefreshTokenCredential, tokens.RefreshToken);
    }

    internal async Task<bool> RestoreSessionAsync(CancellationToken cancellationToken)
    {
        if (_secretStore.Read(NativeLoginCoordinator.RefreshTokenCredential) is null)
        {
            return false;
        }
        try
        {
            await RefreshAsync(cancellationToken);
            return true;
        }
        catch (VolputasApiException exception) when (exception.StatusCode == HttpStatusCode.Unauthorized)
        {
            _secretStore.Delete(NativeLoginCoordinator.RefreshTokenCredential);
            return false;
        }
    }

    internal async Task<string> StartSessionAsync(string gameId, CancellationToken cancellationToken)
    {
        JsonElement payload = JsonSerializer.SerializeToElement(new
        {
            game_id = gameId,
            metadata = new { client = "spectator", platform = "windows" },
        });
        ApiEnvelope<SessionResponse> envelope = await SendJsonAsync<SessionResponse>(
            HttpMethod.Post,
            "/api/v1/sessions",
            payload,
            cancellationToken);
        return envelope.Data?.Id ?? throw new InvalidDataException("Volputas omitted the session identifier.");
    }

    internal async Task HeartbeatAsync(
        string sessionId,
        long elapsedMilliseconds,
        long activeMilliseconds,
        DateTimeOffset occurredAt,
        CancellationToken cancellationToken)
    {
        JsonElement payload = JsonSerializer.SerializeToElement(new
        {
            elapsed_ms = elapsedMilliseconds,
            active_ms = activeMilliseconds,
            occurred_at = occurredAt.ToString("O"),
        });
        _ = await SendJsonAsync<JsonElement>(
            HttpMethod.Post,
            $"/api/v1/sessions/{Uri.EscapeDataString(sessionId)}/heartbeat",
            payload,
            cancellationToken);
    }

    internal async Task EndSessionAsync(string sessionId, CancellationToken cancellationToken)
    {
        _ = await SendJsonAsync<JsonElement>(
            HttpMethod.Patch,
            $"/api/v1/sessions/{Uri.EscapeDataString(sessionId)}",
            null,
            cancellationToken);
    }

    internal async Task<RemoteImpression> CreateImpressionAsync(
        string sessionId,
        StoredDraft draft,
        CancellationToken cancellationToken)
    {
        object[] assets = draft.Assets.Where(asset => asset.IsEnabled).Select(asset => new
        {
            client_asset_id = asset.Id.ToString("N"),
            kind = asset.Kind,
            mime_type = asset.MimeType,
            size_bytes = asset.SizeBytes,
            sha256 = asset.Sha256,
            duration_ms = asset.DurationMilliseconds,
            captured_at = asset.CapturedAt?.ToString("O"),
            clip_started_at = asset.ClipStartedAt?.ToString("O"),
            clip_ended_at = asset.ClipEndedAt?.ToString("O"),
        }).Cast<object>().ToArray();
        JsonElement payload = JsonSerializer.SerializeToElement(new
        {
            client_submission_id = draft.ClientSubmissionId,
            capture_anchor_id = draft.CaptureAnchorId,
            text = draft.Text,
            captured_at = draft.CapturedAt.ToString("O"),
            playtime = new
            {
                elapsed_ms = draft.ElapsedMilliseconds,
                active_ms = draft.ActiveMilliseconds,
            },
            client = new { name = "spectator", version = "0.1.0", platform = "windows" },
            assets,
        });
        return (await SendJsonAsync<RemoteImpression>(
            HttpMethod.Post,
            $"/api/v1/sessions/{Uri.EscapeDataString(sessionId)}/impressions",
            payload,
            cancellationToken,
            new Dictionary<string, string> { ["Idempotency-Key"] = draft.ClientSubmissionId })).Data
            ?? throw new InvalidDataException("Volputas omitted the impression response.");
    }

    internal async Task UploadAssetsAsync(
        RemoteImpression impression,
        StoredDraft draft,
        string captureRoot,
        CancellationToken cancellationToken)
    {
        Dictionary<string, StoredAsset> localAssets = draft.Assets
            .Where(asset => asset.IsEnabled)
            .ToDictionary(asset => asset.Id.ToString("N"), StringComparer.Ordinal);
        foreach (RemoteAsset remoteAsset in impression.Assets)
        {
            if (remoteAsset.Upload is null || !localAssets.TryGetValue(remoteAsset.ClientAssetId, out StoredAsset? asset))
            {
                continue;
            }
            string absolutePath = Path.GetFullPath(Path.Combine(captureRoot, asset.RelativePath));
            string expectedRoot = Path.GetFullPath(captureRoot) + Path.DirectorySeparatorChar;
            if (!absolutePath.StartsWith(expectedRoot, StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidDataException("An asset path escaped the Spectator capture directory.");
            }
            await UploadFileAsync(remoteAsset.Upload, asset, absolutePath, cancellationToken);
        }
    }

    internal async Task<RemoteImpression> CompleteImpressionAsync(
        string impressionId,
        CancellationToken cancellationToken) =>
        (await SendJsonAsync<RemoteImpression>(
            HttpMethod.Post,
            $"/api/v1/impressions/{Uri.EscapeDataString(impressionId)}/complete",
            null,
            cancellationToken)).Data
        ?? throw new InvalidDataException("Volputas omitted the completed impression.");

    internal async Task<RemoteImpression> GetImpressionAsync(
        string impressionId,
        CancellationToken cancellationToken) =>
        (await SendJsonAsync<RemoteImpression>(
            HttpMethod.Get,
            $"/api/v1/impressions/{Uri.EscapeDataString(impressionId)}",
            null,
            cancellationToken)).Data
        ?? throw new InvalidDataException("Volputas omitted the impression.");

    internal async Task DeleteImpressionAsync(string impressionId, CancellationToken cancellationToken)
    {
        using HttpResponseMessage response = await SendAuthorizedAsync(
            () => new HttpRequestMessage(HttpMethod.Delete, new Uri(_baseUri, $"/api/v1/impressions/{Uri.EscapeDataString(impressionId)}")),
            cancellationToken);
        if (response.StatusCode != HttpStatusCode.NoContent)
        {
            await ThrowApiErrorAsync(response, cancellationToken);
        }
    }

    private async Task UploadFileAsync(
        UploadReservation reservation,
        StoredAsset asset,
        string absolutePath,
        CancellationToken cancellationToken)
    {
        await using var stream = new FileStream(
            absolutePath,
            FileMode.Open,
            FileAccess.Read,
            FileShare.Read,
            1024 * 1024,
            FileOptions.Asynchronous | FileOptions.SequentialScan);
        using var request = new HttpRequestMessage(HttpMethod.Put, reservation.Url)
        {
            Content = new StreamContent(stream),
        };
        request.Content.Headers.ContentType = MediaTypeHeaderValue.Parse(asset.MimeType);
        foreach ((string name, string value) in reservation.Headers)
        {
            if (!request.Headers.TryAddWithoutValidation(name, value))
            {
                _ = request.Content.Headers.TryAddWithoutValidation(name, value);
            }
        }
        using HttpResponseMessage response = await _httpClient.SendAsync(request, cancellationToken);
        if (!response.IsSuccessStatusCode)
        {
            throw new VolputasApiException(response.StatusCode, "MEDIA_UPLOAD_FAILED", "Object storage rejected an asset upload.");
        }
    }

    private async Task<ApiEnvelope<T>> SendJsonAsync<T>(
        HttpMethod method,
        string path,
        JsonElement? payload,
        CancellationToken cancellationToken,
        IReadOnlyDictionary<string, string>? headers = null)
    {
        using HttpResponseMessage response = await SendAuthorizedAsync(() =>
        {
            var request = new HttpRequestMessage(method, new Uri(_baseUri, path));
            if (payload is not null) request.Content = JsonContent.Create(payload.Value);
            if (headers is not null)
            {
                foreach ((string name, string value) in headers) request.Headers.TryAddWithoutValidation(name, value);
            }
            return request;
        }, cancellationToken);
        if (!response.IsSuccessStatusCode) await ThrowApiErrorAsync(response, cancellationToken);
        if (response.StatusCode == HttpStatusCode.NoContent)
        {
            return new ApiEnvelope<T>(true, default, null);
        }
        return await response.Content.ReadFromJsonAsync<ApiEnvelope<T>>(cancellationToken: cancellationToken)
            ?? throw new InvalidDataException("Volputas returned an empty JSON response.");
    }

    private async Task<HttpResponseMessage> SendAuthorizedAsync(
        Func<HttpRequestMessage> createRequest,
        CancellationToken cancellationToken)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (_accessToken is null) throw new InvalidOperationException("Volputas login is required.");
        HttpResponseMessage response = await SendOnceAsync(createRequest, cancellationToken);
        if (response.StatusCode != HttpStatusCode.Unauthorized) return response;
        response.Dispose();
        await RefreshAsync(cancellationToken);
        return await SendOnceAsync(createRequest, cancellationToken);
    }

    private async Task<HttpResponseMessage> SendOnceAsync(
        Func<HttpRequestMessage> createRequest,
        CancellationToken cancellationToken)
    {
        using HttpRequestMessage request = createRequest();
        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", _accessToken);
        return await _httpClient.SendAsync(request, cancellationToken);
    }

    private async Task RefreshAsync(CancellationToken cancellationToken)
    {
        await _refreshGate.WaitAsync(cancellationToken);
        try
        {
            string refreshToken = _secretStore.Read(NativeLoginCoordinator.RefreshTokenCredential)
                ?? throw new InvalidOperationException("No Volputas refresh token is stored.");
            using var request = new HttpRequestMessage(HttpMethod.Post, new Uri(_baseUri, "/auth/refresh"))
            {
                Content = JsonContent.Create(new { refresh_token = refreshToken }),
            };
            using HttpResponseMessage response = await _httpClient.SendAsync(request, cancellationToken);
            if (!response.IsSuccessStatusCode) await ThrowApiErrorAsync(response, cancellationToken);
            ApiEnvelope<TokenPair> envelope = await response.Content.ReadFromJsonAsync<ApiEnvelope<TokenPair>>(
                cancellationToken: cancellationToken)
                ?? throw new InvalidDataException("Volputas returned an empty token response.");
            SetTokens(envelope.Data ?? throw new InvalidDataException("Volputas omitted refreshed tokens."));
        }
        finally
        {
            _refreshGate.Release();
        }
    }

    private static async Task ThrowApiErrorAsync(HttpResponseMessage response, CancellationToken cancellationToken)
    {
        ApiEnvelope<JsonElement>? envelope = null;
        try
        {
            envelope = await response.Content.ReadFromJsonAsync<ApiEnvelope<JsonElement>>(cancellationToken: cancellationToken);
        }
        catch (JsonException)
        {
        }
        throw new VolputasApiException(
            response.StatusCode,
            envelope?.Error?.Code ?? "HTTP_ERROR",
            envelope?.Error?.Message ?? $"Volputas returned HTTP {(int)response.StatusCode}.");
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _accessToken = null;
        _refreshGate.Dispose();
        GC.SuppressFinalize(this);
    }

    private sealed record SessionResponse(
        [property: System.Text.Json.Serialization.JsonPropertyName("id")] string Id);
}
