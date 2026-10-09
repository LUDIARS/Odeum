# Odeum 番組配信の自作 設計 (2026-10-09)

neco 指示 (2026-10-09): 「Odeum の配信設計を自作できないか検討して」→ 検討結果の A 案「自作で進める」を選択。

前提の設計: `spec/plan/2026-10-04-odeum-live-presentation-design.md` (ライブプレゼン配信)。
置き換える対象: Odeum `broadcast/` (OBS + MediaMTX) と Cocoiru `apps/tela-resident/src/sharing/` (FFmpeg + SRT)。

## 結論

番組配信 (4 入力の切替・合成・リアクション合成・YouTube Live 送出・送信元への番組返送) を、
外部ソフト 3 本 (OBS、MediaMTX、FFmpeg) に頼らず、既存のライブプレゼン系統 (odeum-presenter /
odeum-relay / Tela / Pictor) に寄せて自作する。WebRTC スタック (libdatachannel) は自作しない。

| 置き換え前 | 置き換え後 | 新規に書く量の目安 |
|---|---|---|
| Cocoiru → FFmpeg (gdigrab + x264/NVENC + MPEG-TS/SRT) | Cocoiru → `odeum_sender` ライブラリ (odeum-presenter の取り込み・符号化・WebRTC 送信層を共通化) | Cocoiru 側 400 行、Odeum 側はライブラリ化のみ |
| MediaMTX (SRT ルータ) | odeum-relay の room に入力スロット (input1〜4) と番組スロット (program) を追加 | 150 行 |
| OBS (復号・合成・リアクションのブラウザソース・符号化・RTMPS) | 新プロセス `odeum-program`: 復号 (OS 標準 API) → Pictor オフスクリーン合成 + Tela 操作パネル → 既存 H.264 符号化器 → FLV/RTMPS で YouTube | 2,200 行 |
| OBS 録画出力 → SRT 返送 | odeum-program が番組を relay の program スロットへ送り、Cocoiru は viewer として受けて復号表示 | 返送は既存 SFU 経路、Cocoiru の復号 150 行 |

残すもの: libdatachannel (MPL-2.0)、libopus、nlohmann/json、Boost.Beast、OpenSSL 3。追加の外部依存は無し。
消すもの (自作系統の YouTube 限定公開確認が取れた後に別 PR で): `broadcast/`、`scripts/broadcast/`、
`excubitor.bootstrap.odeum-broadcast.json`、catalog の `odeum-broadcast` / `odeum-obs`、Cocoiru の FFmpeg 子プロセス管理。

## 全体構成

```
Cocoiru 送信元 ×4 (Windows) ── odeum_sender (WGC + MF H.264 + Opus) ──WebRTC sendonly──▶ odeum-relay room
                                                                                         │ slot input1..4
親機 (VANMAC / GROMAC, macOS) ── odeum-program ◀──WebRTC recvonly ×4 (producer チケット)──┘
   │ VideoToolbox 復号 ×4 → Pictor オフスクリーン 1080p 合成 (切替 / 4 分割 / 待機 / 終了 + リアクション層)
   │ Opus 復号 ×4 → ミキサ → AAC (AudioToolbox)        VideoToolbox H.264
   ├──FLV / RTMPS──▶ YouTube Live (rtmps://a.rtmps.youtube.com:443/live2/<key>)
   └──WebRTC sendonly (slot program, 640x360)──▶ odeum-relay ──▶ Cocoiru 返送プレビュー (viewer) / GLab Web 視聴
GLab ── 発表セッション台帳・チケット発行 (producer / 送信元 presenter+slot / viewer)
スマホ ── /join (ゲスト) ── グッド / スタンプ / telop / submission ── relay ── odeum-program の合成面に描画
```

Tela の操作パネルは親機のデスクトップに出す (既存の desktop overlay 契約)。番組面そのものはウインドウを持たない
オフスクリーン描画で、取り込み除外の心配が無い。

## 責務の境界

