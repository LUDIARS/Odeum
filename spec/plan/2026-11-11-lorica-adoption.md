# Lorica の全面適用と入力経路の上限チェック: 棚卸し (job cpp-lorica-20261111)

ブランチ `cpp/odeum-node-port`、着手時の先頭 `9070773`。

## 1. ビルド単位 (着手時の Lorica 適用)

| ビルド単位 | 種類 | 作る条件 / 環境 | Lorica::boundary | lorica_harden_target |
|---|---|---|---|---|
| odeum_protocol | static lib | 常に | なし | なし |
| odeum_relay_state / odeum_relay_transport | static lib | ODEUM_BUILD_RELAY (既定 ON) | なし | なし |
| odeum-relay | 実行 (常駐 port 4400) | 同上 | なし | なし |
| odeum_{protocol,ticket,state,media,hub,guest,overlay,slot,producer,slot_media}_tests | テスト 10 本 | relay | なし | なし |
| odeum_presenter_core | static lib | PRESENTER_CORE / SENDER / PRESENTER / PROGRAM* | なし | なし |
| odeum_presenter_{url,backoff,overlay,poll,keyframe,media}_tests | テスト 6 本 | presenter core | なし | なし |
| odeum_sender / odeum_presenter_overlay / odeum_presenter_app / odeum-presenter | lib / 実行 | Windows・macOS のみ (Linux は FATAL_ERROR)。overlay 以降は Tela 必須 | なし | なし |
| odeum_program_core | static lib | PROGRAM_CORE / PROGRAM | なし | なし |
| odeum_program_{layout,flv,rtmp,settings,archive}_tests | テスト 5 本 | program core | なし | なし |
| odeum_program_render / _transport / _app / odeum-program | lib / 実行 | Windows・macOS のみ、Tela 必須 | なし | なし |
| odeum_runner_core / _adapters / _app / _webrtc, odeum-runner, odeum-unityctl | lib / 実行 | ODEUM_BUILD_RUNNER (macOS 以外既定 ON) | あり (core PUBLIC) | baseline |
| odeum_runner_{config,messages,auth,process,playlog,unity,farm,media,http,boundary,webrtc}_tests | テスト 11 本 | runner | あり | baseline |
| 第三者: datachannel (+ juice, srtp2, usrsctp) | static lib (ソースからビルド) | relay / runner webrtc / presenter / program | — | なし |
| 第三者: opus | static lib (ソースからビルド) | sender / program (Windows・macOS) | — | なし |

Lorica の固定は `cab4f12086e30484f249d1b4aa479bf7cdec13a9` で、`ODEUM_BUILD_RUNNER` のときだけ取り込み。
→ 共通の `5e8281b0e60fa16f814e16eaa620715937e8618d` にそろえ、常に取り込む。

Linux (クラウド) で作れるもの: protocol・relay・presenter core・program core・runner とそのテスト。
presenter / sender / program のアプリ部分は Windows・macOS 専用 (CMake が Linux を拒否) で、Tela も要る → ビルドできない。

## 2. 外から入る入力の経路

