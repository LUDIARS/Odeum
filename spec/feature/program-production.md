# Odeum 番組制作プロセス (odeum-program)

Actio 参照: `actio:fc09da45-a7fb-4a61-bb7f-a003a49f991c`
設計の正本: [番組配信の自作 設計](../plan/2026-10-09-odeum-self-made-program-pipeline.md) の「odeum-program (run 2)」「全体構成」「責務の境界」節。
タスク定義: [odeum-program](../tasks/2026-10-09-odeum-program.md)。
中継との約束: [中継サーバ](live-relay.md) の「入力スロットと producer」。送信・符号化部品: [発表者アプリ](live-presenter.md)。

OBS + MediaMTX を使わずに、relay の入力スロット input1〜4 を受けて切替・合成・リアクション合成を行い、
YouTube Live へ FLV/RTMPS で送出し、番組を relay の program スロットへ返送する親機プロセス。

## 構成と責務

| 層 | 置き場所 | 依存 | 責務 |
|---|---|---|---|
| Core | `native/program/core/` | `odeum_protocol`、`odeum_presenter_core` (OS API なし) | 番組状態 (`ProgramState`)、レイアウト矩形 (`layout`)、音声ミキサ (`AudioMixer`)、AMF0、FLV タグ (`flv`) と送出列 (`FlvFeed`)、RTMP 握手・chunk・ack window・publish 手順 (`RtmpPublisher`)、YouTube 再接続判断 (`PublishSupervisor`)、設定 (`ProgramSettings`)、producer の mid / signalling 振り分け (`producer_routing`)、復号・AAC・鍵保管の抽象 (`media_codecs.hpp`) |
| 合成 | `native/program/render/` | Tela (Pictor の TrueType ラスタライザ) | NV12 4 本の矩形配置と「入力未接続」板・待機 / 終了の板 (`ProgramRenderer`)、リアクション層 (presenter の `overlay_document` を 1.5 倍で右下に描く)、返送用の縮小 |
| 通信 | `native/program/transport/` | libdatachannel、libopus、OpenSSL | 受信 PeerConnection (`InputReceiver`、relay の offer に recvonly で answer)、Opus 復号、TLS 接続 (`TlsConnection`)、YouTube 送出スレッド (`YouTubePublisher`)。relay との WebSocket (`RelayLink`) と返送の sendonly PeerConnection (`MediaSender`) は `odeum_sender` をそのまま使う |
| アプリ | `native/program/app/` | Tela Core | 入力ごとの復号スレッド (`InputPipeline`)、番組クロック (`ProgramEngine`)、UI スレッドの制御部 (`ProgramController`)、操作パネル (`program_panel`)、表示文言、設定ファイル |
| OS | `native/program/platform/{windows,macos}/` | Media Foundation / VideoToolbox、AudioToolbox、DPAPI / Keychain | H.264 復号 (NV12)、AAC-LC 符号化、ストリームキーの保管、イベントループ |

スレッド: UI (Tela パネル・リアクション層の描画・relay の signalling)、入力ごとの復号スレッド ×4、
番組クロック (合成・符号化・音声ミックス)、YouTube 送出スレッド、libdatachannel のネットワークスレッド。
UI 以外からの仕事は `EventQueue` で UI スレッドへ渡す。

## 起動方法

```
odeum-program [odeum://produce?relay=<wss URL>&ticket=<JWS>] [--font F.ttf] [--help]
```

- 番組リンクは `odeum://produce?relay=...&ticket=...` (presenter の `odeum://present` と同じ検証。action だけが違う)。
  チケットの role は `producer` でなければならない (welcome の role を確認し、違えば切断)。
- 操作パネルの「番組リンクを貼り付け」(クリップボード) でも受け付ける。
- relay との再接続は presenter と同じ `ConnectionMonitor` (0.5 秒から倍々、最大 30 秒、チケット期限で停止)。

## 入力と番組

- welcome で `reaction.ready` を送り (公開 telop / submission を番組に出せる)、受信 PeerConnection を作る。
  relay が接続中の入力スロットを offer し、program は recvonly で answer する。`sdp` の offer と relay が割り当てた
  mid (`input<n>-<k>`) の candidate は受信側、answer とそれ以外の candidate は返送側に振り分ける。
