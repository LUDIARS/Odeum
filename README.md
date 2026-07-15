# Spectator

## リポジトリ境界

Spectator は Volputas とは別の独立ツール・独立 Git リポジトリです。既定の `standalone` モードは認証・ネットワーク・Volputas のソースコードに依存せず、ローカル動画のレビューと raw data の JSON 出力だけで動作します。任意の `volputas` モードでは公開 API と JSON 契約を通じて連携しますが、Volputas リポジトリをビルド依存やプロジェクト参照にはしません。

Spectatorは、選択したWindowsゲームの横へ吸着し、プレイ動画の時刻に本人の反応を書き込んで、感情曲線の一次データをローカル保存・JSON出力するWindowsアプリです。Volputas連携は任意で、既定のstandaloneモードは認証もネットワークも必要としません。

## 実装済み機能

- トップレベルゲームウィンドウの登録、自動再検出、DPI/複数モニター対応の吸着追従
- 常時最前面または`Ctrl+Shift+F8`による表示・キャプチャ、通知領域への常駐
- 経過時間とフォアグラウンド中のアクティブ時間、スリープ除外、クラッシュ復旧
- Windows Graphics Captureによる対象HWND限定のJPEG/PNG静止画
- OBS 28+ WebSocket 5.x Replay Bufferによる直前動画（既定20秒、15〜30秒）
- OBSの認証、必要request、program scene/source、Replay Buffer、保存先書込権限の診断
- Credential ManagerによるRefresh Token/OBSパスワード保存
- loopback PKCEログイン、session heartbeat、冪等なimpression登録、署名URLアップロード
- SQLite WALの下書き・再送キュー、指数バックオフ、再起動復旧、履歴・削除
- 投稿前プレビュー、画像/動画の添付解除、Volputas投稿詳細画面の起動
- 動画再生時刻に紐づく本人の自由記述、SQLite保存、UTF-8 JSON raw data出力
- 録画済みMP4/MKV/WebMのローカルimport、再起動後のレビュー継続
- シーク位置への通常コメント、`ここ良かった`、`ここ悪かった`スタンプ
- Volputas timeline importer互換の`utterances`、`gameId`、`sourceRef`出力
- `standalone` / `volputas`の明示モード分離（standaloneは認証・Volputas URL不要）

動画バックエンドは設計上交換可能ですが、内蔵WGC/WASAPI/Media Foundation版は実機品質ゲート（GPU三社、A/V drift、2時間・100回保存）を未検証のため出荷選択肢に含めていません。品質ゲートに従い、現在の正式バックエンドはOBSです。静止画はOBSに依存しません。

## 設定

`src/Spectator.Windows/spectator.settings.json`で連携モード、OBS loopback URL、静止画形式、動画有無、Replay秒数を設定します。既定は`"integrationMode": "standalone"`です。

Volputasへ投稿する場合だけ`integrationMode`を`volputas`へ変更し、`volputasApiBaseUrl`、`volputasWebBaseUrl`、`loginProvider`を追加します。非loopbackのVolputas URLはHTTPS必須です。必要項目が欠けたVolputasモードは起動時に明示的に失敗します。

## 本人の反応raw data

1. ゲームIDまたは識別名を入力し、「ローカル動画を読み込む」で録画済み動画を選びます。
2. 動画を再生するか、シークバーで感情が変化したポイントへ移動します。
3. 自由記述は「コメント」、良い変化点は「ここ良かった」、悪い変化点は「ここ悪かった」で記録します。スタンプに補足したい場合は先にコメント欄へ入力します。
4. 「JSONを出力」でraw dataを書き出します。

Spectatorは感情を推定・補完・スコア化しません。JSONの`utterances[].content`は本人の入力をそのまま保持し、`videoOffsetMs`で動画時刻、`reactionKind`で`comment` / `positive` / `negative`を表します。契約は[`spec/reaction-raw-data.schema.json`](spec/reaction-raw-data.schema.json)を参照してください。

## ビルドとテスト

```powershell
dotnet restore Spectator.sln
dotnet build Spectator.sln -c Release --no-restore
dotnet run --project tests/Spectator.Core.Tests -c Release --no-build
dotnet run --project tests/Spectator.Windows.Tests -c Release --no-build
./eng/Run-Standalone-Smoke.ps1
./eng/Test-Obs-Prerequisites.ps1
./eng/Test-Obs-Prerequisites.ps1 -RequireRunning
```

Release実行ファイルは`src/Spectator.Windows/bin/Release/net8.0-windows10.0.19041.0/Spectator.exe`、ローカルデータは`%LOCALAPPDATA%\Spectator`に保存されます。

詳細は[設計書](spec/design.md)と[テスト計画](spec/test-plan.md)を参照してください。
