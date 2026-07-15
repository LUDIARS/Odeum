using System.ComponentModel;
using System.Diagnostics;
using System.Windows;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using Spectator.Core.Drafts;
using Spectator.Core.Games;
using Spectator.Core.Identifiers;
using Spectator.Core.Input;
using Spectator.Core.Randomness;
using Spectator.Core.Sessions;
using Spectator.Core.Time;
using Spectator.Windows.Capture;
using Spectator.Windows.Configuration;
using Spectator.Windows.Input;
using Spectator.Windows.Importing;
using Spectator.Windows.Interop;
using Spectator.Windows.Obs;
using Spectator.Windows.Persistence;
using Spectator.Windows.Security;
using Spectator.Windows.Sessions;
using Spectator.Windows.Shell;
using Spectator.Windows.Storage;
using Spectator.Windows.Sync;
using Spectator.Windows.Volputas;
using Spectator.Windows.Windows;

namespace Spectator.Windows;

public partial class MainWindow : Window
{
    private const string ObsPasswordCredential = "ObsWebSocketPassword";
    private const int WmPowerBroadcast = 0x0218;
    private const int PbtApmSuspend = 0x0004;
    private const int PbtApmResumeSuspend = 0x0007;
    private const int PbtApmResumeAutomatic = 0x0012;

    private readonly SystemTimeSource _timeSource = new();
    private readonly GuidIdentifierSource _identifierSource = new();
    private readonly AppSettings _settings;
    private readonly WindowCatalog _windowCatalog = new();
    private readonly GameTargetDetector _targetDetector;
    private readonly GameTargetRepository _gameTargets;
    private readonly DraftRepository _drafts;
    private readonly LocalSessionRepository _localSessions;
    private readonly CredentialSecretStore _secretStore = new();
    private readonly CaptureStore _captureStore = new();
    private readonly HttpClient? _httpClient;
    private readonly VolputasApiClient? _api;
    private readonly NativeLoginCoordinator? _login;
    private readonly SessionController _sessions;
    private readonly SyncWorker? _syncWorker;
    private readonly CaptureCoordinator _captureCoordinator;
    private readonly DispatcherTimer _uiTimer;
    private readonly HotKeyGate _hotKeyGate;
    private readonly CancellationTokenSource _lifetime = new();
    private readonly TrayIcon _trayIcon;
    private WindowAttachment? _attachment;
    private GlobalHotKeyRegistration? _hotKey;
    private ObsVideoCaptureBackend? _videoBackend;
    private HwndSource? _windowSource;
    private StoredDraft? _currentDraft;
    private bool _isCapturing;
    private bool _isAttaching;
    private bool _isCollapsed;
    private bool _shutdownStarted;
    private bool _allowClose;
    private int _autoDetectTicks;

