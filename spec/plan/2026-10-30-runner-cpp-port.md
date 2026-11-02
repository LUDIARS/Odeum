# 遠隔テストランナー (取り込んだ Custos) の C++ 移行: 棚卸し

job `cpp-node-20261030`。目的は (1) npm 依存と Node.js ランタイム同梱部品から来る脆弱性の受け口をなくすこと、
(2) 外部依存を増やさないこと。速度は目的ではない。`custos/` の写しは出所の記録として残し、編集も削除もしない。

新しい実行ファイルは `native/runner` の `odeum-runner` (常駐サーバ) と `odeum-unityctl` (CLI)。
操作画面 (`custos/public`) はブラウザ側の JS のまま移行せず、`odeum-runner` が配信する。

## 1. 依存の棚卸し

`custos/package-lock.json` の数え方: lockfile の `node_modules/*` 全 334 パッケージ。実行時に読み込まれる閉包は
`dependencies` だけで 99、`optionalDependencies` を足して 213 (自身を含む)。

| npm 依存 (版) | 種別 | 推移的 (自身を含む) | 移行後 | 理由 |
|---|---|---|---|---|
| `@hono/node-server` 1.19.17 | 実行 | 2 | 不要 | HTTP の受け口は Boost.Beast (既存 native と同じ)。`serveStatic` は使わず、配信物はビルド時に埋め込む。CVE-2026-118663 の受け口が消える |
| `hono` 4.13.7 | 実行 | 1 | 不要 | ルーティングは `native/runner/app/api_router.cpp` |
| `ws` 8.21.3 | 実行 | 1 | 不要 | WebSocket は Boost.Beast |
| `zod` 3.25.76 | 実行 | 1 | 不要 | 設定・入力の検証を C++ (nlohmann_json + 手書きの検証) に移植 |
| `pino` 9.14.0 / `pino-pretty` 11.3.0 | 実行 | 13 / 29 | 不要 | 1 行 1 JSON のロガーを自作 (標準ライブラリのみ) |
| `werift` 0.23.0 | 実行 | 59 | 置き換え | libdatachannel (既存 `native/relay` と同じ GIT_TAG `d9391849`)。新しい依存ではない |
| `@mediapipe/tasks-vision` 1.0.1 | 実行 (ブラウザ資材) | 1 | 残す (人間の判断) | ブラウザで動く視線推定の wasm。`scripts/setup-gaze.mjs` が `public/gaze-assets` へ写す。C++ は写された固定名のファイルを配信するだけ |
| `@nut-tree-fork/nut-js` 4.2.6 | 任意 | 128 | 置き換え | Win32 `SendInput` (OS API)。Linux (X11) は依存を足さず「ログのみ」(従来の nut-js 不在時と同じ)。follow-redirects 1.16.0 (centra ← jimp ← nut-js) の受け口が消える |
| `electron` 43.6.0 | 開発 | 13 | 不要 | デスクトップの殻は使わない。ブラウザで `http://localhost:4649/` を開く。CVE-2026-115570 の受け口が消える |
| `tsx` / `typescript` / `@types/*` / `cross-env` | 開発 | 29 / 1 / 5 / 7 | 不要 (C++ 実行時) | 互換性テストの golden 作成時だけ使う (下記) |
| `vitest` 3.2.7 | 開発 | 104 | 不要 (C++ 実行時) | テストは ctest。ブラウザ側 JS のテスト (`*-logic.test.ts` など 6 本) は Node のまま |
| Node.js ランタイム (同梱 Brotli・OpenSSL・undici など) | 実行 | — | 不要 | C++ の実行ファイルは Node を起動しない |

外部コマンド (依存ではなく実行時に引数配列で起動する既存の外部プログラム): `ffmpeg` (`CUSTOS_FFMPEG`)、`adb` (`CUSTOS_ADB`)、
Windows の `csc.exe` (ProcessLoopback.cs のビルド)。移行前と同じ。

`scripts/site/*.mjs`・`scripts/broadcast/*.mjs`・`broadcast/scripts/*.mjs` は `node:` の組み込みモジュールしか import しない
(npm 依存なし) ので対象外。Excubitor の起動契約 (`excubitor.bootstrap*.json`) も変えない。

