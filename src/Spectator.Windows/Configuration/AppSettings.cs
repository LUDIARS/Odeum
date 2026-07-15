using System.Text.Json;

namespace Spectator.Windows.Configuration;

internal sealed record AppSettings(
    string IntegrationMode,
    Uri? VolputasApiBaseUrl,
    Uri? VolputasWebBaseUrl,
    string? LoginProvider,
    Uri ObsWebSocketUrl,
    string CaptureHotKey,
    string ScreenshotFormat,
    string VideoBackend,
    int ReplaySeconds)
{
    internal bool IsVolputasEnabled => IntegrationMode == "volputas";

    private sealed record SettingsDocument(
        string? IntegrationMode,
        string? VolputasApiBaseUrl,
        string? VolputasWebBaseUrl,
        string? LoginProvider,
        string? ObsWebSocketUrl,
        string? CaptureHotKey,
        string? ScreenshotFormat,
        string? VideoBackend,
        int? ReplaySeconds);

    internal static AppSettings Load(string path)
    {
        if (!File.Exists(path))
        {
            throw new FileNotFoundException("Spectator's settings file is required.", path);
        }

        SettingsDocument document = JsonSerializer.Deserialize<SettingsDocument>(
            File.ReadAllText(path),
            new JsonSerializerOptions { PropertyNameCaseInsensitive = true })
            ?? throw new InvalidDataException("spectator.settings.json is invalid.");
        string integrationMode = RequireChoice(
            document.IntegrationMode,
            "integrationMode",
            "standalone",
            "volputas");
        Uri? apiBaseUrl = null;
        Uri? webBaseUrl = null;
        string? provider = null;
        if (integrationMode == "volputas")
        {
            apiBaseUrl = RequireHttpUri(document.VolputasApiBaseUrl, "volputasApiBaseUrl");
            webBaseUrl = RequireHttpUri(document.VolputasWebBaseUrl, "volputasWebBaseUrl");
            provider = RequireChoice(document.LoginProvider, "loginProvider", "google", "discord");
        }
        Uri obsUrl = RequireWebSocketUri(document.ObsWebSocketUrl);
        string hotKey = RequireChoice(document.CaptureHotKey, "captureHotKey", "Ctrl+Shift+F8");
        string screenshot = RequireChoice(document.ScreenshotFormat, "screenshotFormat", "jpeg", "png");
        string backend = RequireChoice(document.VideoBackend, "videoBackend", "obs", "none");
        int replaySeconds = document.ReplaySeconds
            ?? throw new InvalidDataException("replaySeconds is required.");
        if (replaySeconds is < 15 or > 30)
        {
            throw new InvalidDataException("replaySeconds must be between 15 and 30.");
        }

        return new AppSettings(
            integrationMode,
            apiBaseUrl,
            webBaseUrl,
            provider,
            obsUrl,
            hotKey,
            screenshot,
            backend,
            replaySeconds);
    }

    private static Uri RequireHttpUri(string? value, string field)
    {
        if (!Uri.TryCreate(value, UriKind.Absolute, out Uri? uri)
            || (uri.Scheme != Uri.UriSchemeHttp && uri.Scheme != Uri.UriSchemeHttps))
        {
            throw new InvalidDataException($"{field} must be an absolute http:// or https:// URL.");
        }
        bool loopbackHttp = uri.Scheme == Uri.UriSchemeHttp
            && ((IPAddress.TryParse(uri.Host, out IPAddress? address) && IPAddress.IsLoopback(address))
                || string.Equals(uri.Host, "localhost", StringComparison.OrdinalIgnoreCase));
        if (uri.Scheme != Uri.UriSchemeHttps && !loopbackHttp)
        {
            throw new InvalidDataException($"{field} must use HTTPS except for a loopback development URL.");
        }
        return uri;
    }

    private static Uri RequireWebSocketUri(string? value)
    {
        if (!Uri.TryCreate(value, UriKind.Absolute, out Uri? uri)
            || (uri.Scheme != Uri.UriSchemeWs && uri.Scheme != Uri.UriSchemeWss))
        {
            throw new InvalidDataException("obsWebSocketUrl must be an absolute ws:// or wss:// URL.");
        }
        if (!IPAddress.TryParse(uri.Host, out IPAddress? address) || !IPAddress.IsLoopback(address))
        {
            throw new InvalidDataException("obsWebSocketUrl must use a loopback address.");
        }
        return uri;
    }

    private static string RequireChoice(string? value, string field, params string[] choices)
    {
        if (value is null || !choices.Contains(value, StringComparer.Ordinal))
        {
            throw new InvalidDataException($"{field} must be one of: {string.Join(", ", choices)}.");
        }
        return value;
    }
}
