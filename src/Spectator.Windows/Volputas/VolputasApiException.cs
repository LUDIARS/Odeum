namespace Spectator.Windows.Volputas;

internal sealed class VolputasApiException : Exception
{
    internal VolputasApiException(HttpStatusCode statusCode, string code, string message)
        : base(message)
    {
        StatusCode = statusCode;
        Code = code;
    }

    internal HttpStatusCode StatusCode { get; }

    internal string Code { get; }

    internal bool IsPermanent => StatusCode is >= HttpStatusCode.BadRequest and < HttpStatusCode.InternalServerError
        && StatusCode != HttpStatusCode.Unauthorized
        && StatusCode != HttpStatusCode.RequestTimeout
        && (int)StatusCode != 429;
}
