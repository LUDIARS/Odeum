# Odeum 発表者アプリ (odeum-presenter)

Actio 参照: `actio:a33eca3e-d732-4b8c-9249-e39d08b44d57`
設計の正本: [ライブ配信設計](../plan/2026-10-04-odeum-live-presentation-design.md) の「発表者アプリ」節。
中継との約束: [中継サーバ](live-relay.md)。

発表者の画面を取り込み H.264 で中継 (`odeum-relay`) へ送り、視聴者のグッド・スタンプ・コメント・
投票の集計・視聴者数を Tela/Pictor の重ね表示に出す。投票の作成・開始・終了もここで行う。

## 構成と責務

| 層 | 置き場所 | 依存 | 責務 |
|---|---|---|---|
| Core | `native/presenter/core/` | `odeum_protocol` のみ | 起動リンクの解析、再接続の判断、重ね表示の状態、投票の検証、キーフレーム要求の伝搬、H.264 / NV12 / PCM の整形、設定と起動引数。OS API を持たず CTest で検証する |
| 通信 | `native/presenter/transport/` | libdatachannel、libopus | 中継への WebSocket、PeerConnection (sendonly の H.264 と任意の Opus)、Opus 符号化 |
| アプリ | `native/presenter/app/` | Tela Core | UI スレッドの制御部、重ね表示と操作パネルの宣言、表示文言、設定ファイル |
| OS | `native/presenter/platform/{windows,macos}/` | OS API、Tela の OS アダプタ | 取り込み、符号化、音声、クリップボード、URL スキーム、イベントループ |

取り込み (`CaptureSource`)・符号化 (`VideoEncoder`)・音声 (`AudioSource` / `AudioEncoder`) は Core の抽象で、
OS 側が実装する。ネットワーク・取り込み・OS コールバックの各スレッドからの仕事は `EventQueue` で UI
スレッドへ渡し、状態はすべて UI スレッドが持つ。

## 起動方法

- GLab の「発表を始める」が出す `odeum://present?relay=<wss URL>&ticket=<JWS>` で起動する
  (値は `encodeURIComponent` 済み)。`relay` は中継の WebSocket ベース URL で、接続先は
  `<relay>/v1/ws?ticket=<ticket>`。
- 引数に同じリンクを直接渡しても、操作パネルの「発表リンクを貼り付け」(クリップボード) でもよい。
- 受け付けるのは `odeum://present` だけ。`wss://` 以外の relay、資格情報・クエリ・空白を含む relay、
  ticket の欠落・JWS compact 以外・base64url 以外は拒否してパネルに理由を出す。
- macOS: アプリバンドルの `Info.plist` が `odeum` スキームを宣言し、`NSAppleEventManager` の GetURL で受ける。
  起動中のアプリには LaunchServices が同じ経路で後続のリンクを渡す。
- Windows: `odeum-presenter --register-url-scheme` で `HKCU\Software\Classes\odeum` に現在のユーザー分だけ
  登録する (管理者権限不要、`--unregister-url-scheme` で削除)。リンクごとに新しいプロセスが起動するので、
  起動中の発表者アプリがあれば `WM_COPYDATA` でリンクを渡して新しいプロセスは終了する。

```
odeum-presenter [odeum://present?...] [--font F.ttf] [--width W --height H] [--fps N]
                [--max-bitrate-kbps N] [--audio | --no-audio] [--overlay-corner C]
                [--register-url-scheme | --unregister-url-scheme | --help]
```

## 接続と再接続

- `welcome` の `role` が `presenter` でなければ切断して「発表者用ではない」と出す。
- `welcome` を受けたら PeerConnection を作り offer を送る。中継の answer と candidate を受けて接続する。
- 切断時は 0.5 秒から倍々で最大 30 秒の間隔で再接続する。`welcome` まで届けば間隔は初期値に戻る。
- チケットは発行から 5 分 (`exp`)。次の再接続が `exp` 以降になるなら再接続をやめ、「GLab で発表リンクを
  再発行してください」と出す。中継は `jti` を `exp` まで記憶するため、中継の再起動以外では同じチケットでの
  再接続は拒否されうる。`welcome` に届かない失敗が 3 回続いたら期限前でも再発行を促す。
- 新しいリンクを受けたら接続・映像経路・重ね表示の状態をすべて作り直す。
- 中継は発表者の切断で部屋を閉じるので、再接続後のグッド累計・投票は 0 から始まる。

