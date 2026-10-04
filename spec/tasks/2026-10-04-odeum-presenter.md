---
task: odeum-presenter
project: Od
kind: implementation
created: 2026-10-04
memory_links:
  - spec/plan/2026-10-04-odeum-live-presentation-design.md
  - spec/feature/live-relay.md
---

# Odeum 発表者アプリ (odeum-presenter)

設計の正本: `spec/plan/2026-10-04-odeum-live-presentation-design.md` の「発表者アプリ」節と、中継サーバの実装仕様 `spec/feature/live-relay.md`。最小版・スタブで済ませない。

## 背景 (neco 2026-10-04)

GLab 向けのライブプレゼン配信。発表者が自分の画面を配信し、GLab ユーザーがブラウザからグッド・スタンプ・コメント・投票回答を返す。中継サーバ `odeum-relay` と共通プロトコル `odeum_protocol` は `native/` に実装済み (マージ済み)。GLab 側 (`odeum` プラグイン、チケット発行と `odeum://` リンク) もマージ済み。Tela には macOS / Windows のデスクトップ重ね表示 (`tela::MacOSDesktopOverlay` / `tela::WindowsDesktopOverlay`、`DesktopOverlayOptions::exclude_from_capture`) がマージ済み。**描画は Tela/Pictor、Mac 対応が必須** (neco)。

## やること

`native/presenter/` に実行ファイル `odeum-presenter` を作る (C++20 / macOS は Objective-C++)。`native/CMakeLists.txt` に `ODEUM_BUILD_PRESENTER` (既定 OFF) を足し、ON のときだけ Tela/Pictor を要求する (中継サーバだけのビルドと審査を壊さない)。

1. **起動と接続**: `odeum://present?relay=<wss URL>&ticket=<JWS>` を受けて起動 (macOS は Info.plist の URL scheme + `NSAppleEventManager` の GetURL、Windows は HKCU のURL プロトコル登録を `--register-url-scheme` で行う)。引数に URL を直接渡す起動と、操作パネルへの貼り付けも受ける。`odeum_protocol` で `welcome` 以降のメッセージを扱う。WebSocket と WebRTC は libdatachannel (中継と同じ版)。切断時は指数バックオフで再接続し、チケット期限切れ (5 分) なら GLab で再発行するよう表示する。
2. **取り込み (OS 別、インタフェースで分離)**: `CaptureSource` (ディスプレイ / ウインドウの列挙と選択、フレーム供給) と `VideoEncoder` (H.264 Constrained Baseline、packetization-mode=1、キーフレーム要求に応答) を Core の抽象にし、
   - macOS: ScreenCaptureKit (`SCStream`、`SCContentFilter` で自分のウインドウを除外) + VideoToolbox (`VTCompressionSession`)。画面収録の権限が無いときは案内を出す。
   - Windows: Windows Graphics Capture + Media Foundation H.264 エンコーダ。
   既定 1080p / 30fps、ビットレート上限 6 Mbps (設定可)。中継から届く PLI/FIR (キーフレーム要求) で IDR を出す。音声は任意 (既定 OFF、ON で Opus)。
3. **描画 (Tela/Pictor)**: デスクトップ重ね表示 (`exclude_from_capture = true`) に次を出す。
   - グッドの湧き上がりと累計 (`reaction.burst` の `good`)。連打が多いほど派手に。
   - スタンプ (押した人の名前つき、数秒で消える)。
   - コメントの流れ (任意文字列。表示 ON/OFF 可)。
   - 投票の集計 (`tally`)、視聴者数 (`presence`)。
   - 位置は四隅から選べ、つまみで移動。
4. **操作パネル** (Tela の通常ウインドウ): 開始/停止、取り込み対象の選択、投票の作成 (質問・2〜6 択・複数選択可否) / 開始 / 終了、コメント表示の ON/OFF、接続状態。
5. **ビルド**: macOS は `-DODEUM_BUILD_PRESENTER=ON -DTELA_BUILD_MACOS=ON` 相当で Tela と Pictor を `find_package` または明示パスで受ける (Tela の `spec/macos-composition.md` に従い Pictor は同一アーキでビルドしたものを明示)。Windows も同様。依存の取得方針は中継と同じ (FetchContent は版とハッシュ固定、Tela/Pictor は LUDIARS のローカル checkout をパス指定)。
6. **ドメインと仕様**: `spec/domains/live-presenter.domain.json` を足し新規ファイルをすべて所属させる。`spec/feature/live-presenter.md` に起動方法・権限・設定・制約を書く。
7. **テスト** (`native/tests/`、CTest): URL の解析と検証 (scheme / wss 以外拒否 / ticket 欠落)、再接続のバックオフ、受信メッセージから重ね表示の状態への変換 (burst の累計、スタンプの寿命、コメントの上限件数、tally)、投票作成の入力検証、キーフレーム要求の伝搬 (エンコーダは差し替え可能な偽物で検証)。OS API に触れない部分を Core に寄せてテストする。

## 受入条件

- `ODEUM_BUILD_PRESENTER=OFF` (既定) で中継サーバのビルドと ctest が今までどおり通る。
- Windows で `ODEUM_BUILD_PRESENTER=ON` のビルドが通る構成 (Tela は `-DTELA_BUILD_WINDOWS=ON`)。
- macOS 向けコードは Mac 拠点でのビルドが必要。実行は依頼者の許可範囲に従い、未実施は PR に未実施と明記する。
- 実機での画面取り込み・配信・ブラウザ視聴の確認は人間の許可後。

## 作業の進め方

- この worktree で作業し、ブランチは現在のものを使う。`git add <パス>` + `git commit` は可 (パス指定、`git add -A` は使わない。`native/build*` はコミットしない)。
- ビルドはフォアグラウンドで完了まで待つ (バックグラウンド待ちにしない)。
- Anatomia は `node E:/Document/Ars/Anatomia/bin/anatomia.mjs` で呼ぶ。
- local PR は Concordia の `/v1/prs/local/direct` で提出する (本文に `## 実装内容` と `## 受け入れ条件` の節が必須)。完了報告は delegation status へ送る。Actio の本文 API が取れなければこのファイルを本文として扱う。