- **GLab (正本)**: 発表セッション台帳、権限、チケット発行。番組セッションではチケットに `slot` と `role=producer` を載せる。
  参加コード / overlay 鍵の発行は既存どおり。overlay 鍵は odeum-program がブラウザ無しで同じ broadcast を受けるため不要になるが、
  互換のため残す。
- **odeum-relay**: メディア中継とリアクションの受け渡しだけ。永続データ無し・再符号化しない (既存不変)。
  追加は「room 内の複数ソース」の管理と、producer への全スロット配信。
- **odeum-program (新規)**: 入力の復号・合成・音声ミックス・番組符号化・YouTube 送出・番組返送・操作パネル。
  YouTube のストリームキーは設定ファイル (ユーザ毎・平文禁止: macOS は Keychain、Windows は DPAPI) に持ち、ログ・PR・テンプレートに出さない。
- **Cocoiru**: 送信元の選択 UI と返送プレビュー窓 (既存 UX は変えない)。送信・符号化・復号は odeum_sender / 復号器を link する。
- **個人データ**: 既存どおり Cernere `user_id` と表示名だけ。番組面に出す本文は送信者が `show_on_screen=true` にしたものだけ。

## odeum-relay の拡張 (run 1)

### チケット

- presenter チケットに任意の `slot` claim を追加: `"input1"`〜`"input4"` または `"program"`。省略時は `"program"` (従来の 1 発表者構成と互換)。
- 新しい role `producer`: 同じ `sid` の room の全入力スロットを受信し (`recvonly` ×4)、`program` スロットを送信する (`sendonly` 1 本)。
  room に producer は 1 接続まで。producer は視聴者数に数えず、リアクションの投稿はできない (`forbidden`)。全員向け broadcast は受け取る。
- viewer / guest / overlay は変更なし。`invite` claim の規則も変更なし (producer チケットにも `invite` を載せてよい)。

### room とメディア

- `Room.presenter` を `std::map<std::string /*slot*/, std::uint64_t>` に広げる。room の寿命は「いずれかのスロットに接続がある間」。
  `program` スロットの切断では room を閉じない (入力が残っていれば次の producer が再接続できる)。全スロットが空になったら従来どおり破棄。
- `MediaRoom::sources_` をスロット別に持つ。viewer への配信元は `program` があればそれ、無ければ `input1`〜 の若い順で最初に接続中のもの。
  配信元が切り替わったら viewer へ新しい offer (renegotiation) を送り、キーフレームを要求する。
- producer は join 時に接続中の全入力スロットの offer を受ける。後から入力が増減したら追加の offer / `track.closed` で追随する。
- `presence` に `slots: {"input1": true, ...}` と `program_connected` を足す。`GET /v1/sessions` にも同じ項目を足す。
- PLI/FIR の集約は スロットごと (producer からの要求はそのスロットの送信元へ、viewer からの要求は配信元へ)。
- 上限: 入力スロット 4 (設定 `ODEUM_RELAY_MAX_INPUTS`、1〜8)。

### 受入条件 (run 1)

1. 単体テスト: `slot` claim の検証 (未知値 / viewer に付いたら invalid)、producer の権限 (投稿 forbidden、視聴者数に不算入)、
   スロット別の presenter 重複拒否、配信元の選択順序と切替、room 寿命 (program 切断で閉じない / 全空で閉じる)、presence の項目。
2. 既存テスト (protocol / ticket / relay-state / guest / overlay) が全部通る構成のまま。`slot` 省略の既存クライアントの振る舞いが変わらない。
3. `spec/feature/live-relay.md` に slot / producer / presence の追加を書く。
4. Windows で `cmake -S native -B native/build` と Release ビルドが通る構成。テスト実行は人間の許可範囲に従い、未実施は PR に書く。

## odeum-program (run 2)

置き場所: Odeum `native/program/`。実行ファイル `odeum-program`。依存は presenter と同じ (odeum_protocol、libdatachannel、libopus、Tela、Pictor) + OS 標準 API。

