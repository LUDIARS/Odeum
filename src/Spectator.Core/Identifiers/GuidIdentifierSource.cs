namespace Spectator.Core.Identifiers;

public sealed class GuidIdentifierSource : IIdentifierSource
{
    public Guid NewIdentifier() => Guid.NewGuid();
}