- `track.closed` で該当 mid の受信を止める。presence の `slots.inputN` が false になった入力と、track が閉じた入力は
  復号状態・最後の映像・音声キューを捨てる。**入力が落ちたら最後のフレームを保持せず「入力 N 未接続」板を出す**。
  接続中でも 1 秒以上新しい映像が無い入力も板にする。
- 映像は入力ごとのスレッドで復号 (待ち行列 8 AU を超えたら捨てて PLI)。track が開いたとき・復号に失敗したときも PLI を送る。
- 番組: 1920x1080 / 30 fps。モードは 全画面 1〜4 / 4 分割 (960x540 ×4) / 待機 (「まもなく始まります」) / 終了
  (「ご視聴ありがとうございました」)。リアクション層 (グッドの湧き上がり、スタンプ合計、公開 telop と公開 submission、
  投票集計、視聴者数) を右下に重ねる。
- 音声: 各入力の Opus (48 kHz ステレオ) を 20 ms 単位でミキサへ。全画面はその入力、4 分割は全入力、待機 / 終了は無音。
  スロット別ミュートと番組音量 (0〜200 %) を掛け、ピークが 1 を超えるフレームは全体を縮めてクリップを防ぐ。

## 符号化と送出

| 出力 | 映像 | 音声 |
|---|---|---|
| YouTube | 1920x1080 / 30 fps / H.264 High (設定で Main) / CBR 6〜10 Mbps (既定 8) / キーフレーム 2 秒 / B フレーム 2 (符号化器が対応する場合) | AAC-LC 128 kbps 48 kHz ステレオ |
| 返送 (relay の program スロット) | 640x360 / 30 fps / 1.2 Mbps / Constrained Baseline | Opus 64 kbps |

- H.264 は `odeum_sender` の符号化器 (Media Foundation / VideoToolbox) を 2 本使う。`VideoEncoder::options()` で
  profile と B フレーム数を指定する (presenter の既定は Constrained Baseline・B フレームなしのまま)。
  合成済みの NV12 は `VideoFrame::nv12` で渡す。
- YouTube: `rtmps://a.rtmps.youtube.com:443/live2` に TLS (OpenSSL 3、証明書とホスト名を検証。Windows は
  `org.openssl.winstore://` も読む) で接続し、RTMP 簡易握手 → Set Chunk Size 4096 → `connect(app=live2, tcUrl)` →
  `releaseStream` / `FCPublish` / `createStream` → `publish(<key>, "live")` → `NetStream.Publish.Start`。
  Window Ack Size に従って Acknowledgement を返し、Set Peer Bandwidth と ping に答える。
- FLV: 送出開始ごとに時刻 0 から、`@setDataFrame onMetaData`、AVC sequence header (AVCDecoderConfigurationRecord)、
  AAC sequence header (AudioSpecificConfig)、以後キーフレームから映像・音声タグ。B フレームの composition time を付ける。
- 送出の失敗は 1 秒から倍々で最大 30 秒の間隔で再接続し、再接続後は metadata と sequence header から送り直す。
  送出が 2 秒分以上遅れたら待ち行列を捨てて次のキーフレームから再開する (ドロップ数を表示)。
- 返送は操作パネルで開始 / 停止する。relay へ sendonly offer を送り program スロットの送信元になる。

## ストリームキー

- macOS は login Keychain (generic password、service `com.ludiars.odeum.program`、account `youtube-stream-key`)。
  Windows は DPAPI (現在のユーザー) で暗号化して `%APPDATA%\Odeum\program-stream-key.bin`。平文ファイルは作らない。
- 操作パネルの「キーを貼り付け」でクリップボードから受け、`A-Z a-z 0-9 - _` 1〜128 文字だけを受け付ける。
  キーは表示しない。ログ・エラー文・PR・テンプレートに出さない (RTMP の拒否はサーバの説明文ではなく固定コードで扱う)。

## 操作パネル

入力 1〜4 のサムネイル (160x90、2 回/秒) と接続状態、全画面 1〜4 / 4 分割 / 待機 / 終了、スロット別ミュート、
番組音量 ±10 %、YouTube 送出の開始 / 停止、キーの貼り付け / 削除、返送の開始 / 停止、番組リンクの貼り付け、終了。
状態として relay 接続、視聴者数、YouTube の状態 (送出中 / 再接続までの秒数 / 再接続回数)、送出ビットレート・送信量・
ドロップ数、合成フレーム数と遅れを出す。Windows は `tela::WindowsView`、macOS は `tela::MacOSDesktopOverlay`。

