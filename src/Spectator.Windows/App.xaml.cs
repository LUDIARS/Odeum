using System.Windows;
using System.Windows.Threading;

namespace Spectator.Windows;

public partial class App : System.Windows.Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        DispatcherUnhandledException += HandleDispatcherUnhandledException;
        base.OnStartup(e);
    }

    private static void HandleDispatcherUnhandledException(
        object sender,
        DispatcherUnhandledExceptionEventArgs eventArgs)
    {
        System.Windows.MessageBox.Show(
            eventArgs.Exception.Message,
            "Spectator error",
            System.Windows.MessageBoxButton.OK,
            System.Windows.MessageBoxImage.Error);
        eventArgs.Handled = true;
    }
}
