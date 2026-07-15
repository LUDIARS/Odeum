using System.Diagnostics;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Spectator.Core.Security;

namespace Spectator.Windows.Volputas;

internal sealed class NativeLoginCoordinator
{
    internal const string RefreshTokenCredential = "VolputasRefreshToken";
    private readonly HttpClient _httpClient;
    private readonly Uri _baseUri;
    private readonly string _provider;
    private readonly ISecretStore _secretStore;

    internal NativeLoginCoordinator(HttpClient httpClient, Uri baseUri, string provider, ISecretStore secretStore)
    {
        _httpClient = httpClient;
        _baseUri = baseUri;
        _provider = provider;
        _secretStore = secretStore;
    }

    internal async Task<TokenPair> LoginAsync(CancellationToken cancellationToken)
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start(1);
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        string verifier = Base64Url(RandomNumberGenerator.GetBytes(48));
        string challenge = Base64Url(SHA256.HashData(Encoding.ASCII.GetBytes(verifier)));
        string nonce = Base64Url(RandomNumberGenerator.GetBytes(32));
        string redirectUri = $"http://127.0.0.1:{port}/callback";
        Uri loginUri = BuildLoginUri(redirectUri, challenge, nonce);
        ApiEnvelope<AuthorizationResponse> login = await GetJsonAsync<ApiEnvelope<AuthorizationResponse>>(
            loginUri,
            cancellationToken);
        if (!login.IsSuccessful || login.Data is null)
        {
            throw new InvalidOperationException(login.Error?.Message ?? "Volputas rejected native login initialization.");
        }

        Process.Start(new ProcessStartInfo(login.Data.AuthorizationUrl) { UseShellExecute = true });
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromMinutes(5));
        using TcpClient client = await listener.AcceptTcpClientAsync(timeout.Token);
        if (client.Client.RemoteEndPoint is not IPEndPoint remote || !IPAddress.IsLoopback(remote.Address))
        {
            throw new InvalidOperationException("The login callback did not originate from loopback.");
        }

        Callback callback = await ReadCallbackAsync(client, timeout.Token);
        await WriteBrowserResponseAsync(client, timeout.Token);
        if (!CryptographicOperations.FixedTimeEquals(
                Encoding.UTF8.GetBytes(callback.State),
                Encoding.UTF8.GetBytes(nonce)))
        {
            throw new InvalidOperationException("The native login state did not match.");
        }

        using var exchange = new HttpRequestMessage(HttpMethod.Post, new Uri(_baseUri, "/auth/ticket"))
        {
            Content = JsonContent.Create(new { ticket = callback.Ticket, code_verifier = verifier }),
        };
        using HttpResponseMessage response = await _httpClient.SendAsync(exchange, cancellationToken);
        ApiEnvelope<TokenPair> envelope = await ReadEnvelopeAsync<TokenPair>(response, cancellationToken);
        TokenPair tokens = envelope.Data
            ?? throw new InvalidOperationException(envelope.Error?.Message ?? "Volputas did not issue native tokens.");
        _secretStore.Write(RefreshTokenCredential, tokens.RefreshToken);
        return tokens;
    }

    private Uri BuildLoginUri(string redirectUri, string challenge, string nonce)
    {
        var builder = new UriBuilder(new Uri(_baseUri, "/auth/login"));
        Dictionary<string, string> parameters = new()
        {
            ["provider"] = _provider,
            ["client"] = "spectator",
            ["redirect_uri"] = redirectUri,
            ["code_challenge"] = challenge,
            ["code_challenge_method"] = "S256",
            ["nonce"] = nonce,
        };
        builder.Query = string.Join("&", parameters.Select(pair =>
            $"{Uri.EscapeDataString(pair.Key)}={Uri.EscapeDataString(pair.Value)}"));
        return builder.Uri;
    }

    private async Task<T> GetJsonAsync<T>(Uri uri, CancellationToken cancellationToken)
    {
        using HttpResponseMessage response = await _httpClient.GetAsync(uri, cancellationToken);
        response.EnsureSuccessStatusCode();
        return await response.Content.ReadFromJsonAsync<T>(cancellationToken: cancellationToken)
            ?? throw new InvalidDataException("Volputas returned an empty JSON response.");
    }

    private static async Task<Callback> ReadCallbackAsync(TcpClient client, CancellationToken cancellationToken)
    {
        using NetworkStream stream = client.GetStream();
        using var reader = new StreamReader(stream, Encoding.ASCII, false, 4096, true);
        string? requestLine = await reader.ReadLineAsync(cancellationToken);
        while (!string.IsNullOrEmpty(await reader.ReadLineAsync(cancellationToken)))
        {
        }
        string[] parts = requestLine?.Split(' ') ?? [];
        if (parts.Length < 2 || parts[0] != "GET")
        {
            throw new InvalidDataException("The login callback request was invalid.");
        }
        var uri = new Uri($"http://127.0.0.1{parts[1]}");
        Dictionary<string, string> query = uri.Query.TrimStart('?')
            .Split('&', StringSplitOptions.RemoveEmptyEntries)
            .Select(part => part.Split('=', 2))
            .ToDictionary(
                part => Uri.UnescapeDataString(part[0]),
                part => part.Length > 1 ? Uri.UnescapeDataString(part[1]) : string.Empty,
                StringComparer.Ordinal);
        if (uri.AbsolutePath != "/callback"
            || !query.TryGetValue("ticket", out string? ticket)
            || !query.TryGetValue("state", out string? state))
        {
            throw new InvalidDataException("The login callback was missing ticket or state.");
        }
        return new Callback(ticket, state);
    }

    private static async Task WriteBrowserResponseAsync(TcpClient client, CancellationToken cancellationToken)
    {
        const string html = "<!doctype html><meta charset=utf-8><title>Spectator</title><p>ログインが完了しました。Spectatorへ戻ってください。</p>";
        byte[] body = Encoding.UTF8.GetBytes(html);
        byte[] header = Encoding.ASCII.GetBytes(
            $"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: {body.Length}\r\nConnection: close\r\n\r\n");
        NetworkStream stream = client.GetStream();
        await stream.WriteAsync(header, cancellationToken);
        await stream.WriteAsync(body, cancellationToken);
        await stream.FlushAsync(cancellationToken);
    }

    private static async Task<ApiEnvelope<T>> ReadEnvelopeAsync<T>(
        HttpResponseMessage response,
        CancellationToken cancellationToken)
    {
        ApiEnvelope<T> envelope = await response.Content.ReadFromJsonAsync<ApiEnvelope<T>>(
            cancellationToken: cancellationToken)
            ?? throw new InvalidDataException("Volputas returned an empty JSON response.");
        if (!response.IsSuccessStatusCode || !envelope.IsSuccessful)
        {
            throw new InvalidOperationException(envelope.Error?.Message ?? $"Volputas returned {response.StatusCode}.");
        }
        return envelope;
    }

    private static string Base64Url(byte[] bytes) => Convert.ToBase64String(bytes).TrimEnd('=').Replace('+', '-').Replace('/', '_');

    private sealed record AuthorizationResponse(
        [property: System.Text.Json.Serialization.JsonPropertyName("authorizationUrl")] string AuthorizationUrl);

    private sealed record Callback(string Ticket, string State);
}