| 層 | 置き場所 | 依存 | 責務 |
|---|---|---|---|
| Core | `native/program/core/` | odeum_protocol のみ | 番組状態 (入力の接続状態、選択中レイアウト、待機 / 終了)、レイアウト計算 (全画面 1〜4 / 4 分割 / 待機 / 終了の矩形)、音声ミキサ (float PCM、スロット別ミュート、クリップ防止)、FLV 多重化 (AVC/AAC sequence header、タグ、タイムスタンプ)、RTMP の握手と AMF0 (connect / releaseStream / FCPublish / createStream / publish、chunk 分割、ack window)、YouTube 送出の再接続判断、設定。OS API を持たず CTest で検証する |
| 復号 | `native/program/platform/{macos,windows}/` | VideoToolbox / Media Foundation、AudioToolbox / MF AAC | H.264 Annex B → NV12 の復号 (スロット 4 本)、AAC-LC 48 kHz ステレオ符号化。Opus 復号は libopus |
| 合成 | `native/program/render/` | Pictor | NV12 4 本をテクスチャへ上げ、1920x1080 のオフスクリーン render target に矩形配置し、リアクション層 (グッドの湧き上がり・スタンプ・公開 submission・telop・投票集計・視聴者数) を presenter と同じ OverlayState から描く。GPU 読み戻しで NV12 (または BGRA → NV12 変換) を取り出す。Pictor の headless 経路 (`enable_swapchain_transfer_src` 相当のオフスクリーン target) を使い、無ければ Pictor 側に最小の offscreen target + readback を足す (別 PR、Pictor は PUBLIC リポ) |
| 通信 | `native/program/transport/` | libdatachannel、OpenSSL | relay との WebSocket (producer チケット)、PeerConnection (recvonly ×4 + sendonly program)、RTMPS (TLS は OpenSSL、FLV/RTMP の組み立ては Core) |
| アプリ | `native/program/app/` | Tela Core + 既存 desktop overlay | 操作パネル: 入力 1〜4 のサムネイル (接続状態)、全画面 1〜4 / 4 分割 / 待機 / 終了の切替、スロット別ミュート、番組音量、YouTube 送出の開始 / 停止、返送の開始 / 停止、キー設定、状態 (送出ビットレート / ドロップ / 再接続) |

### 映像・音声

- 番組: 1920x1080 / 30 fps / H.264 High or Main (YouTube 推奨) / CBR 6〜10 Mbps (設定、既定 8) / キーフレーム 2 秒 / B フレーム 2 (符号化器が対応する場合)。
  既存の `VideoEncoder` 抽象に profile と B フレームの設定を足す (presenter 側は既定を変えない)。
- 返送: 640x360 / 30 fps / 1.2 Mbps / Constrained Baseline (relay の検証規則どおり)。番組面の縮小は Core の `nv12_scaler` を再利用。番組と返送で符号化は 2 回 (再符号化を必要箇所に限定する 2026-10-04 方針どおり)。
- 音声: 各入力の Opus 48 kHz を復号 → ミキサ (全画面はその入力、4 分割は全入力のミックス、待機 / 終了は無音) → AAC-LC 128 kbps ステレオ (YouTube) と Opus (返送)。
- 入力が落ちたら最後のフレームを保持せず「入力未接続」板を出す (Cocoiru の返送窓の「古いフレームを live と見せない」規則と同じ)。

### YouTube 送出

- `rtmps://a.rtmps.youtube.com:443/live2` に TLS で接続し、RTMP 握手 (C0/C1/C2、簡易握手でよい) → `connect` (app `live2`, tcUrl) → `createStream` → `publish(<key>, "live")`。
  FLV: `onMetaData`、AVC sequence header (SPS/PPS)、AAC sequence header、以後 video/audio タグ。chunk size 4096、ack window に従う。
- 送出の失敗は 1→30 秒の指数バックオフで再接続し、再接続後は sequence header から送り直す。キーはログに出さず、エラー文にも含めない。
- 代替として YouTube の HLS 入力 (HTTPS PUT) は採らない (遅延が増え、TS 多重化を別に書くため)。

