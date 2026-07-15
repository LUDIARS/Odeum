using System.Text.Json.Serialization;

namespace Spectator.Windows.Volputas;

internal sealed record TokenPair(
    [property: JsonPropertyName("access_token")] string AccessToken,
    [property: JsonPropertyName("refresh_token")] string RefreshToken);
