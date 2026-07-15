# Release policy

`eng/Build-Release.ps1`はself-contained単一EXE、設定、ユーザー単位installer/uninstaller、SHA-256一覧、ZIPを`artifacts/release`へ生成します。通常ユーザーでインストールでき、管理者権限・ゲームprocessへのinject・driverは不要です。

```powershell
.\eng\Build-Release.ps1 -Runtime win-x64
```

出力にはZIPと `spectator.release/v1` manifestが含まれます。manifestはversion、runtime、archive名、SHA-256、署名状態、UTC生成日時を保持します。配布先が決まった場合は`-ArchiveUrl https://...`を指定します。HTTP URLは拒否します。

コード署名時はWindows証明書storeにあるAuthenticode証明書のthumbprintを`SPECTATOR_SIGN_CERT_SHA1`へ設定します。証明書・private key・passwordはrepositoryやartifactへ格納しません。署名時はDigiCert timestampを付与します。

自動更新は、署名済みreleaseとHTTPS配信基盤が用意されるまでアプリから無断適用しません。配布時はversion、ZIP URL、SHA-256、公開日時を持つHTTPS manifestをVolputas管理下で公開し、Spectatorは通知のみ、ユーザー操作で署名済みinstallerを起動する方針です。downgrade、未署名binary、hash不一致は拒否します。

## 動画バックエンド出荷ゲート

初回リリースで選択可能な動画バックエンドはOBS Studio 28+ Replay Bufferと`none`だけです。`eng/Test-Obs-Prerequisites.ps1`でインストールとversionを、`-RequireRunning`で実行中processとWebSocket 4455待受を検証します。その後、アプリのOBS接続操作で認証、必要request、program scene/source、保存先書込、Replay Buffer開始をfail-fast確認します。

内蔵連続録画はGPU三社、2時間A/V drift、100回連続保存、sleep/resume、display scale切替の実機matrixを通過するまで設定値として受け付けません。
