namespace Spectator.Core.Security;

public interface ISecretStore
{
    void Write(string name, string secret);

    string? Read(string name);

    void Delete(string name);
}
