namespace Spectator.Core.Identifiers;

public interface IIdentifierSource
{
    Guid NewIdentifier();
}
