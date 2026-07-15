using System.Windows;
using Spectator.Core.Drafts;
using Spectator.Windows.Importing;

namespace Spectator.Windows;

public partial class MainWindow
{
    private readonly LocalVideoImporter _localVideoImporter;

    private async void ImportLocalVideoClick(object sender, RoutedEventArgs eventArgs)
    {
        string gameId = ImportGameIdTextBox.Text.Trim();
        if (gameId.Length is 0 or > 100)
        {
            ImportVideoStatusText.Text = "ゲームIDまたは識別名を1〜100文字で入力してください。";
            return;
        }
        var dialog = new Microsoft.Win32.OpenFileDialog
        {
            CheckFileExists = true,
            Filter = "対応動画 (*.mp4;*.mkv;*.webm)|*.mp4;*.mkv;*.webm",
            Multiselect = false,
            Title = "レビューする録画済み動画を選択",
        };
        if (dialog.ShowDialog(this) != true) return;

        ImportLocalVideoButton.IsEnabled = false;
        ImportVideoStatusText.Text = "動画をローカルライブラリへ読み込んでいます…";
        try
        {
            StoredDraft draft = await _localVideoImporter.ImportAsync(
                dialog.FileName,
                gameId,
                _lifetime.Token);
            RefreshDrafts();
            ShowDraft(draft);
            SetCollapsed(false);
            ImportVideoStatusText.Text = "読み込み完了。動画を再生してコメントまたはスタンプを記録できます。";
            CaptureStatusText.Text = "録画済み動画のレビューを開始しました。";
        }
        catch (OperationCanceledException) when (_lifetime.IsCancellationRequested)
        {
        }
        catch (Exception exception)
        {
            ImportVideoStatusText.Text = $"動画の読み込みに失敗しました: {exception.Message}";
        }
        finally
        {
            ImportLocalVideoButton.IsEnabled = true;
        }
    }
}
