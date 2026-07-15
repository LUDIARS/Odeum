using System.Windows;
using Spectator.Core.Drafts;
using Spectator.Core.Reactions;
using Spectator.Windows.Persistence;
using Spectator.Windows.Reactions;

namespace Spectator.Windows;

public partial class MainWindow
{
    private readonly ReactionRawDataExporter _reactionExporter = new();
    private readonly ReactionRepository _reactions;
    private StoredAsset? _currentVideo;
    private bool _isUpdatingSeekSlider;

    private void ConfigureReactionPanel(StoredDraft draft, StoredAsset? video)
    {
        _currentVideo = video;
        bool hasVideo = video?.DurationMilliseconds is not null;
        ReactionPanel.Visibility = hasVideo ? Visibility.Visible : Visibility.Collapsed;
        ReactionContentTextBox.IsEnabled = hasVideo;
        AddReactionButton.IsEnabled = hasVideo;
        PositiveReactionButton.IsEnabled = hasVideo;
        NegativeReactionButton.IsEnabled = hasVideo;
        VideoSeekSlider.IsEnabled = hasVideo;
        VideoSeekSlider.Maximum = Math.Max(1, video?.DurationMilliseconds ?? 1);
        VideoSeekSlider.Value = 0;
        RefreshReactions(draft.Id);
        UpdateReactionPlaybackPosition();
    }

    private void ClearReactionPanel()
    {
        _currentVideo = null;
        ReactionContentTextBox.Clear();
        ReactionListBox.ItemsSource = null;
        VideoPositionText.Text = FormatVideoOffset(0);
    }

    private void RefreshReactions(Guid draftId)
    {
        ReactionListBox.ItemsSource = _reactions.List(draftId)
            .Select(annotation => new ReactionListItem(annotation))
            .ToArray();
    }

    private void PlayVideoClick(object sender, RoutedEventArgs eventArgs)
    {
        if (VideoPreview.Source is null) return;
        VideoPreview.Play();
    }

    private void PauseVideoClick(object sender, RoutedEventArgs eventArgs)
    {
        if (VideoPreview.Source is null) return;
        VideoPreview.Pause();
        UpdateReactionPlaybackPosition();
    }

    private void VideoPreviewMediaOpened(object sender, RoutedEventArgs eventArgs) =>
        UpdateReactionPlaybackPosition();

    private void VideoPreviewMediaEnded(object sender, RoutedEventArgs eventArgs)
    {
        VideoPreview.Stop();
        VideoPreview.Position = TimeSpan.Zero;
        UpdateReactionPlaybackPosition();
    }

    private void VideoSeekSliderValueChanged(
        object sender,
        System.Windows.RoutedPropertyChangedEventArgs<double> eventArgs)
    {
        if (_isUpdatingSeekSlider || VideoPreview.Source is null) return;
        VideoPreview.Position = TimeSpan.FromMilliseconds(eventArgs.NewValue);
        UpdateReactionPlaybackPosition();
    }

    private void AddReactionClick(object sender, RoutedEventArgs eventArgs)
    {
        AddReactionAtCurrentPosition(ReactionKind.Comment, ReactionContentTextBox.Text);
    }

    private void AddPositiveReactionClick(object sender, RoutedEventArgs eventArgs)
    {
        string content = string.IsNullOrWhiteSpace(ReactionContentTextBox.Text)
            ? "ここ良かった"
            : ReactionContentTextBox.Text;
        AddReactionAtCurrentPosition(ReactionKind.Positive, content);
    }

    private void AddNegativeReactionClick(object sender, RoutedEventArgs eventArgs)
    {
        string content = string.IsNullOrWhiteSpace(ReactionContentTextBox.Text)
            ? "ここ悪かった"
            : ReactionContentTextBox.Text;
        AddReactionAtCurrentPosition(ReactionKind.Negative, content);
    }