## 2. 機能の棚卸し

「既存 native」は Odeum の `native/presenter` `native/relay` `native/program` で同じ働きをするものがあるか。

| custos モジュール | 働き | 既存 native | C++ の置き場 |
|---|---|---|---|
| `main.ts` `app.ts` | 2 ポート (7676 / 4649) で同じアプリ、静的配信、`/api/health`、非 loopback の無認証 bind 拒否 | なし (relay は別の経路) | `native/runner/app/main.cpp` `http_server.*` `api_router.*`、`core/auth_policy.*` |
| `config/apps-config.ts` | apps.json の検証・既定値・`ergoCustos` の正規化 | なし | `core/app_config.*` |
| `apps/registry.ts` `apps/runner.ts` | build / run / test の起動・行単位ログ・タイムアウト・kill (Windows は木ごと) | なし | `core/registry.*` `app/apps_runner.*` `adapters/process_*.cpp` |
| `auth/cernere-auth.ts` `auth/middleware.ts` | Cernere `/api/auth/verify` (5 分キャッシュ)・open / anonymous / stub | relay は Ed25519 チケット (別方式) | `core/auth_policy.*` `adapters/cernere_client.*` |
| `routes/apps-routes.ts` | 一覧・状態・build/run/test/kill、設定の秘匿 (redact) | なし | `app/api_router.cpp` `core/app_view.*` |
| `routes/settings-routes.ts` `runtime/stream-prefs.ts` | スクリーンショット配信の実行時上書き | なし | `core/stream_prefs.*` |
| `routes/control-guard.ts` `input/control-lease.ts` | 遠隔プレイの操作権 | relay の producer 役は別概念 | `core/control_lease.*` |
| `ws/handler.ts` `ws/*-message.ts` `audience/room.ts` `shared/types.ts` | `/ws` の購読・ログ配信・入力・感想・観戦 | relay の WS は別プロトコル | `app/ws_broker.*` `core/client_message.*` `core/audience.*` |
| `input/forwarder.ts` `input/android.ts` | 仮想キー・クリック・ポインタ (ブリッジ / adb / nut-js) | presenter は入力注入なし | `app/input_forwarder.*` `adapters/desktop_input_*.cpp` `adapters/adb.*` `core/keys.*` |
| `capture/ergo-custos-client.ts` | アプリ内ブリッジ `/screenshot` `/key` `/pointer` `/state` | なし | `adapters/bridge_client.*` |
| `capture/screenshot.ts` `scale.ts` `screenshot-stream.ts` | ffmpeg 1 枚取得・縮小・定期配信 | presenter は WGC (別用途、アプリ単位の静止画ではない) | `adapters/screenshot.*` `app/screenshot_streamer.*` `core/ffmpeg_args.*` |
| `playlog/*` `routes/playlog-routes.ts` | 行動ログ (JSONL)、player の HMAC、一覧・取得 | なし | `core/playlog_row.*` `app/playlog_recorder.*` `adapters/playlog_files.*` |
| `unity/*` `routes/unity-routes.ts` | Unity ブリッジのプロキシ (状態の正規化・操作権) | なし | `core/unity_contract.*` `adapters/bridge_client.*` `app/unity_proxy.*` |
| `cli/unityctl.ts` `unity/unityctl-*.ts` | `unityctl` CLI | なし | `native/runner/unityctl/main.cpp` `core/unityctl.*` → `odeum-unityctl` |
| `farm/*` `routes/farm-routes.ts` | 実機ファームの期限付きリース・ホスト報告・ロック付き JSON 保存 | なし | `core/farm.*` `adapters/farm_store.*` `app/farm_routes.*` |
| `capture/webrtc-broker.ts` `shared-rtp-source.ts` `ice-servers.ts` | WebRTC 送出 (offer→answer)・映像源の共有・PLI/REMB・STUN/TURN | relay / presenter は libdatachannel で同等の送受信を持つ (流用は依存のみ、クラスは別) | `webrtc/*` (libdatachannel) |
| `capture/ffmpeg-pipeline.ts` `video-pipeline-selector.ts` `bridge-stream.ts` | ffmpeg (gdigrab / ブリッジの BGRA) → RTP | presenter は自前エンコード (Media Foundation / VideoToolbox) で、ffmpeg 経路は持たない | `webrtc/video_pipeline.*` `core/ffmpeg_args.*` |
| `capture/h264-bridge-pipeline.ts` `bridge-h264-*.ts` `h264-rtp-packetizer.ts` | ブリッジの H.264 → RTP (ffmpeg なし) | relay は RTP を書き換えない中継 | `core/h264.*` `webrtc/video_pipeline.*` |
| `capture/video-feedback-policy.ts` | PLI の間引き・REMB の最小値でビットレート決定 | presenter に類似 (`video_pipeline`)、別実装 | `core/video_feedback_policy.*` |
| `capture/audio-*.ts` `process-loopback.ts` (+ `.cs`) | Windows のプロセス音声 → ffmpeg Opus → RTP | presenter は WASAPI loopback (デバイス単位) | `webrtc/audio_pipeline.*` (ProcessLoopback.cs のビルドと起動は従来どおり) |
| `capture/h264.ts` (`H264NalParser`) | Annex-B 分割 | — | 移さない (本番コードから参照なし。テスト専用の死にコード) |
| `electron/main.cjs` | デスクトップの殻 | — | 移さない (ブラウザで開く) |
| `public/**` | 操作画面 (ブラウザ) | — | 移さない。ビルド時に `odeum-runner` へ埋め込んで配信 |
| `native/custos-h264` `unity/**` | Unity ブリッジ (C# / DLL) | — | 対象外 (Node ではない) |

## 3. 安全の対応表 (移植前)

| 既存の防御 (TS) | 場所 | C++ での扱い |
|---|---|---|
| 既定 bind は loopback、非 loopback の無認証 bind は起動拒否 | `main.ts` | 同じ判定を `core/auth_policy` に移し、起動時に拒否 |
| `/api/apps/*` `/api/settings/*` `/api/unity/*` `/api/playlogs*` `/api/farm/devices*` は Cernere 認証 | `app.ts` `middleware.ts` | 同じ範囲 (前方一致) に認証をかける。素の `/api/apps` も含める |
| `/ws` は `?token=` / Bearer で同じ検証、失敗は 4401 で閉じる | `ws/handler.ts` | 同じ |
| Cernere の検証結果を 5 分キャッシュ | `cernere-auth.ts` | 同じ。上限件数を追加 (無制限に増えない) |
| 子プロセスは `shell:false` + 引数配列、bare な `.exe` は workingDir に昇格 | `runner.ts` | posix_spawn / CreateProcessW に引数配列。Windows の `.bat/.cmd` の直接起動は拒否 (Node 20 以降と同じ) |
| kill は Windows で木ごと (`taskkill /T /F`)、タイムアウトで SIGKILL | `runner.ts` | Windows は Job Object、POSIX はプロセスグループへ SIGKILL |
| 観戦者の入力・Unity 操作の拒否 (操作権) | `control-lease.ts` `control-guard.ts` | 同じ |
| WS 入力の境界検証 (pointer 0..1・感想 200 文字・制御文字除去・1 秒間隔・観客 750ms) | `ws/*` `audience/room.ts` | 同じ |
| apps 一覧で host / port / env / cwd を出さない | `apps-routes.ts` `redact` | 同じ |
| 行動ログの player は HMAC-SHA256 (salt は env か 0600 のファイル) | `player-hash.ts` | OpenSSL HMAC、乱数は `RAND_bytes` |
| 行動ログの取得は appId / run を正規表現で検査 (ディレクトリ外を読まない) | `playlog-store.ts` | 同じ。読み出しに大きさ上限を追加 |
| 行動ログは既存ファイルを再利用しない (`wx`) | `playlog-writer.ts` | `O_CREAT|O_EXCL` / `CREATE_NEW` |
| ファーム: 本文 256KiB 上限、ホスト token は SHA-256 で比較 (定数時間)、open/stub では起動しない、状態ファイルはロック + 一時ファイル + fsync + rename | `farm/*` | 同じ。比較は `CRYPTO_memcmp` |
| Unity プロキシは apps.json の `kind:"unity"` の登録先だけ (任意の接続先を取らない) | `bridge-target.ts` | 同じ |
| ブリッジ応答の PNG 検査、H.264 フレーム長の上限 16MiB | `ergo-custos-client.ts` `bridge-h264-frames.ts` | 同じ。Lorica の予算で課金 |
| 外部 I/O の期限: スクリーンショット 8 秒・キー 2 秒・ブリッジ 10 秒・TURN 10 秒・ffmpeg probe 8 秒 | 各所 | 同じ値。期限の無かった Cernere 検証・adb に期限を追加 |
| TURN の token をログに出さない | `ice-servers.ts` | 同じ |
| 静的配信 (`serveStatic`) | `app.ts` | ファイルシステムを読まない埋め込み配信に変更 (視線推定の資材だけ固定名の許可リストで読む) |

移植で追加する防御 (弱める方向の変更はしない): 全入力経路の Lorica 予算 (本文・JSON の要素数と深さ・子プロセス出力・ファイル)、
HTTP ヘッダ 16KiB / 本文 256KiB / WS メッセージ 64KiB の上限、接続数・WS 送信キューの上限、同一オリジン以外からの
状態変更リクエストと WS 接続の拒否 (CSRF / CSWSH)、loopback 待ち受け時の Host 検査 (DNS rebinding)。

## 4. 移行結果

### C++ の依存 (odeum-runner / odeum-unityctl)

| 依存 | 版 | 取得 | 推移的依存 | 備考 |
|---|---|---|---|---|
| C++20 標準ライブラリ・OS API | — | — | — | POSIX (posix_spawn, pipe2, waitpid) / Win32 (CreateProcessW, Job Object, SendInput) |
| Boost Asio / Beast (ヘッダのみ) | 1.87.0 | `boost-1.87.0-b2-nodocs.zip` SHA256 40efbb1f… | 0 | 既存 native と同じ |
| nlohmann_json | 3.11.3 | `json.tar.xz` SHA256 d6c65aca… | 0 | 既存 native と同じ |
| OpenSSL 3 | システム | `find_package(OpenSSL 3)` | 0 | TLS (Cernere / TURN)・HMAC・SHA-256・乱数 |
| Lorica | cab4f120 | FetchContent / `LORICA_SOURCE_DIR` | 0 | 必須。予算 (`input_budget`/`checked_*`) と `lorica_harden_target` |
| libdatachannel | d9391849 | 既存の FetchContent | libjuice v1.6.2・libsrtp ee1a77c・usrsctp fec583d・plog 94899e0 | 既存 relay と同じ。`ODEUM_RUNNER_WEBRTC=OFF` で外せる |

新しく増えた第三者ライブラリはない (Lorica は依頼で必須)。外部コマンドは ffmpeg / adb / csc.exe (移行前と同じ)。

### 意図した挙動の違い

- 状態を変えるリクエスト (POST / PUT …) と WebSocket 接続は、Origin が付いていて同一オリジンでも
  `CUSTOS_ALLOWED_ORIGINS` でもなければ 403 (CSRF / CSWSH)。loopback 待ち受けでは Host を
  localhost / 127.0.0.1 / [::1] / `LUDIARS_ALLOWED_HOSTS` に限る (DNS rebinding)。
- 全応答に `X-Content-Type-Options: nosniff`。設定 PUT の不正値のエラー文は zod の JSON ではなく短い文。
- 起動できないコマンドは 409 `{ok:false,error:"spawn <cmd> ENOENT"}` (Node 版は 200 を返して running のまま残った)。
  Windows の `.bat` / `.cmd` 直接起動は Node 20 以降と同じく拒否 (EINVAL)。
- kill とタイムアウトは POSIX でもプロセスグループごと (Windows は従来どおり木ごと、Job Object)。
- 出力の最後の行は exit 通知より前に必ず届く (exit は両パイプの EOF まで、最大 2 秒待つ)。
- 上限: HTTP ヘッダ 16KiB (431)・本文 256KiB (413)・WS 1 メッセージ 64KiB・毎秒 240 メッセージ・
  接続 512・WS セッション 128・WebRTC セッション 32・Cernere 同時検証 32 / キャッシュ 2048 件・
  ブリッジ入力キュー 256 / app・adb 同時 8・スクリーンショット 32MiB・行動ログ読み出し 64MiB。
- 操作画面はビルド時に埋め込み (`custos/public` をそのまま)。視線推定の資材 (`gaze-assets`) だけは
  固定名 9 ファイルの許可リストで `ODEUM_RUNNER_GAZE_ASSETS_DIR` (既定 `custos/public/gaze-assets`) から読む。
- Linux のデスクトップ入力注入は無し (ログのみ)。移行前も nut-js 不在時は同じ。macOS はビルド対象外 (Lorica)。

### テスト対応表 (custos/tests → ctest)

| TS テスト | C++ (ctest 名 > ケース) |
|---|---|
| config.test.ts (21) | odeum.runner.config > appsRootSchema / inAppBridge 系 + golden `config_cases.json` |
| apps-redact.test.ts (5) | odeum.runner.config > apps-redact (golden `redact_cases.json`) |
| registry.test.ts (3) | odeum.runner.config > AppsRegistry |
| comment-message.test.ts (7), control-lease.test.ts (8), audience-room.test.ts (3), stream-prefs.test.ts (6) | odeum.runner.messages (golden) |
| auth.test.ts (4) | odeum.runner.auth > CernereAuth (+ 偽 Cernere でのキャッシュ・失敗) |
| runner-resolve.test.ts (5) | odeum.runner.process > resolveBareExe (golden) + AppsRunner |
| playlog-row.test.ts (8), playlog-recorder.test.ts (9) | odeum.runner.playlog |
| unity-bridge.test.ts (21) | odeum.runner.unity (golden `unity_cases.json` + 偽ブリッジでのプロキシ) |
| farm-pool (10) / farm-store (4) / farm-host-auth (2) | odeum.runner.farm (golden `farm_cases.json`) |
| farm-routes.test.ts (5) | odeum.runner.http > farm-routes |
| audience-broker.test.ts (2) | odeum.runner.http > audience-broker |
| bridge-h264 (9), bridge-stream (29), ice-servers (9), scale (5), screenshot-stream (8) | odeum.runner.media (golden h264 / ffmpeg / ice / keys / frame headers) |
| shared-rtp-source (8), video-pipeline (6), audio-source (6) | odeum.runner.webrtc (+ loopback の WebRTC 2 本: ブリッジ H.264 / BGRA→ffmpeg) |
| h264.test.ts (4) | 移さない (`H264NalParser` は本番コードから参照なし) |
| play-eye-tracking / play-input-logic / rtc-stats-logic / unity-panel-logic / virtual-pad-logic / window-layout-logic | 移さない (ブラウザ側 JS。vitest のまま) |

追加: odeum.runner.boundary (ファイル・ブリッジ応答・ストリームのエラー本文・ファームの件数上限)。

### 本社での実行手順

```sh
# 1. ビルド (Windows は MSVC、Linux は GCC。どちらも警告ゼロ確認のため WERROR を付ける)
cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release -DODEUM_RUNNER_WERROR=ON
cmake --build native/build --config Release
ctest --test-dir native/build -C Release --output-on-failure      # 期待: 全件 Passed

# 2. golden の作り直し (Node 側の記録。差分が出なければ移植と Node 版が一致)
cd custos && npm ci --ignore-scripts && npx tsx ../native/runner/tests/golden/generate.mts && cd ..
git diff --exit-code native/tests/fixtures/runner                   # 期待: 差分なし

# 3. HTTP / WebSocket の互換性 (合成 apps.json、外部サービスなし)
node native/runner/tests/compat/http_compat.mjs record node.json -- npx --prefix custos tsx custos/src/main.ts
node native/runner/tests/compat/http_compat.mjs record cpp.json -- native/build/bin/odeum-runner
node native/runner/tests/compat/http_compat.mjs compare node.json cpp.json   # 期待: "compatible (only expected differences)"
```

実機確認 (本社のみ): Windows でアプリの build / run / kill (子孫ごと終了)、仮想キー (SendInput)、
gdigrab キャプチャの WebRTC 表示、Unity ブリッジ (H.264 / BGRA / `/pointer` / `/state` / `unityctl compile --wait`)、
プロセス音声 (ProcessLoopback.exe の csc ビルド)、Android (adb)、Cernere 認証と TURN、ファームのホスト報告。
