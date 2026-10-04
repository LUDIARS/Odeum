# Odeum ライブプレゼン配信 設計 (2026-10-04)

neco 指示 (2026-10-04):

- GLab ツールとして、本人がプレゼンしている画面を WebRTC か高速なキャストで受け取り、各 GLab ユーザーがリアクションや回答を返せる仕組みを用意する。
- 担当は Volputas の相棒 (旧 Spectator、本日 **Odeum** へ改名。GitHub `LUDIARS/Odeum`、Cc 略称 `Od`)。
- 描画は Tela/Pictor、Mac に対応する。
- GLab と連携し、GLab にログインしたとき発表中のイベントがあれば確認できるようにする。
- 視聴とリアクションは **GLab の Web 画面** (neco 選択)。
- 中継サーバは Node を使わずネイティブ。**中継サーバだけ Astra に作らせる**。

## 全体構成

| 部品 | リポ / 置き場所 | 言語 | 担当 |
|---|---|---|---|
| 共通プロトコル `odeum_protocol` | Odeum `native/protocol/` | C++20 | Astra (中継と同じ run) |
| 中継サーバ `odeum-relay` | Odeum `native/relay/` | C++20 + libdatachannel | **Astra** |
| 発表者アプリ `odeum-presenter` | Odeum `native/presenter/` | C++20 / Objective-C++ + Tela/Pictor | 別 run (中継マージ後) |
| Mac デスクトップ重ね表示 | Tela `src/infrastructure/macos/` | Objective-C++ | 別 run |
| 視聴画面・発表一覧 | GLAB `plugins/odeum/` | TypeScript (Corpus プラグイン) | 別 run |

既存の .NET Windows アプリ (`src/Spectator.*`) は変更しない (改名に伴う表示名・ドキュメント更新は別タスク)。

```
発表者 Mac ─ odeum-presenter ──WebRTC (H.264 送信)──▶ odeum-relay (SFU, :4400) ──▶ 視聴ブラウザ (GLab Web)
   ▲ Tela 重ね表示でリアクション/回答                     │  WebSocket シグナリング + リアクション/回答
   └──────────────── 集計・リアクションを返送 ────────────┘
GLab サーバ ── 発表セッション台帳 (GLAB DB)・チケット発行 ── GET /v1/sessions で中継の生存状態を取得
```

## 責務の境界

- **GLab が正本**: 発表セッション台帳 (どのイベントで誰が発表中か)、権限 (誰が発表/視聴できるか)、チケット発行。ログイン時の「発表中のイベント」表示。
- **odeum-relay**: メディア中継とリアルタイムのリアクション/回答の受け渡しだけ。永続データを持たない (再起動でセッション状態は消え、発表者アプリは再接続する)。ユーザー認証はしない (チケット署名の検証だけ)。
- **odeum-presenter**: 画面取り込み・符号化・送信、受信したリアクション/回答/集計を Tela で描画、投票の開始/終了。
- 個人データ: チケットとリアクションに載るのは Cernere `user_id` と表示名のみ。中継はログに表示名・本文を残さない (件数と session_id のみ)。

## チケット (GLab → 中継の認証)

- 形式: JWS compact、`alg: EdDSA` (Ed25519)。`kid` 必須。
- 秘密鍵は GLab が既存の秘密保管経路で保持する。中継は公開鍵ファイル (`ODEUM_RELAY_TICKET_PUBKEYS`、`kid` → PEM の JSON ファイルパス) を読む。公開鍵は秘密ではない。
- claims: `iss: "glab"`, `aud: "odeum-relay"`, `sub` (Cernere user_id), `name` (表示名, ≤64 文字), `role` (`presenter` | `viewer` | `service`), `sid` (session_id, `service` では省略), `exp` (発行から最大 5 分。接続確立時にのみ検証し、確立後の WebSocket は切らない), `jti`。
- 中継は `jti` を `exp` まで記憶し再利用を拒否する。

## 中継 API (odeum-relay, 既定 :4400)

- `GET /health` → `{"ok":true,"version":"x.y.z"}`
- `GET /v1/sessions` (Bearer: `role=service` チケット) → `[{"sid","presenter_connected","viewer_count","started_at"}]`
- `GET /v1/ws?ticket=<JWS>` → WebSocket。1 接続 = 1 参加者。
- 待ち受けは既定 `127.0.0.1`。外部公開は Cloudflare Tunnel / リバースプロキシ前提 (TLS 終端は外)。WebRTC の UDP は `ODEUM_RELAY_UDP_PORT_RANGE` で範囲指定、`ODEUM_RELAY_PUBLIC_IP` で候補アドレスを指定、STUN/TURN は設定で渡す。

### WebSocket メッセージ (UTF-8 JSON、1 メッセージ ≤ 16 KiB)

共通: `{"type": "...", ...}`。未知 type は `error` を返して無視。

| 方向 | type | 内容 |
|---|---|---|
| S→C | `welcome` | `sid`, `role`, `self` {`sub`,`name`}, `ice_servers` |
| C↔S | `sdp` | `sdp` {`type`:`offer`/`answer`, `sdp`} |
| C↔S | `candidate` | `candidate`, `mid` |
| 視聴→S | `good` | `count` (1〜50)。クライアントが連打を 200ms ごとにまとめて送る |
| 視聴→S | `stamp` | `kind` (`clap`/`laugh`/`wow`/`question`/`agree`) |
| 視聴→S | `comment` | `text` (任意文字列, ≤280 文字) |
| 発表→S | `poll.open` | `poll_id`, `question`, `choices` (2〜6 件, 各 ≤60 文字), `multi` (bool) |
| 発表→S | `poll.close` | `poll_id` |
| S→視聴 | `poll.open` / `poll.closed` | 同上 / `poll_id`, `tally` |
| 視聴→S | `poll.answer` | `poll_id`, `choices` (index 配列) 。1 人 1 回、再送は上書き |
| S→発表 | `stamp` / `comment` | `from` {`sub`,`name`}, 本体、`at` (グッドは個別に送らず `reaction.burst` だけ) |
| S→全員 | `tally` | `poll_id`, `counts`, `answered` (1 秒以上の間隔でまとめて送る) |
| S→全員 | `reaction.burst` | `good` (n), `stamps` {kind: n} (250ms ごとに集計、個人名なし、0 件なら送らない) |
| S→全員 | `presence` | `presenter_connected`, `viewer_count` |
| S→C | `error` | `code`, `message` |