## 映像と音声

- 既定 1920x1080 / 30 fps / 上限 6 Mbps / キーフレーム最大 4 秒間隔。設定範囲は 320x180〜3840x2160
  (偶数)、1〜60 fps、300〜20000 kbps。
- SDP は `profile-level-id=42e028;packetization-mode=1` (Constrained Baseline Level 4.0。libdatachannel 既定の
  Level 3.1 は 720p 止まりのため)。映像・音声とも 1 SSRC、sendonly。中継の検証 (42e0/42c0 前方一致) を満たす。
- 中継から届く RTCP の PLI (PT 206 FMT 1) と FIR (FMT 4) で、次に符号化するフレームを IDR にする。
  映像経路の接続直後と配信開始直後も IDR から始める。IDR には必ず SPS/PPS を付ける。
- 取り込みが目標フレームレートより速いときは間引く。
- 音声は既定 OFF。ON (`--audio`) でシステム音声を 48 kHz ステレオ 20 ms の Opus (既定 64 kbps、in-band FEC)
  で送る。取り込みのレートとチャンネル数は Core の `PcmFramer` で揃える。
- macOS: ScreenCaptureKit (`SCStream`、NV12、ストリームサイズへ縦横比を保って縮小) + VideoToolbox
  (`kVTProfileLevel_H264_ConstrainedBaseline_AutoLevel`、RealTime、フレーム並べ替えなし、平均は上限の 70 %、
  `DataRateLimits` で上限)。音声は別の `SCStream` (`excludesCurrentProcessAudio`)。
- Windows: Windows Graphics Capture (WRL / ABI) + Media Foundation の H.264 エンコーダ MFT (同期、
  PeakConstrainedVBR、Constrained Baseline、B フレームなし、低遅延)。D3D11 テクスチャを CPU で NV12 に
  縮小 (縦横比保持、黒帯) してから符号化する。音声は WASAPI ループバック。

## 重ね表示 (Tela/Pictor)

- macOS は `tela::MacOSDesktopOverlay`、Windows は `tela::WindowsDesktopOverlay`。`exclude_from_capture = true`
  で配信に写さない (OS が除外できなければ起動時に失敗し、写る状態では表示しない)。macOS の画面取り込みは
  `SCContentFilter` で自アプリのウインドウも除外する。
- 表示: 視聴者数、グッドの湧き上がり (`reaction.burst` ごとの泡。直近 3 秒の押下レートが高いほど数・大きさ・
  暖色が増す) と累計、スタンプ合計、押した人の名前つきスタンプ (新しい順に 4 件、4 秒で消える)、投票の
  質問と棒グラフと回答数 (締切後 30 秒残す)、コメントの流れ (最新 8 件を保持し 15 秒で消える。表示 ON/OFF 可)。
- 位置: 操作パネルで四隅から選ぶか、上端のつまみ「:::」で移動する。移動先は設定ファイルに保存し次回復元する。
- つまみ以外は入力を下のアプリへ通す。

## 操作パネル

- 発表リンクの貼り付け、配信開始/停止、取り込み対象 (画面・ウインドウ) の選択と一覧更新、画面収録の許可、
  投票の作成 (質問と選択肢はクリップボードから貼り付け。選択肢は 1 行 1 件、2〜6 件、各 60 文字まで。
  複数選択の可否)、投票の開始/終了、コメント表示 ON/OFF、重ね表示の四隅、終了。
- 接続状態 (中継・映像経路・再接続までの秒数・期限切れ)、視聴者数、配信状態、直近のお知らせを出す。
- 配信停止は取り込みと符号化だけを止め、中継との接続は保つ (リアクションは届き続ける)。
- Windows は `tela::WindowsView` の通常ウインドウ (`WDA_EXCLUDEFROMCAPTURE` で配信から除外)。Tela に
  AppKit の通常ウインドウ host が無いため、macOS では 2 枚目の `MacOSDesktopOverlay` (つまみで移動、
  配信から除外) として出す。Tela は文字入力を提供しないので、文字はすべてクリップボードから受ける。

## 権限

- macOS: 「画面収録」の許可が必要。未許可なら配信開始時に `CGRequestScreenCaptureAccess` を呼び、
  システム設定の画面収録ペインを開いて案内を出す (許可後はアプリの再起動が必要)。対応は macOS 12.3 以降。
- Windows: Windows 10 2004 以降 (`WDA_EXCLUDEFROMCAPTURE` と Windows Graphics Capture)。URL スキームの登録は
  現在のユーザーのレジストリだけに書く。