| 経路 | 場所 | 既存の上限 |
|---|---|---|
| relay HTTP 要求 (target・ヘッダ) | relay/connection.cpp, http_query.cpp | Beast header 16KiB、本文 0 |
| relay WebSocket テキスト (JSON) | relay/connection.cpp → protocol/message.cpp | 16KiB/メッセージ、120 通/秒、送信キュー 256KiB |
| チケット (JWS, Ed25519) | protocol/ticket.cpp | 8192 文字 |
| relay 設定 (env の ICE JSON、公開鍵ファイル) | relay/config.cpp | ICE 16 件。鍵ファイルは上限なし |
| WebRTC の RTP/RTCP (libdatachannel 経由) | relay/media.cpp, media_sdp.cpp | キュー 512 パケット |
| SDP / ICE candidate | relay/media.cpp, media_sdp.cpp | メッセージ 16KiB、candidate 128 件 |
| program: RTMPS の受信 (YouTube の応答) | program/core/rtmp_chunk.cpp, rtmp_publisher.cpp, amf0.cpp | メッセージ 16MiB。チャンクストリーム数・保留バイト合計は上限なし、AMF0 要素数の上限なし |
| program: 録画ファイル (FLV + archive.json) | program/core/archive_reader.cpp | タグ 8MiB、manifest 16KiB |
| program: 設定ファイル | program/app/program_settings_file.cpp | なし |
| program: relay からの WebRTC フレーム | program/transport/input_receiver.cpp | なし |
| presenter: odeum:// 起動 URL・チケットの exp | presenter/core/launch_url.cpp | URL 8192 |
| presenter: 設定ファイル | presenter/app/settings_file.cpp | 要確認 |
| runner: HTTP / WS / 子プロセス出力 / ファイル / ブリッジ応答 | native/runner/** | 前回 job で Lorica の予算済み |

## 3. 依存

| 依存 | 取得 | 許可リスト |
|---|---|---|
| Boost 1.87.0 (ヘッダのみ) | FetchContent zip + SHA256 | 可 |
| nlohmann_json 3.11.3 | FetchContent + SHA256 | 可 |
| OpenSSL 3 | システム | 可 |
| libdatachannel d9391849 (+ libjuice, libsrtp, usrsctp, plog) | FetchContent git | 10 章で個別に許可 |
| opus (xiph git ddbe4838) | FetchContent git | 10 章で個別に許可 |
| Lorica | FetchContent / LORICA_SOURCE_DIR | 必須 |
| Tela (+ Pictor) | find_package / ODEUM_TELA_SOURCE_DIR | **許可リスト外** (既存。外さない → 人間の判断) |
| Windows / macOS の OS API (Media Foundation, D3D11, WASAPI, ScreenCaptureKit, VideoToolbox …) | OS | 可 |

## 4. 前回の報告の残り

`spec/plan/2026-10-30-runner-cpp-port.md` に未了の移植項目はなし (「移さない」とした項目と本社テストのみ)。
sanitizer のビルド設定と fuzz の入口は repo 全体で未整備 (runner も含む)。

## 5. 実施結果 (2 代目の担当で仕上げ)

### 5.1 Lorica の適用

- 固定: `5e8281b0e60fa16f814e16eaa620715937e8618d`。常に取り込む (`LORICA_SOURCE_DIR` でローカルの checkout も可)。
- `native/cmake/OdeumLorica.cmake` の `odeum_harden(<t> [EXTENDED] [EXPORTED])` を自前の全ターゲットで呼ぶ
  (`Lorica::boundary` + `lorica_harden_target` + GCC/Clang では `lorica_deprecate_unsafe_apis`)。
  `odeum_lorica_require_coverage()` が、自前のディレクトリに未適用のコンパイル単位が残ると configure を FATAL_ERROR にする。
- EXTENDED: odeum-relay (+ odeum_protocol, odeum_relay_state, odeum_relay_transport) と odeum-runner (+ runner の lib)。
  ツールチェーンが EXTENDED を満たさなければ WARNING を出して baseline (`ODEUM_LORICA_EXTENDED=OFF` で明示的に切れる)。
- 第三者 (datachannel, juice, srtp2, usrsctp, opus): 自前の CMake から `lorica_harden_target` (baseline) を差し込む。
  ソースと CMake は変更しない。boundary と unsafe API の拒否は入れない (C のソースを含むので Lorica が拒否する)。
- macOS: Lorica の hardening は macOS を拒否するので、`-DODEUM_LORICA_MACOS_UNHARDENED=ON` を明示したときだけ
  hardening を飛ばして configure を通す (飛ばしたターゲットごとに WARNING)。boundary は入る。
- MSVC: `lorica_deprecate_unsafe_apis` は `/we4996` で CRT の `getenv` なども拒否するので入れていない (本社で確認)。
- ビルドの結果を見るテスト `odeum.lorica_applied.*` (`native/tests/lorica_applied_tests.cmake`、configure の網羅確認は残す):
  `.baseline` / `.extended` (odeum_harden を通した翻訳単位が、強制 include・SSP・CET・PIE・`_GLIBCXX_ASSERTIONS`・FORTIFY を
  中から確かめ、boundary の `checked_add` / `input_budget` を実行)、`.compile_commands` (`cmake -P` で compile_commands.json の
  全エントリ。自前は全部、第三者は baseline のみを見る。打ち消すフラグは最後の出現で判定)、`.binary.<exe>` (python3 があるとき
  verify_binary.py)。macOS の unhardened・Visual Studio・EXTENDED 不可の環境では理由を出して skip。

### 5.2 入力経路の上限

| 経路 | 場所 | 上限 |
|---|---|---|
| relay WebSocket メッセージ | protocol/message.cpp (`parse_json_within`) | 16KiB、値 1024、深さ 8 |
| チケット (ヘッダ・クレーム) | protocol/ticket.cpp | 8192 文字、1 パート値 256・深さ 4 |
| relay HTTP クエリ | relay/http_query.cpp | 16 件 |
| relay 公開鍵ファイル / ICE の env JSON | relay/config.cpp | 1MiB、値 16384、深さ 16 |
| RTCP の PLI/FIR 判定 | protocol/rtcp.cpp (relay と presenter で共有) | 65535 バイト |
| relay: Cloudflare TURN の応答 (main の bb2775e) | relay/cloudflare_turn.cpp | HTTP 64KiB (既存)、JSON 値 2048・深さ 6、16 件 × 16 URL |
| relay の RTP 転送 | relay/media.cpp | 65535 バイト超は捨てる |
| presenter 起動 URL | presenter/core/launch_url.cpp | 8192 文字 (既存)、クエリ 16 件 |
| presenter ticket_expiry | presenter/core/launch_url.cpp | 8192 文字、値 256・深さ 4 |
| presenter / program 設定ファイル | core の `settings_from_text` + app の `read_file_within` | 64KiB、値 16384、深さ 16 |
| program RTMP 受信 | program/core/rtmp_chunk.cpp | チャンクストリーム 64、保留バイト 32MiB |
| program AMF0 | program/core/amf0.cpp | 1 ペイロード 4096 値、深さ 16 (既存) |
| program archive.json | program/core/archive_reader.cpp | 16KiB (読みながら)、値 64、深さ 2 |
| program 入力フレーム | program/core/input_frame_limits.hpp → transport/input_receiver.cpp | 映像 8MiB、音声 65535 |
| lesson WebSocket メッセージ (main の odeum-lesson) | lesson/input_limits.cpp → connection.cpp | 16KiB (既存)、値 1024、深さ 8 |
| lesson 道具の POST 本文 | lesson/input_limits.cpp → connection.cpp | 4KiB (既存)、値 64、深さ 4 |
| lesson プロットのファイル | lesson/catalog.cpp (`read_file_within`) | 1MiB (読みながら)、値 16384、深さ 16 |

### 5.3 sanitizer と fuzz

- `ODEUM_SANITIZE` (`address,undefined` / `thread`): 依存を含む全ターゲットに付く。
- `ODEUM_FUZZ` (既定 OFF、Clang のみ): `native/fuzz/` の 11 入口 (merge 後に cloudflare_turn を追加)を libFuzzer でビルド。
  seed を 1 回ずつ流す `odeum.fuzz.*` は GCC のビルドでも ctest に入る。
- TSan は `native/tests/tsan.supp` を使う (libdatachannel 内部の callback の競合だけ)。Clang の UBSan では libjuice に
  `-fno-sanitize=function` (同梱の picohash が関数ポインタの型を合わせずに呼ぶため)。
- 結果 (2026-11-11、Linux): GCC Release / ASan+UBSan / TSan、Clang ODEUM_FUZZ で ctest 各 46/46。fuzz 10 入口を各 30 秒、クラッシュなし。