- リアクションは 3 系統に分ける (neco 2026-10-04): **グッド** (連打前提、件数だけが意味を持つ)、**スタンプ** (種類つき、誰が押したか発表者に見える)、**コメント** (任意文字列)。
- 流量制限: 視聴者 1 人あたり グッド 合計 30 回/秒 (超過分は切り捨てて受理、エラーにしない)、`stamp` 2 回/秒、`comment` 1 回/3 秒。stamp/comment の超過分は捨てて `error code=rate_limited`。
- 上限: 1 セッションの視聴者 300、同時セッション 32 (設定可)。

### メディア

- 発表者は H.264 (Constrained Baseline / packetization-mode=1) 映像 1 本 + 任意で Opus 音声 1 本を送信する。
- 中継は SFU として RTP を各視聴者の PeerConnection へ転送する (再符号化しない)。視聴者からの PLI/FIR は発表者へまとめて (最短 1 秒間隔) 転送し、新規視聴者参加時もキーフレームを要求する。
- 視聴者は受信専用 (`recvonly`)。ブラウザ標準 WebRTC で再生できること。

## 発表者アプリ (odeum-presenter)

- 起動: GLab から `odeum://present?relay=<wss URL>&ticket=<JWS>` で起動 (macOS は URL scheme 登録、Windows はレジストリの URL プロトコル)。手動起動時は URL 貼り付け欄。
- 取り込み: macOS は ScreenCaptureKit (ディスプレイ単位 / ウインドウ単位を選択)、符号化は VideoToolbox H.264。Windows は Windows Graphics Capture + Media Foundation H.264。取り込み/符号化はインタフェースで分け、Core は OS API を持たない (Tela と同じ方針)。
- 送信: libdatachannel。既定 1080p/30fps・可変ビットレート上限 6 Mbps (設定可)。
- 描画: Tela/Pictor。デスクトップに浮かぶ重ね表示に、グッドの湧き上がりと累計 (`reaction.burst`)、スタンプ (押した人の名前つき)、コメントの流れ、投票の集計、視聴者数を出す。重ね表示は取り込み対象から除外する (macOS は SCContentFilter で自ウインドウを除外)。
- 操作パネル: 開始/停止、取り込み対象選択、投票の作成/開始/終了、コメント表示の ON/OFF。

## Tela: macOS デスクトップ重ね表示

Windows の `tela::WindowsDesktopOverlay` (`spec/feature/desktop-overlay.md`) と同じ契約を macOS に足す。対象ウインドウ無し・最前面・非アクティブ・排他領域以外はクリック透過・メインディスプレイ作業領域の隅 / 絶対位置・ディスプレイごとの倍率追従・つまみで移動。NSPanel (nonactivating) + `ignoresMouseEvents` の領域切替で実装し、Core に AppKit を持ち込まない。

## GLab: `odeum` プラグイン

- 発表セッション台帳を GLAB DB に持つ (`glab_odeum_sessions`: id, event_id, presenter user_id, status `live`/`ended`, started_at, ended_at)。イベントの正本は既存の GLAB イベントストア。
- 発表開始: イベント詳細から「発表を始める」(権限: イベント作成者または GLab 管理者)。セッションを作り presenter チケットを発行して `odeum://` リンクを出す。
- ログイン時: ダッシュボード上部に「いま発表中」カードを出す (live セッションがあるときだけ)。中継の `GET /v1/sessions` で presenter_connected を確認し、切断中は「接続待ち」表示。
- 視聴画面: viewer チケットで中継に接続し `<video>` 再生、投票への回答、視聴者数。リアクションは 3 つの領域に分ける。
  - **グッド**: 画面で最も大きいボタン (スマホでは親指で連打できる幅いっぱい・高さ 96px 以上)。押すたびに即時の手応え (アニメーションと自分の押下数) を出し、送信は 200ms ごとにまとめる。長押しで連打扱いにはしない。
  - **スタンプ**: グッドとは別の列に小さめのボタンを並べる (`clap`/`laugh`/`wow`/`question`/`agree`)。
  - **コメント**: 任意文字列の入力欄と送信ボタン。スタンプ列とは分けて配置する。
- 中継の URL は Excubitor topology から解決する (`ODEUM_RELAY_URL`、ハードコードしない)。

## Excubitor / ブートストラップ

- Odeum の `excubitor.catalog.yaml` に `odeum-relay` (port 4400, runtime native, autostart false, health `/health`) を宣言する。
- `excubitor.bootstrap.json` と `scripts/site/setup.mjs` (CMake configure/build) / `export-data.mjs` / `import-data.mjs` (永続データ無しの明示的な空形式) を用意する (Castra `.agents/skills/service-bootstrap/SKILL.md`)。

## 検証の扱い

- Windows 上で build・単体テストまで。macOS 向けコードは Mac 拠点 (Excubitor peer) でのビルドが必要で、実行は人間の許可範囲に従う。未実施はそのまま PR に書く。
- 実機の画面取り込み・ブラウザ視聴の確認は人間の許可後に行う。