## 設定

設定ファイル (JSON、無ければ既定値で起動、壊れていれば上書きせず起動失敗):

- macOS: `~/Library/Application Support/Odeum/presenter.json`
- Windows: `%APPDATA%\Odeum\presenter.json`

| キー | 既定 | 内容 |
|---|---|---|
| `stream.width` / `stream.height` | 1920 / 1080 | 配信サイズ |
| `stream.fps` | 30 | フレームレート |
| `stream.max_bitrate_kbps` | 6000 | ビットレート上限 |
| `stream.keyframe_interval_s` | 4 | キーフレーム最大間隔 |
| `stream.audio` / `stream.audio_bitrate_kbps` | false / 64 | Opus 音声 |
| `font` | (候補から探索) | TrueType (.ttf) のパス |
| `comments_visible` | true | コメント表示 |
| `overlay.corner` / `margin_x` / `margin_y` / `absolute` / `x` / `y` | bottom-right / 24 / 24 / false | 重ね表示の位置 |

起動引数は設定ファイルの値を上書きする (保存はしない)。重ね表示の位置とコメント表示はパネルで変えると保存する。

フォントは Pictor が .ttc を読めないため .ttf が必要。未指定時の候補: `<設定フォルダ>/presenter.ttf`、
macOS は `Arial Unicode.ttf`、Windows は `NotoSansJP-VF.ttf` など Windows 同梱の単体 .ttf。日本語の名前や
コメントを正しく出すには日本語グリフを持つ .ttf を `--font` か `font` で指定する。

## ビルド

既定 (`ODEUM_BUILD_PRESENTER=OFF`) では中継サーバだけをビルドし、Tela/Pictor を要求しない。
`ODEUM_BUILD_PRESENTER_CORE=ON` は Tela 無しで Core とそのテストだけを作る。

```sh
# Windows (Tela は -DTELA_BUILD_WINDOWS=ON 相当で同時にビルドされる)
cmake -S native -B native/build -DODEUM_BUILD_PRESENTER=ON \
  -DODEUM_TELA_SOURCE_DIR=E:/Document/Ars/Tela \
  -DTELA_PICTOR_INCLUDE_DIR=E:/Document/Ars/Pictor/include \
  -DTELA_PICTOR_LIBRARY=E:/Document/Ars/Pictor/build/Release/pictor.lib
cmake --build native/build --config Release --target odeum-presenter

# macOS (Tela は TELA_BUILD_MACOS=ON。Pictor は同じ CPU 向けにビルドしたものを明示する)
cmake -S native -B native/build -DODEUM_BUILD_PRESENTER=ON \
  -DODEUM_TELA_SOURCE_DIR=/path/to/Tela \
  -DTELA_PICTOR_INCLUDE_DIR=/path/to/Pictor/include \
  -DTELA_PICTOR_LIBRARY=/path/to/Pictor/build/libpictor.a
cmake --build native/build --config Release --target odeum-presenter   # odeum-presenter.app
```

- Tela/Pictor は LUDIARS のローカル checkout をパスで渡す (`ODEUM_TELA_SOURCE_DIR`)。インストール済みの Tela を
  使うときは `ODEUM_TELA_SOURCE_DIR` を空にして `find_package(Tela CONFIG)` に任せる。
  `tela::DesktopOverlayOptions` と `MacOSDesktopOverlay` を含む Tela (origin/main `1cc5c3b` 以降) が必要。
- libopus は v1.5.2 のコミット (`ddbe4838`) を FetchContent で固定取得する (libdatachannel と同じ方針)。
  json / libdatachannel / Boost は中継と共通。Tela・libopus 自身のテストは ctest に登録しない。
- ライセンス: libopus BSD-3-Clause (中継の依存は [中継サーバ](live-relay.md) を参照)。

## 制約と未対応

- 映像は 1 本 (simulcast なし)。中継が再符号化しないため、全視聴者が同じビットレートを受ける。
- Windows の符号化はソフトウェア MFT と CPU の縮小変換を使う。ハードウェアエンコーダは使わない。
- 取り込み対象のウインドウが閉じられたら配信を止めてパネルで知らせる。
- 個人データ: 表示名・コメント本文・チケットはログに出さない (発表者アプリはログを書かない)。
- 再接続でチケットを自動更新する仕組みは無い (GLab の操作が必要)。
