using System.Text.Json.Serialization;

namespace Spectator.Windows.Volputas;

internal sealed record ApiEnvelope<T>(
    [property: JsonPropertyName("ok")] bool IsSuccessful,
    [property: JsonPropertyName("data")] T? Data,
    [property: JsonPropertyName("error")] ApiError? Error);

internal sealed record ApiError(
    [property: JsonPropertyName("code")] string Code,
    [property: JsonPropertyName("message")] string Message);
