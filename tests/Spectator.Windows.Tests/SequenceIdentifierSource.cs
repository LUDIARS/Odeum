using Spectator.Core.Identifiers;

namespace Spectator.Windows.Tests;

internal sealed class SequenceIdentifierSource(params Guid[] identifiers) : IIdentifierSource
{
    private readonly Queue<Guid> _identifiers = new(identifiers);

    public Guid NewIdentifier() => _identifiers.Count > 0
        ? _identifiers.Dequeue()
        : throw new InvalidOperationException("No test identifier remains.");
}