## 設定

設定ファイル (JSON、無ければ既定値、壊れていれば上書きせず起動失敗):
macOS `~/Library/Application Support/Odeum/program.json`、Windows `%APPDATA%\Odeum\program.json`。

| キー | 既定 | 範囲 |
|---|---|---|
| `video.bitrate_kbps` | 8000 | 6000〜10000 |
| `video.keyframe_interval_s` | 2 | 1〜4 |
| `video.profile` | `high` | `main` / `high` |
| `video.b_frames` | 2 | 0〜2 |
| `audio.bitrate_kbps` | 128 | 64〜320 (Media Foundation は 96/128/160/192 の最も近い値) |
| `return.bitrate_kbps` / `return.audio_kbps` | 1200 / 64 | 300〜2500 / 16〜256 |
| `youtube.url` | `rtmps://a.rtmps.youtube.com:443/live2` | `rtmps://host[:port]/app` のみ (資格情報・query・平文 rtmp は拒否) |
| `font` | (候補から探索) | TrueType (.ttf) |
| `volume` / `muted` | 1.0 / 全 false | 0〜2 / 4 要素の bool |

## 合成の実装と Pictor のオフスクリーン経路

Pictor には今のところ 2D 合成用のオフスクリーン render target と GPU 読み戻しが無い (Vulkan の
`enable_swapchain_transfer_src` のみ)。このため `ProgramRenderer` をインタフェースとし、次の 2 実装に分けた。

- `CpuProgramRenderer` (既定): NV12 の矩形配置・縮小・板をメモリ上で行い、Tela + Pictor の TrueType ラスタライザ
  (`tela::PictorSurface`) で描いた BGRA 層を NV12 に重ねる。今の Pictor で動く。
- `PictorOffscreenRenderer` (`-DODEUM_PROGRAM_PICTOR_OFFSCREEN=ON` のときだけビルド): Pictor 側に別 PR で足す
  `pictor/surface/offscreen_target.h` を前提にした GPU 経路。必要な API は
  `OffscreenTarget::create({width,height})` (headless デバイス、無ければ nullptr)、`upload_nv12(slot, w, h, y, uv)`、
  `begin_frame(color)`、`draw_texture_fit(texture, rect)`、`fill(rect, color)`、`draw_bgra_premultiplied(pixels, w, h, rect)`、
  `end_frame()`、`read_back_nv12(out, size)` (BT.709 limited range)。Pictor の PR がマージされるまで既定は CPU 経路。

## ビルド

```sh
# Windows (Tela は -DTELA_BUILD_WINDOWS=ON 相当で同時にビルドされる)
cmake -S native -B native/build -DODEUM_BUILD_PROGRAM=ON \
  -DODEUM_TELA_SOURCE_DIR=E:/Document/Ars/Tela \
  -DTELA_PICTOR_INCLUDE_DIR=E:/Document/Ars/Pictor/include \
  -DTELA_PICTOR_LIBRARY=E:/Document/Ars/Pictor/build/Release/pictor.lib
cmake --build native/build --config Release --target odeum-program
```

- `ODEUM_BUILD_PROGRAM_CORE=ON` は Tela 無しで Core とそのテスト (`odeum.program.{layout,flv,rtmp,settings}`) だけを作る。
- `scripts/site/setup.mjs` は relay と一緒に program もビルドする (Tela / Pictor は兄弟 checkout、環境変数で上書き可)。
- Excubitor catalog: `odeum-program` (runtime node、`native/build/bin/odeum-program`、autostart false、health process)。
- 出力は `native/build/bin/odeum-program[.exe]`。macOS の VideoToolbox / AudioToolbox / Keychain 実装は Mac 拠点でのビルドが必要。

## 制約と未対応

- カメラ / HDMI 取り込み、録画、任意レイアウト編集はしない。`broadcast/` (OBS + MediaMTX) は実機確認後に別 PR で消す。
- YouTube の HLS 入力は使わない。RTMP は簡易握手のみ (digest 握手なし)。
- 合成は CPU 経路が既定 (上記)。リアクション層は 1080p に対して presenter の重ね表示を 1.5 倍で描く。
- 個人データ: 番組面に出す本文は送信者が公開にした telop / submission だけ (relay が producer に非公開分を送らない)。
  表示名・本文・チケット・ストリームキーはログに出さない (odeum-program はログを書かない)。
