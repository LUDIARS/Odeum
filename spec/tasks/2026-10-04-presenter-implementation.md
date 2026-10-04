# 発表者アプリの実装記録

Actio: `actio:a33eca3e-d732-4b8c-9249-e39d08b44d57`
タスク定義: [2026-10-04-odeum-presenter.md](2026-10-04-odeum-presenter.md)
仕様: [live-presenter](../feature/live-presenter.md)

## 作業単位

- Core: 起動リンク (`launch_url`)、再接続 (`reconnect_backoff` / `connection_monitor`)、重ね表示の状態
  (`overlay_state`)、投票 (`poll_draft`)、キーフレーム要求 (`keyframe_request` / `video_pipeline`)、
  H.264 / NV12 / PCM の整形、設定と起動引数、`EventQueue`。取り込み・符号化・音声の抽象。
- 通信: `RelayLink` (WebSocket)、`MediaSender` (PeerConnection、H.264/Opus の sendonly トラック)、Opus 符号化。
- アプリ: `PresenterController`、重ね表示と操作パネルの Tela 宣言、表示文言、設定ファイル、`PresenterApp`。
- macOS: ScreenCaptureKit (映像・音声)、VideoToolbox、URL スキーム (GetURL)、NSPasteboard、Info.plist。
- Windows: Windows Graphics Capture (WRL/ABI)、Media Foundation H.264 MFT、WASAPI ループバック、
  HKCU の URL プロトコル登録、`WM_COPYDATA` による起動中インスタンスへの受け渡し。
- ビルド: `ODEUM_BUILD_PRESENTER` / `ODEUM_BUILD_PRESENTER_CORE` (既定 OFF)、libopus の固定取得、
  Tela はローカル checkout のパス指定。
- 仕様・ドメイン・契約: `live-presenter.md`、`live-presenter.domain.json`、C-7〜C-12。

## 再利用の判断

- 採用: `odeum_protocol` の `parse_message` / `serialize_message` / `utf8_length` (受信と送信前の検証、文字数)、
  中継と同じ libdatachannel / nlohmann/json、Tela の `DesktopOverlayOptions` / `WindowsView` / `place_on_desktop`。
- 不採用: 中継の `contains_keyframe_request` (中継の transport ライブラリに属し Boost/Asio を引き込むため。
  同じ判定を Core に持ち、FIR も扱う)。既存 .NET アプリ (`src/Spectator.*`) は責務も実行環境も異なる。
- C++/WinRT は SDK 10.0.19041 同梱版が MSVC 14.39 の C++20 と両立しないため使わず、WRL で ABI を呼ぶ。

## テスト計画

`augur plan --kind new_feature --domain service` の 3 提案に対応させる。OS API に触れず、時刻は注入する。

| 提案 | テスト |
|---|---|
| 正常系 | `presenter_url_tests` (GLab 形式のリンク)、`presenter_overlay_tests` (burst 累計・スタンプ・tally)、`presenter_poll_tests` (poll.open の生成)、`presenter_keyframe_tests` (偽エンコーダで最初の IDR と PLI/FIR による IDR) |
| 利用側契約 | 生成した `poll.open` / `poll.close` が `parse_message` を通る、受信は `parse_message` 済みのメッセージで検証、設定 JSON の往復、偽エンコーダへの出力が送信側へ届く |
| 境界・不正入力 | scheme / action / wss 以外 / ticket 欠落・形式違い / 重複キー / 壊れた % 、バックオフの上限と exp 境界、コメント上限件数とスタンプ寿命の境界、選択肢 1/7 件・61 文字・重複・不正 UTF-8、RTCP の長さ不正と他種 PSFB、AVCC の長さ超過、奇数サイズの NV12 |

## 検証結果

- `ODEUM_BUILD_PRESENTER_CORE=ON` (Tela なし) の configure と、Core + presenter テスト 6 本のビルド: 成功 (Windows / MSVC 19.39)。
- `ODEUM_BUILD_PRESENTER=ON` (Windows、Tela origin/main `1cc5c3b` を `git archive` で展開、Pictor `build/Release/pictor.lib`) の
  configure と `odeum-presenter.exe` のビルド: 成功。同じツリーで `odeum-relay` と既存テスト 4 本のビルドも成功。
  presenter のソースから警告なし。
- `ctest -N`: 既存 4 本 + presenter 6 本の 10 本だけが登録される (Tela・libopus のテストは登録しない)。
- ctest の実行、アプリの起動、実機での画面取り込み・配信・ブラウザ視聴: 未実施 (テスト実行と起動の指示がないため)。
- 既定 (`ODEUM_BUILD_PRESENTER=OFF`) の configure: 成功。presenter のターゲット・Tela・libopus は構成に入らず、
  `ctest -N` は既存 4 本のまま。中継のターゲットは上記 ON のツリーでビルド済み (OFF ツリーでのビルドは未実施)。
- macOS: Mac 拠点でのビルドが必要で未実施 (ScreenCaptureKit / VideoToolbox / AppKit のコードはコンパイル未確認)。
- `Anatomia verify --json`: pass。新規ファイルはすべていずれか 1 つのドメインに所属 (テストは test-support)。
- Augur inject: C++ は注入対象外で `applied=0 / unresolved`。report は C-1〜C-12 すべて `uncovered (not-injected)`。
  観測済みとは申告しない。`DELEGATION_STARTED_AT` が環境に無いため run の created_at を `--since` に使った。

## 提出時の未充足事項

- Mac でのビルドと実行、Windows/Mac 実機での取り込み・配信・視聴の確認 (人間の許可後)。
- ctest の実行 (指示があれば)。
- Tela のローカル checkout (`E:/Document/Ars/Tela`) は origin/main より古く `DesktopOverlayOptions` を含まない。
  この checkout を `ODEUM_TELA_SOURCE_DIR` に渡す前に main を更新する必要がある (この run では Tela を変更していない)。
