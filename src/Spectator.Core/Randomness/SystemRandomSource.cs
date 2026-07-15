namespace Spectator.Core.Randomness;

public sealed class SystemRandomSource : IRandomSource
{
    public double NextUnit() => Random.Shared.NextDouble();
}
