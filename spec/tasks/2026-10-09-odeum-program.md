---
task: odeum-program
project: Od
kind: implementation
created: 2026-10-09
memory_links:
  - spec/plan/2026-10-09-odeum-self-made-program-pipeline.md
  - spec/feature/live-relay.md
  - spec/feature/live-presenter.md
---

# Odeum 番組制作プロセス odeum-program (run 2)

設計の正本: `spec/plan/2026-10-09-odeum-self-made-program-pipeline.md` の「odeum-program (run 2)」節と「全体構成」「責務の境界」。
run 1 (`spec/tasks/2026-10-09-odeum-relay-slots.md`) がマージ済みの main を起点にする。

## 目的

OBS + MediaMTX を使わずに、4 入力の切替・合成・リアクション合成・YouTube Live 送出・番組返送を行う親機プロセスを作る。

## 対象 (このリポに新設)

- `native/program/core/` — 番組状態、レイアウト矩形、音声ミキサ、FLV 多重化、RTMP 握手 / AMF0 / chunk / ack、再接続判断、設定。OS API を持たない。
- `native/program/platform/windows/` — Media Foundation の H.264 復号と AAC-LC 符号化。
- `native/program/platform/macos/` — VideoToolbox の H.264 復号と AudioToolbox の AAC-LC 符号化 (Mac 拠点でのビルドが必要、未ビルドは PR に書く)。
- `native/program/render/` — Pictor で 1920x1080 のオフスクリーン合成 (NV12 ×4 のテクスチャ配置 + リアクション層) と GPU 読み戻し。
  Pictor に offscreen target + readback の経路が無ければ、Pictor 側に足す変更は別 PR として報告し、本 PR はその API を前提にした実装 + インタフェースで分離する。
- `native/program/transport/` — relay との WebSocket (producer チケット)、PeerConnection (recvonly ×4 + sendonly program)、RTMPS (OpenSSL)。
- `native/program/app/` — Tela の操作パネル (入力サムネイル、切替、ミュート、音量、YouTube 送出と返送の開始 / 停止、キー設定、状態表示)。
- 既存 `native/presenter/core/video_encoder.hpp` に profile (Baseline / Main / High) と B フレーム数の設定を足す (presenter の既定は変えない)。
- `native/presenter/` の取り込み・符号化・送信層を静的ライブラリ `odeum_sender` として `install` / `OdeumConfig.cmake` でエクスポートする (run 3 の Cocoiru が link する)。
- YouTube ストリームキー: macOS は Keychain、Windows は DPAPI。ログ・エラー文・PR・テンプレートに出さない。
- `excubitor.catalog.yaml` に `odeum-program` (runtime node、command `native/build/bin/odeum-program`、autostart false、health process)。`scripts/site/setup.mjs` が program もビルドする。
- `spec/feature/program-production.md`、`spec/domains/program-production.domain.json` (新規ファイルを全部所属させる)。

## 対象外

- `broadcast/` の削除 (実機確認後の別 PR)。Cocoiru 側の変更 (run 3)。GLab 側のチケット発行。
- カメラ / HDMI 取り込み、録画、任意レイアウト編集。

## 受入条件

1. Core の単体テスト (CTest): レイアウト矩形 (全画面 1〜4 / 4 分割 / 待機 / 終了)、ミキサ (ミュート / クリップ防止 / モード別の入力選択)、FLV タグのバイト列 (既知ベクタ)、RTMP 握手 (C0/C1/C2 / S0/S1/S2) と AMF0 の encode / decode (既知ベクタ)、chunk 分割と ack window、再接続バックオフ (1→30 秒)、設定の境界値。
2. Windows で `cmake -S native -B native/build` と Release ビルドが通る構成。テストの実行は依頼者の許可範囲に従い、未実施は PR に書く。
3. 番組 1080p30 / 既定 8 Mbps / キーフレーム 2 秒、返送 640x360 / 1.2 Mbps / Constrained Baseline、音声 AAC-LC 128 kbps 48 kHz ステレオ (YouTube) と Opus (返送)。
4. 入力が落ちたら古いフレームを出さず「入力未接続」板を出す。
5. 実機の復号・合成・YouTube 限定公開・返送は人間の許可後。PR に実施 / 未実施を明記。

## 作業環境の注意 (必読)

- 作業場所はこの worktree。`git worktree` / `git switch` で他の場所へ移らない。
- Claude 系の委託先はこの worktree で `git add <paths>` + `git commit` を使ってよい。Codex 系は `.concordia-commit.json` (`{"message": "...", "paths": [...]}`) を worktree 直下に置いて Concordia にコミットを依頼し、5 分待って `git log -1` で確認する。
- Anatomia は `node E:/Document/Ars/Anatomia/bin/anatomia.mjs` で呼び、`ANATOMIA_VESTIGIUM=0` と `VESTIGIUM_LOGS_DIR=<この worktree>/.vestigium-logs` を設定する。
- local PR は Concordia の `/v1/prs/local/direct` (`{"repo_path": "<この worktree>", "branch": "<このブランチ>", "session_id": "<自分の session id>"}`) で提出する。
- 完了報告は delegation status へ送り、報告してから終了指示を待つ。Actio の本文 API が取れなければこのファイルを本文として扱う。
- ビルドはフォアグラウンドで待つ (バックグラウンドにしない)。
