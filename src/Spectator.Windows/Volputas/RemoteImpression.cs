using System.Text.Json.Serialization;

namespace Spectator.Windows.Volputas;

internal sealed record RemoteImpression(
    [property: JsonPropertyName("id")] string Id,
    [property: JsonPropertyName("status")] string Status,
    [property: JsonPropertyName("rejection_reason")] string? RejectionReason,
    [property: JsonPropertyName("assets")] IReadOnlyList<RemoteAsset> Assets);

internal sealed record RemoteAsset(
    [property: JsonPropertyName("client_asset_id")] string ClientAssetId,
    [property: JsonPropertyName("status")] string Status,
    [property: JsonPropertyName("upload")] UploadReservation? Upload);

internal sealed record UploadReservation(
    [property: JsonPropertyName("url")] string Url,
    [property: JsonPropertyName("headers")] IReadOnlyDictionary<string, string> Headers);
