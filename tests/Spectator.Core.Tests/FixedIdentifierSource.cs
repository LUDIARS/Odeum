using Spectator.Core.Identifiers;

namespace Spectator.Core.Tests;

internal sealed class FixedIdentifierSource : IIdentifierSource
{
    private readonly Guid _identifier;

    public FixedIdentifierSource(Guid identifier)
    {
        _identifier = identifier;
    }

    public Guid NewIdentifier() => _identifier;
}