### 受入条件 (run 2)

1. Core の単体テスト: レイアウト矩形 (各モード)、ミキサ (ミュート / クリップ)、FLV タグのバイト列 (既知ベクタと比較)、RTMP 握手と AMF0 の encode / decode (既知ベクタ)、chunk 分割と ack、再接続バックオフ、設定の境界値。
2. Windows でビルドが通る構成 (復号・AAC は MF 実装)。macOS 実装 (VideoToolbox / AudioToolbox) は Mac 拠点でのビルドが必要で、未実施はそのまま書く。
3. `excubitor.catalog.yaml` に `odeum-program` (runtime node, command `native/build/bin/odeum-program`, autostart false, health process) を追加。`scripts/site/setup.mjs` が program もビルドする。
4. `spec/feature/program-production.md` に構成・設定・制限を書き、`spec/domains/` に `program-production.domain.json` を追加して新規ファイルを全部所属させる。
5. 実機の復号・合成・YouTube 限定公開は人間の許可後に行う。PR には実施 / 未実施を明記する。

## Cocoiru の送信差し替え (run 3)

- Odeum 側で `native/presenter/core` + `transport` + `platform/windows` の取り込み・符号化・送信を `odeum_sender` 静的ライブラリとして切り出し (run 2 で同時に行う)、Cocoiru の `apps/tela-resident` が `find_package(Odeum CONFIG)` で link する。
- `sharing/` の FFmpeg 引数組み立て・子プロセス管理 (`media_arguments`、`process`) を削除し、`srt_url` を `odeum://` 形式の送信リンク (relay URL + presenter チケット、slot 付き) に置き換える。DPAPI 保護はそのまま使う。
- 送信対象の選択 (ウインドウ / ディスプレイ) と「対象が閉じたら止める・デスクトップへ fallback しない」規則は既存どおり。取り込み除外 (SetWindowDisplayAffinity) も既存どおり。
- 返送プレビュー: viewer チケットで relay に接続し、program スロットを受けて MF で復号 → 既存のプレビュー窓に BGRA で描く。「待ち / 停止」表示と古いフレームを出さない規則は既存どおり。
- `spec/SCREEN-SHARING.md` を書き換える (FFmpeg / SRT / OBS の記述を外す)。

### 受入条件 (run 3)

1. 単体テスト: 送信リンクの解析 (slot 必須)、プレビューの状態遷移 (待ち / live / 停止)、設定の保存と DPAPI 往復 (既存テストの更新)。
2. `grep -ri "ffmpeg\|srt://\|mpegts" apps/tela-resident/src` が 0 件。
3. Windows でビルドが通る構成。実機の送信・返送確認は人間の許可後。

## 実装の順序と委託

run 1 → run 2 → run 3 の順に 1 本ずつ (run 2 は run 1 の relay 仕様に、run 3 は run 2 の `odeum_sender` に依存する)。
各 run は Odeum (run 3 は Cocoiru) のローカル main から切った専用 worktree で、Revisor local PR → Test OK → マージ → main 同期まで 1 loop。
委託先は Astra (neco の当初方針)。Codex の sandbox が setup refresh で落ちる間は Opus 5.5 へ切り替える。
タスク定義: `spec/tasks/2026-10-09-odeum-relay-slots.md`、`spec/tasks/2026-10-09-odeum-program.md`、`spec/tasks/2026-10-09-cocoiru-sender.md`。

## 検証の扱い

- Windows 上で build・単体テストまで。macOS 向けコード (復号・AAC・Pictor Metal) は Mac 拠点でのビルドが必要で、実行は人間の許可範囲に従う。
- 実機の 4 入力・合成・YouTube 限定公開・返送の確認は人間の許可後に行う。OBS 系統の削除はその確認の後。

## 対象外

- カメラ / HDMI 取り込み、フィルタ、録画、任意レイアウト編集などの OBS 汎用機能。
- WebRTC スタック (ICE / DTLS / SRTP) の自作。
- 既存 .NET アプリ (`src/Spectator.*`)。
