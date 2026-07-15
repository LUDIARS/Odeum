using System.Drawing;
using System.Windows.Forms;

namespace Spectator.Windows.Shell;

internal sealed class TrayIcon : IDisposable
{
    private readonly NotifyIcon _icon;
    private bool _disposed;

    internal TrayIcon(Action show, Action exit)
    {
        var menu = new ContextMenuStrip();
        menu.Items.Add("Spectatorを表示", null, (_, _) => show());
        menu.Items.Add("終了", null, (_, _) => exit());
        _icon = new NotifyIcon
        {
            Icon = SystemIcons.Application,
            Text = "Spectator",
            Visible = true,
            ContextMenuStrip = menu,
        };
        _icon.DoubleClick += (_, _) => show();
    }

    internal void Notify(string title, string message, ToolTipIcon icon = ToolTipIcon.Info)
    {
        _icon.ShowBalloonTip(5000, title, message, icon);
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _icon.Visible = false;
        _icon.ContextMenuStrip?.Dispose();
        _icon.Dispose();
        GC.SuppressFinalize(this);
    }
}