    private void AddReactionAtCurrentPosition(ReactionKind kind, string content)
    {
        if (_currentDraft is null || _currentVideo?.DurationMilliseconds is not int duration)
        {
            CaptureStatusText.Text = "反応を記録する動画がありません。";
            return;
        }
        try
        {
            long offset = checked((long)Math.Round(VideoPreview.Position.TotalMilliseconds));
            if (offset < 0 || offset > duration)
            {
                CaptureStatusText.Text = "動画の再生範囲内で反応を記録してください。";
                return;
            }
            var annotation = new ReactionAnnotation(
                _identifierSource.NewIdentifier(),
                _currentDraft.Id,
                offset,
                kind,
                content,
                _timeSource.GetUtcNow());
            _reactions.Add(annotation);
            ReactionContentTextBox.Clear();
            RefreshReactions(_currentDraft.Id);
            CaptureStatusText.Text = $"{ReactionLabel(kind)}を {FormatVideoOffset(offset)} に記録しました。";
        }
        catch (Exception exception)
        {
            CaptureStatusText.Text = $"反応の記録に失敗しました: {exception.Message}";
        }
    }

    private void DeleteReactionClick(object sender, RoutedEventArgs eventArgs)
    {
        if (_currentDraft is null || ReactionListBox.SelectedItem is not ReactionListItem selected)
        {
            CaptureStatusText.Text = "削除する反応を選択してください。";
            return;
        }
        try
        {
            _reactions.Delete(selected.Annotation.Id, _currentDraft.Id);
            RefreshReactions(_currentDraft.Id);
            CaptureStatusText.Text = "本人の反応を削除しました。";
        }
        catch (Exception exception)
        {
            CaptureStatusText.Text = $"反応の削除に失敗しました: {exception.Message}";
        }
    }

    private void ExportReactionRawDataClick(object sender, RoutedEventArgs eventArgs)
    {
        if (_currentDraft is null || _currentVideo is null)
        {
            CaptureStatusText.Text = "raw dataを出力する動画がありません。";
            return;
        }
        var dialog = new Microsoft.Win32.SaveFileDialog
        {
            AddExtension = true,
            DefaultExt = ".json",
            Filter = "JSON raw data (*.json)|*.json",
            FileName = $"spectator-reactions-{_currentDraft.Id:N}.json",
            OverwritePrompt = true,
            Title = "本人の反応raw dataを保存",
        };
        if (dialog.ShowDialog(this) != true) return;
        try
        {
            string gameId = _localSessions.Get(_currentDraft.LocalSessionId)?.GameId
                ?? throw new InvalidDataException("The reaction draft's play session does not exist.");
            _reactionExporter.Write(
                dialog.FileName,
                gameId,
                _currentDraft,
                _reactions.List(_currentDraft.Id));
            CaptureStatusText.Text = "本人の反応raw dataをJSONで出力しました。";
        }
        catch (Exception exception)
        {
            CaptureStatusText.Text = $"raw dataの出力に失敗しました: {exception.Message}";
        }
    }

    private void UpdateReactionPlaybackPosition()
    {
        long offset = Math.Max(0, checked((long)Math.Round(VideoPreview.Position.TotalMilliseconds)));
        VideoPositionText.Text = FormatVideoOffset(offset);
        _isUpdatingSeekSlider = true;
        try
        {
            VideoSeekSlider.Value = Math.Min(offset, VideoSeekSlider.Maximum);
        }
        finally
        {
            _isUpdatingSeekSlider = false;
        }
    }

    private static string FormatVideoOffset(long milliseconds)
    {
        TimeSpan offset = TimeSpan.FromMilliseconds(milliseconds);
        return $"{(int)offset.TotalHours:00}:{offset.Minutes:00}:{offset.Seconds:00}.{offset.Milliseconds:000}";
    }

    private sealed record ReactionListItem(ReactionAnnotation Annotation)
    {
        internal string OffsetText => FormatVideoOffset(Annotation.VideoOffsetMilliseconds);

        internal string Content => Annotation.Content;

        internal string StampLabel => ReactionLabel(Annotation.Kind);
    }

    private static string ReactionLabel(ReactionKind kind) => kind switch
    {
        ReactionKind.Comment => "コメント",
        ReactionKind.Positive => "👍 良かった",
        ReactionKind.Negative => "👎 悪かった",
        _ => throw new ArgumentOutOfRangeException(nameof(kind)),
    };
}
