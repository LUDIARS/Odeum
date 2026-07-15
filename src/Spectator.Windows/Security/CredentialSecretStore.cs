using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using Spectator.Core.Security;

namespace Spectator.Windows.Security;

internal sealed class CredentialSecretStore : ISecretStore
{
    private const string TargetPrefix = "LUDIARS/Spectator/";
    private const uint CredentialTypeGeneric = 1;
    private const uint CredentialPersistLocalMachine = 2;

    public void Write(string name, string secret)
    {
        ValidateName(name);
        ArgumentNullException.ThrowIfNull(secret);
        byte[] bytes = Encoding.UTF8.GetBytes(secret);
        if (bytes.Length > 5120)
        {
            throw new ArgumentException("The credential is too large.", nameof(secret));
        }

        GCHandle pinned = GCHandle.Alloc(bytes, GCHandleType.Pinned);
        try
        {
            var credential = new NativeCredential
            {
                Type = CredentialTypeGeneric,
                TargetName = TargetPrefix + name,
                CredentialBlobSize = checked((uint)bytes.Length),
                CredentialBlob = pinned.AddrOfPinnedObject(),
                Persist = CredentialPersistLocalMachine,
                UserName = Environment.UserName,
            };
            if (!CredWrite(ref credential, 0))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "Credential Manager rejected the secret.");
            }
        }
        finally
        {
            CryptographicOperations.ZeroMemory(bytes);
            pinned.Free();
        }
    }

    public string? Read(string name)
    {
        ValidateName(name);
        if (!CredRead(TargetPrefix + name, CredentialTypeGeneric, 0, out IntPtr pointer))
        {
            int error = Marshal.GetLastWin32Error();
            if (error == 1168)
            {
                return null;
            }
            throw new Win32Exception(error, "Credential Manager could not read the secret.");
        }

        try
        {
            NativeCredential credential = Marshal.PtrToStructure<NativeCredential>(pointer);
            if (credential.CredentialBlob == IntPtr.Zero || credential.CredentialBlobSize == 0)
            {
                return string.Empty;
            }
            byte[] bytes = new byte[credential.CredentialBlobSize];
            Marshal.Copy(credential.CredentialBlob, bytes, 0, bytes.Length);
            try
            {
                return Encoding.UTF8.GetString(bytes);
            }
            finally
            {
                CryptographicOperations.ZeroMemory(bytes);
            }
        }
        finally
        {
            CredFree(pointer);
        }
    }

    public void Delete(string name)
    {
        ValidateName(name);
        if (!CredDelete(TargetPrefix + name, CredentialTypeGeneric, 0))
        {
            int error = Marshal.GetLastWin32Error();
            if (error != 1168)
            {
                throw new Win32Exception(error, "Credential Manager could not delete the secret.");
            }
        }
    }

    private static void ValidateName(string name)
    {
        if (string.IsNullOrWhiteSpace(name) || name.IndexOfAny(['/', '\\']) >= 0)
        {
            throw new ArgumentException("A simple credential name is required.", nameof(name));
        }
    }

    [DllImport("advapi32.dll", EntryPoint = "CredWriteW", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CredWrite(ref NativeCredential credential, uint flags);

    [DllImport("advapi32.dll", EntryPoint = "CredReadW", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CredRead(string target, uint type, uint flags, out IntPtr credential);

    [DllImport("advapi32.dll", EntryPoint = "CredDeleteW", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CredDelete(string target, uint type, uint flags);

    [DllImport("advapi32.dll")]
    private static extern void CredFree(IntPtr credential);

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct NativeCredential
    {
        internal uint Flags;
        internal uint Type;
        internal string TargetName;
        internal string? Comment;
        internal System.Runtime.InteropServices.ComTypes.FILETIME LastWritten;
        internal uint CredentialBlobSize;
        internal IntPtr CredentialBlob;
        internal uint Persist;
        internal uint AttributeCount;
        internal IntPtr Attributes;
        internal string? TargetAlias;
        internal string UserName;
    }
}