    public MainWindow()
    {
        InitializeComponent();
        _settings = AppSettings.Load(Path.Combine(AppContext.BaseDirectory, "spectator.settings.json"));
        string databasePath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "Spectator",
            "spectator.db");
        var database = new SpectatorDatabase(databasePath);
        database.Initialize();
        _gameTargets = new GameTargetRepository(database);
        _localSessions = new LocalSessionRepository(database);
        _drafts = new DraftRepository(database);
        _reactions = new ReactionRepository(database);
        _localVideoImporter = new LocalVideoImporter(
            _captureStore,
            _drafts,
            _localSessions,
            _identifierSource,
            _timeSource,
            new WindowsMediaDurationSource());
        _captureStore.CleanupOrphans(
            _drafts.ListAssetRelativePaths(),
            _timeSource.GetUtcNow() - TimeSpan.FromHours(1));
        _targetDetector = new GameTargetDetector(_windowCatalog);
        if (_settings.IsVolputasEnabled)
        {
            Uri apiBaseUrl = _settings.VolputasApiBaseUrl
                ?? throw new InvalidDataException("volputasApiBaseUrl is required in Volputas mode.");
            string loginProvider = _settings.LoginProvider
                ?? throw new InvalidDataException("loginProvider is required in Volputas mode.");
            _httpClient = new HttpClient { Timeout = TimeSpan.FromSeconds(30) };
            _api = new VolputasApiClient(_httpClient, apiBaseUrl, _secretStore);
            _login = new NativeLoginCoordinator(_httpClient, apiBaseUrl, loginProvider, _secretStore);
            _syncWorker = new SyncWorker(
                _drafts,
                _localSessions,
                _api,
                _captureStore,
                _timeSource,
                new SystemRandomSource());
            _syncWorker.StatusChanged += SyncStatusChanged;
        }
        _sessions = new SessionController(_localSessions, _api, _timeSource, _identifierSource);
        _captureCoordinator = new CaptureCoordinator(
            new WindowScreenshotCapture(),
            _captureStore,
            _drafts,
            _sessions,
            _identifierSource,
            _timeSource,
            _settings);
        _hotKeyGate = new HotKeyGate(_timeSource, TimeSpan.FromMilliseconds(250));
        _uiTimer = new DispatcherTimer(
            TimeSpan.FromMilliseconds(250),
            DispatcherPriority.Background,
            UpdateUi,
            Dispatcher);
        _trayIcon = new TrayIcon(ShowFromTray, () => Dispatcher.Invoke(Close));
        if (!_settings.IsVolputasEnabled)
        {
            VolputasPanel.Visibility = Visibility.Collapsed;
            HeaderStatusText.Text = "スタンドアロン: 認証なし";
            SubmitDraftButton.Content = "ローカル保存";
        }
        ObsEndpointTextBox.Text = _settings.ObsWebSocketUrl.AbsoluteUri;
        if (_settings.VideoBackend == "none")
        {
            ConnectObsButton.IsEnabled = false;
            ObsEndpointTextBox.IsEnabled = false;
            ObsPasswordBox.IsEnabled = false;
            ObsStatusText.Text = "設定により動画キャプチャは無効です。";
        }
        Loaded += OnLoaded;
        SizeChanged += OnSizeChanged;
    }

    protected override void OnSourceInitialized(EventArgs eventArgs)
    {
        base.OnSourceInitialized(eventArgs);
        IntPtr handle = new WindowInteropHelper(this).Handle;
        _windowSource = HwndSource.FromHwnd(handle);
        _windowSource?.AddHook(WindowMessageHook);
        if (!NativeMethods.SetWindowDisplayAffinity(handle, NativeMethods.WdaExcludeFromCapture))
        {
            CaptureStatusText.Text = "警告: Spectatorを画面キャプチャから除外できませんでした。";
        }
        try
        {
            _hotKey = new GlobalHotKeyRegistration(
                handle,
                NativeMethods.ModControl | NativeMethods.ModShift,
                0x77);
            _hotKey.Pressed += HotKeyPressed;
        }
        catch (Win32Exception exception)
        {
            CaptureStatusText.Text = $"ホットキーを登録できません: {exception.Message}";
        }
    }

    private async void OnLoaded(object sender, RoutedEventArgs eventArgs)
    {
        RefreshWindows();
        RefreshDrafts();
        _uiTimer.Start();
        if (!_settings.IsVolputasEnabled)
        {
            await TryAutoAttachAsync();
            return;
        }
        try
        {
            VolputasApiClient api = _api
                ?? throw new InvalidOperationException("Volputas mode is not initialized.");
            bool restored = await api.RestoreSessionAsync(_lifetime.Token);
            LoginStatusText.Text = restored ? "ログイン済み" : "未ログイン";
            HeaderStatusText.Text = restored ? "Volputas: 接続済み" : "Volputas: 未ログイン";
            if (restored)
            {
                await _sessions.RecoverAsync(_lifetime.Token);
                _syncWorker?.Wake();
            }
            await TryAutoAttachAsync();
        }
        catch (Exception exception)
        {
            LoginStatusText.Text = $"初期化エラー: {exception.Message}";
        }
    }

    private async void LoginClick(object sender, RoutedEventArgs eventArgs)
    {
        if (_login is null || _api is null || _syncWorker is null)
        {
            LoginStatusText.Text = "スタンドアロンモードではログインは使用しません。";
            return;
        }
        LoginButton.IsEnabled = false;
        LoginStatusText.Text = "ブラウザでログインしてください…";
        try
        {
            TokenPair tokens = await _login.LoginAsync(_lifetime.Token);
            _api.SetTokens(tokens);
            await _sessions.RecoverAsync(_lifetime.Token);
            LoginStatusText.Text = "ログイン済み";
            HeaderStatusText.Text = "Volputas: 接続済み";
            _syncWorker.Wake();
        }
        catch (OperationCanceledException) when (_lifetime.IsCancellationRequested)
        {
        }
        catch (Exception exception)
        {
            LoginStatusText.Text = $"ログイン失敗: {exception.Message}";
        }
        finally
        {
            LoginButton.IsEnabled = true;
        }
    }

    private void RefreshWindowsClick(object sender, RoutedEventArgs eventArgs) => RefreshWindows();

    private void RefreshWindows()
    {
        IntPtr selectedHandle = (WindowComboBox.SelectedItem as WindowDescriptor)?.Handle ?? IntPtr.Zero;
        IReadOnlyList<WindowDescriptor> windows = _windowCatalog.ListCapturableWindows();
        WindowComboBox.ItemsSource = windows;
        WindowComboBox.SelectedItem = windows.FirstOrDefault(window => window.Handle == selectedHandle)
            ?? windows.FirstOrDefault();
        if (WindowComboBox.SelectedItem is WindowDescriptor selected && string.IsNullOrWhiteSpace(GameIdTextBox.Text))
        {
            GameIdTextBox.Text = selected.ProcessName.ToLowerInvariant();
        }
        if (windows.Count == 0) AttachmentStatusText.Text = "キャプチャ可能なウィンドウがありません。";
    }

    private async void AttachClick(object sender, RoutedEventArgs eventArgs)
    {
        if (WindowComboBox.SelectedItem is not WindowDescriptor target)
        {
            AttachmentStatusText.Text = "ゲームウィンドウを選択してください。";
            return;
        }
        string gameId = GameIdTextBox.Text.Trim();
        if (gameId.Length is 0 or > 100)
        {
            AttachmentStatusText.Text = "game_idは1〜100文字で入力してください。";
            return;
        }
        var registration = new GameTarget(
            gameId,
            target.Title,
            target.ProcessName,
            null,
            TopmostCheckBox.IsChecked == true,
            _timeSource.GetUtcNow());
        _gameTargets.Save(registration);
        await AttachTargetAsync(target, gameId);
    }

    private async Task AttachTargetAsync(WindowDescriptor target, string gameId)
    {
        if (_isAttaching) return;
        _isAttaching = true;
        try
        {
            if (_sessions.IsRunning) await _sessions.EndAsync(_lifetime.Token);
            DetachWindow();
            _attachment = new WindowAttachment(this, target.Handle);
            _attachment.TargetClosed += TargetClosed;
            await _sessions.StartAsync(gameId, NativeMethods.GetForegroundWindow() == target.Handle, _lifetime.Token);
            Topmost = TopmostCheckBox.IsChecked == true;
            AttachmentStatusText.Text = $"{target.ProcessName} ({target.ProcessId}) に吸着中";
        }
        catch (Exception exception)
        {
            AttachmentStatusText.Text = $"吸着失敗: {exception.Message}";
            DetachWindow();
        }
        finally
        {
            _isAttaching = false;
        }
    }

    private async Task TryAutoAttachAsync()
    {
        if (_attachment is not null || _isAttaching) return;
        foreach (GameTarget target in _gameTargets.List())
        {
            IReadOnlyList<WindowDescriptor> candidates = _targetDetector.FindCandidates(target);
            if (candidates.Count == 1)
            {
                WindowDescriptor window = candidates[0];
                WindowComboBox.SelectedItem = (WindowComboBox.ItemsSource as IEnumerable<WindowDescriptor>)?
                    .FirstOrDefault(item => item.Handle == window.Handle) ?? window;
                GameIdTextBox.Text = target.GameId;
                TopmostCheckBox.IsChecked = target.IsTopmost;
                await AttachTargetAsync(window, target.GameId);
                return;
            }
            if (candidates.Count > 1)
            {
                AttachmentStatusText.Text = $"{target.DisplayName}の候補が複数あります。選択してください。";
            }
        }
    }

    private async void TargetClosed(object? sender, EventArgs eventArgs)
    {
        DetachWindow();
        await _sessions.EndAsync(_lifetime.Token);
        AttachmentStatusText.Text = "対象ゲームが終了しました。再起動を待機しています。";
    }

    private void DetachWindow()
    {
        if (_attachment is null) return;
        _attachment.TargetClosed -= TargetClosed;
        _attachment.Dispose();
        _attachment = null;
    }

    private void TopmostChanged(object sender, RoutedEventArgs eventArgs) => Topmost = TopmostCheckBox.IsChecked == true;

    private async void ConnectObsClick(object sender, RoutedEventArgs eventArgs)
    {
        if (_settings.VideoBackend != "obs")
        {
            ObsStatusText.Text = "videoBackendをobsに設定するとOBSを利用できます。";
            return;
        }
        ConnectObsButton.IsEnabled = false;
        try
        {
            if (!Uri.TryCreate(ObsEndpointTextBox.Text.Trim(), UriKind.Absolute, out Uri? endpoint)
                || (endpoint.Scheme != Uri.UriSchemeWs && endpoint.Scheme != Uri.UriSchemeWss)
                || !IPAddress.TryParse(endpoint.Host, out IPAddress? address)
                || !IPAddress.IsLoopback(address))
            {
                throw new InvalidDataException("OBS接続先は数値loopbackのws://またはwss:// URLにしてください。");
            }
            if (_videoBackend is not null)
            {
                await _videoBackend.DisposeAsync();
                _videoBackend = null;
            }
            string password = ObsPasswordBox.Password;
            if (password.Length > 0)
            {
                _secretStore.Write(ObsPasswordCredential, password);
                ObsPasswordBox.Clear();
            }
            else
            {
                password = _secretStore.Read(ObsPasswordCredential)
                    ?? throw new InvalidOperationException("OBSパスワードを入力してください。");
            }
            using var timeout = CancellationTokenSource.CreateLinkedTokenSource(_lifetime.Token);
            timeout.CancelAfter(TimeSpan.FromSeconds(10));
            ObsWebSocketClient client = await ObsWebSocketClient.ConnectAsync(endpoint, password, timeout.Token);
            var backend = new ObsVideoCaptureBackend(client, _timeSource);
            try
            {
                await backend.StartReplayBufferAsync(timeout.Token);
                _videoBackend = backend;
                ObsStatusText.Text = (await backend.GetStatusAsync(timeout.Token)).Description;
            }
            catch
            {
                await backend.DisposeAsync();
                throw;
            }
        }
        catch (Exception exception) when (exception is not OperationCanceledException || !_lifetime.IsCancellationRequested)
        {
            ObsStatusText.Text = $"OBS接続失敗: {exception.Message}";
        }
        finally
        {
            ConnectObsButton.IsEnabled = true;
        }
    }

    private async void HotKeyPressed(object? sender, EventArgs eventArgs)
    {
        if (_hotKeyGate.TryAccept()) await CaptureAndShowAsync();
    }

    private async void CaptureClick(object sender, RoutedEventArgs eventArgs) => await CaptureAndShowAsync();

    private async Task CaptureAndShowAsync()
    {
        if (_isCapturing) return;
        if (_attachment is null || !_sessions.IsRunning)
        {
            CaptureStatusText.Text = "先にゲームへ吸着してください。";
            return;
        }
        _isCapturing = true;
        CaptureButton.IsEnabled = false;
        CaptureStatusText.Text = "画像と直前動画を保存中…";
        try
        {
            CaptureResult result = await _captureCoordinator.CaptureAsync(
                _attachment.TargetHandle,
                _videoBackend,
                _lifetime.Token);
            _currentDraft = result.Draft;
            ShowDraft(result);
            SetCollapsed(false);
            Activate();
            CommentTextBox.Focus();
            CaptureStatusText.Text = result.Warnings.Count == 0
                ? "キャプチャ完了。内容を確認して投稿してください。"
                : $"一部失敗: {string.Join(" / ", result.Warnings)}";
        }
        catch (OperationCanceledException) when (_lifetime.IsCancellationRequested)
        {
        }
        catch (Exception exception)
        {
            CaptureStatusText.Text = $"キャプチャ失敗: {exception.Message}";
        }
        finally
        {
            _isCapturing = false;
            CaptureButton.IsEnabled = true;
        }
    }

    private void ShowDraft(CaptureResult result) => ShowDraft(result.Draft);

    private void ShowDraft(StoredDraft draft)
    {
        _currentDraft = draft;
        DraftPanel.Visibility = Visibility.Visible;
        CommentTextBox.Text = draft.Text;
        bool isEditable = draft.RemoteImpressionId is null;
        CommentTextBox.IsReadOnly = !isEditable;
        SubmitDraftButton.IsEnabled = isEditable;
        SubmitDraftButton.Visibility = isEditable ? Visibility.Visible : Visibility.Collapsed;
        DiscardDraftButton.Content = isEditable ? "破棄" : "Volputasとローカルから削除";
        OpenVolputasButton.Visibility = draft.RemoteImpressionId is null
            ? Visibility.Collapsed
            : Visibility.Visible;
        StoredAsset? screenshot = draft.Assets.FirstOrDefault(asset => asset.Kind == "screenshot");
        ConfigureAssetPreview(screenshot, ScreenshotEnabledCheckBox);
        ScreenshotEnabledCheckBox.IsEnabled = isEditable;
        if (screenshot is not null)
        {
            string path = Path.Combine(_captureStore.RootDirectory, screenshot.RelativePath);
            var image = new BitmapImage();
            image.BeginInit();
            image.CacheOption = BitmapCacheOption.OnLoad;
            image.UriSource = new Uri(path);
            image.EndInit();
            image.Freeze();
            ScreenshotPreview.Source = image;
            ScreenshotPreview.Visibility = Visibility.Visible;
        }
        else
        {
            ScreenshotPreview.Source = null;
            ScreenshotPreview.Visibility = Visibility.Collapsed;
        }

        StoredAsset? video = draft.Assets.FirstOrDefault(asset => asset.Kind == "video");
        ConfigureAssetPreview(video, VideoEnabledCheckBox);
        VideoEnabledCheckBox.IsEnabled = isEditable;
        if (video is not null)
        {
            VideoPreview.Source = new Uri(Path.Combine(_captureStore.RootDirectory, video.RelativePath));
            VideoPreview.Visibility = Visibility.Visible;
        }
        else
        {
            VideoPreview.Source = null;
            VideoPreview.Visibility = Visibility.Collapsed;
        }
        ConfigureReactionPanel(draft, video);
    }

    private static void ConfigureAssetPreview(StoredAsset? asset, System.Windows.Controls.CheckBox checkBox)
    {
        checkBox.Visibility = asset is null ? Visibility.Collapsed : Visibility.Visible;
        checkBox.IsChecked = asset?.IsEnabled == true;
        checkBox.Tag = asset?.Id;
    }

    private void VideoPreviewMouseDown(object sender, MouseButtonEventArgs eventArgs)
    {
        if (VideoPreview.Source is null) return;
        VideoPreview.Play();
    }

    private void AssetEnabledClick(object sender, RoutedEventArgs eventArgs)
    {
        if (_currentDraft?.RemoteImpressionId is not null) return;
        if (sender is System.Windows.Controls.CheckBox checkBox && checkBox.Tag is Guid assetId)
        {
            _drafts.SetAssetEnabled(assetId, checkBox.IsChecked == true, _timeSource.GetUtcNow());
            if (_currentDraft is not null)
            {
                _currentDraft = _drafts.Get(_currentDraft.Id);
            }
        }
    }

    private void SubmitDraftClick(object sender, RoutedEventArgs eventArgs)
    {
        if (_currentDraft is null) return;
        if (_currentDraft.RemoteImpressionId is not null)
        {
            CaptureStatusText.Text = "登録済み投稿の内容は変更できません。削除して新しく投稿してください。";
            return;
        }
        if (!_settings.IsVolputasEnabled)
        {
            _drafts.UpdateText(_currentDraft.Id, CommentTextBox.Text, _timeSource.GetUtcNow());
            _trayIcon.Notify("Spectator", "ローカルに保存しました。");
            ClearDraftPanel();
            RefreshDrafts();
            CaptureStatusText.Text = "認証なしでローカル保存しました。";
            return;
        }
        SyncWorker syncWorker = _syncWorker
            ?? throw new InvalidOperationException("Volputas synchronization is not initialized.");
        VolputasApiClient api = _api
            ?? throw new InvalidOperationException("Volputas mode is not initialized.");
        _drafts.UpdateTextAndQueue(_currentDraft.Id, CommentTextBox.Text, _timeSource.GetUtcNow());
        syncWorker.Wake();
        _trayIcon.Notify("Spectator", "投稿を送信キューへ追加しました。");
        ClearDraftPanel();
        RefreshDrafts();
        CaptureStatusText.Text = api.IsAuthenticated ? "投稿を送信中です。" : "オフライン保存済み。ログイン後に自動送信します。";
    }

    private async void DiscardDraftClick(object sender, RoutedEventArgs eventArgs)
    {
        if (_currentDraft is null) return;
        await DeleteDraftAsync(_currentDraft);
    }

    private async Task DeleteDraftAsync(StoredDraft draft)
    {
        try
        {
            StoredDraft deleted;
            if (draft.RemoteImpressionId is null)
            {
                deleted = _drafts.DeleteLocal(draft.Id);
            }
            else
            {
                VolputasApiClient api = _api
                    ?? throw new InvalidOperationException("リモート投稿の削除にはVolputasモードが必要です。");
                await api.DeleteImpressionAsync(draft.RemoteImpressionId, _lifetime.Token);
                deleted = _drafts.DeleteAfterRemote(draft.Id, draft.RemoteImpressionId);
            }
            DeleteLocalAssets(deleted);
            if (_currentDraft?.Id == draft.Id) ClearDraftPanel();
            RefreshDrafts();
            CaptureStatusText.Text = "投稿データを削除しました。";
        }
        catch (OperationCanceledException) when (_lifetime.IsCancellationRequested)
        {
        }
        catch (Exception exception)
        {
            CaptureStatusText.Text = $"削除失敗: {exception.Message}";
        }
    }

    private void DeleteLocalAssets(StoredDraft draft)
    {
        string root = Path.GetFullPath(_captureStore.RootDirectory) + Path.DirectorySeparatorChar;
        foreach (StoredAsset asset in draft.Assets)
        {
            string path = Path.GetFullPath(Path.Combine(_captureStore.RootDirectory, asset.RelativePath));
            if (path.StartsWith(root, StringComparison.OrdinalIgnoreCase) && File.Exists(path)) File.Delete(path);
        }
    }

    private void RefreshDraftsClick(object sender, RoutedEventArgs eventArgs) => RefreshDrafts();

    private void RefreshDrafts()
    {
        Guid? selectedId = (DraftListBox.SelectedItem as StoredDraft)?.Id;
        IReadOnlyList<StoredDraft> drafts = _drafts.ListRecent();
        DraftListBox.ItemsSource = drafts;
        DraftListBox.SelectedItem = selectedId is null
            ? drafts.FirstOrDefault()
            : drafts.FirstOrDefault(draft => draft.Id == selectedId) ?? drafts.FirstOrDefault();
    }

    private void OpenDraftClick(object sender, RoutedEventArgs eventArgs)
    {
        if (DraftListBox.SelectedItem is not StoredDraft draft)
        {
            CaptureStatusText.Text = "開く下書きを選択してください。";
            return;
        }
        ShowDraft(draft);
        SetCollapsed(false);
    }

    private async void DeleteSelectedDraftClick(object sender, RoutedEventArgs eventArgs)
    {
        if (DraftListBox.SelectedItem is not StoredDraft draft)
        {
            CaptureStatusText.Text = "削除する下書きを選択してください。";
            return;
        }
        await DeleteDraftAsync(draft);
    }

    private void OpenVolputasClick(object sender, RoutedEventArgs eventArgs)
    {
        if (_currentDraft?.RemoteImpressionId is not string remoteId) return;
        Uri webBaseUrl = _settings.VolputasWebBaseUrl
            ?? throw new InvalidOperationException("Volputas web URL is not configured.");
        Uri destination = new(
            webBaseUrl,
            $"/impressions/{Uri.EscapeDataString(remoteId)}");
        Process.Start(new ProcessStartInfo(destination.AbsoluteUri) { UseShellExecute = true });
    }

    private void ClearDraftPanel()
    {
        VideoPreview.Stop();
        VideoPreview.Source = null;
        ScreenshotPreview.Source = null;
        DraftPanel.Visibility = Visibility.Collapsed;
        CommentTextBox.Clear();
        ClearReactionPanel();
        _currentDraft = null;
    }

    private void SyncStatusChanged(object? sender, SyncStatusChangedEventArgs eventArgs)
    {
        Dispatcher.BeginInvoke(() =>
        {
            CaptureStatusText.Text = eventArgs.Status switch
            {
                "submitted" => "Volputasへの投稿が完了しました。",
                "rejected" => "Volputasが投稿を拒否しました。詳細は下書き状態を確認してください。",
                _ => "ネットワーク復旧後に投稿を再試行します。",
            };
            if (eventArgs.Status == "submitted") _trayIcon.Notify("Spectator", "Volputasへの投稿が完了しました。");
            RefreshDrafts();
        });
    }

    private async void UpdateUi(object? sender, EventArgs eventArgs)
    {
        UpdateReactionPlaybackPosition();
        if (_attachment is not null && _sessions.IsRunning)
        {
            bool active = NativeMethods.GetForegroundWindow() == _attachment.TargetHandle
                && !NativeMethods.IsIconic(_attachment.TargetHandle);
            _sessions.SetActive(active);
            PlaytimeSnapshot snapshot = _sessions.Snapshot();
            ElapsedText.Text = FormatDuration(snapshot.Elapsed);
            ActiveText.Text = FormatDuration(snapshot.Active);
        }
        else
        {
            ElapsedText.Text = "00:00:00";
            ActiveText.Text = "00:00:00";
            _autoDetectTicks++;
            if (_autoDetectTicks >= 8)
            {
                _autoDetectTicks = 0;
                RefreshWindows();
                await TryAutoAttachAsync();
            }
        }
    }

    private static string FormatDuration(TimeSpan duration) =>
        $"{(int)duration.TotalHours:00}:{duration.Minutes:00}:{duration.Seconds:00}";

    private void CollapseClick(object sender, RoutedEventArgs eventArgs) => SetCollapsed(!_isCollapsed);

    private void SetCollapsed(bool collapsed)
    {
        _isCollapsed = collapsed;
        MainContent.Visibility = collapsed ? Visibility.Collapsed : Visibility.Visible;
        CollapseButton.Content = collapsed ? "開く" : "折り畳む";
        Height = collapsed ? 82 : Math.Max(620, Height);
        IntPtr handle = new WindowInteropHelper(this).Handle;
        long style = NativeMethods.GetWindowLongPtr(handle, NativeMethods.GwlExStyle).ToInt64();
        long updated = collapsed ? style | NativeMethods.WsExNoActivate : style & ~NativeMethods.WsExNoActivate;
        _ = NativeMethods.SetWindowLongPtr(handle, NativeMethods.GwlExStyle, new IntPtr(updated));
    }

    private void ShowFromTray()
    {
        Dispatcher.Invoke(() =>
        {
            Show();
            SetCollapsed(false);
            Activate();
        });
    }

    private void OnSizeChanged(object sender, SizeChangedEventArgs eventArgs) => _attachment?.Reposition();

    private IntPtr WindowMessageHook(IntPtr hwnd, int message, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if (message == WmPowerBroadcast)
        {
            int powerEvent = wParam.ToInt32();
            if (powerEvent == PbtApmSuspend) _sessions.SetSuspended(true);
            if (powerEvent is PbtApmResumeSuspend or PbtApmResumeAutomatic) _sessions.SetSuspended(false);
        }
        return IntPtr.Zero;
    }

    protected override async void OnClosing(CancelEventArgs eventArgs)
    {
        if (_allowClose)
        {
            base.OnClosing(eventArgs);
            return;
        }
        eventArgs.Cancel = true;
        if (_shutdownStarted) return;
        _shutdownStarted = true;
        _uiTimer.Stop();
        _lifetime.Cancel();
        DetachWindow();
        if (_hotKey is not null)
        {
            _hotKey.Pressed -= HotKeyPressed;
            _hotKey.Dispose();
            _hotKey = null;
        }
        if (_windowSource is not null)
        {
            _windowSource.RemoveHook(WindowMessageHook);
            _windowSource = null;
        }
        if (_videoBackend is not null)
        {
            await _videoBackend.DisposeAsync();
            _videoBackend = null;
        }
        if (_syncWorker is not null) await _syncWorker.DisposeAsync();
        await _sessions.DisposeAsync();
        if (_syncWorker is not null) _syncWorker.StatusChanged -= SyncStatusChanged;
        _api?.Dispose();
        _httpClient?.Dispose();
        _trayIcon.Dispose();
        _lifetime.Dispose();
        _allowClose = true;
        Close();
    }
}
