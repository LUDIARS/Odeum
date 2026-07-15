using System.Text.RegularExpressions;
using Spectator.Core.Games;
using Spectator.Windows.Interop;

namespace Spectator.Windows.Windows;

internal sealed class GameTargetDetector
{
    private readonly WindowCatalog _windowCatalog;

    internal GameTargetDetector(WindowCatalog windowCatalog) => _windowCatalog = windowCatalog;

    internal IReadOnlyList<WindowDescriptor> FindCandidates(GameTarget target)
    {
        Regex? titlePattern = target.TitlePattern is null
            ? null
            : new Regex(target.TitlePattern, RegexOptions.CultureInvariant, TimeSpan.FromMilliseconds(50));
        return _windowCatalog.ListCapturableWindows()
            .Where(window => string.Equals(window.ProcessName, target.ExecutableName, StringComparison.OrdinalIgnoreCase))
            .Where(window => titlePattern is null || titlePattern.IsMatch(window.Title))
            .OrderByDescending(window => window.Handle == NativeMethods.GetForegroundWindow())
            .ToArray();
    }
}
