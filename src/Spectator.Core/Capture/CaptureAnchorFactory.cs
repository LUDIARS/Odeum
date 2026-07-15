using Spectator.Core.Identifiers;
using Spectator.Core.Sessions;

namespace Spectator.Core.Capture;

public sealed class CaptureAnchorFactory
{
    private readonly IIdentifierSource _identifierSource;

    public CaptureAnchorFactory(IIdentifierSource identifierSource)
    {
        _identifierSource = identifierSource ?? throw new ArgumentNullException(nameof(identifierSource));
    }

    public CaptureAnchor Create(PlaytimeSnapshot playtime)
    {
        ArgumentNullException.ThrowIfNull(playtime);
        return new CaptureAnchor(_identifierSource.NewIdentifier(), playtime.CapturedAt, playtime);
    }
}
