---
task: cocoiru-sender
project: Cocoiru
kind: implementation
created: 2026-10-09
memory_links:
  - spec/plan/2026-10-09-odeum-self-made-program-pipeline.md
  - spec/feature/live-relay.md
---

# Cocoiru の送信を FFmpeg/SRT から odeum_sender へ差し替える (run 3)

設計の正本: Odeum `spec/plan/2026-10-09-odeum-self-made-program-pipeline.md` の「Cocoiru の送信差し替え (run 3)」節。
対象リポは Cocoiru (`E:/Document/Ars/Cocoiru`)。委託時はこの設計書と本ファイルを Cocoiru 側の worktree にコピーして渡す。
run 2 (`odeum_sender` のエクスポート) がマージ済みであることが前提。

## 目的

Cocoiru の画面共有を、FFmpeg 子プロセス + SRT + MediaMTX + OBS ではなく、Odeum の `odeum_sender` (WGC + MF H.264 + Opus + WebRTC) と
odeum-relay の入力スロットで行う。返送プレビューは relay の program スロットを viewer として受けて復号表示する。

## 対象 (Cocoiru)

- `apps/tela-resident/CMakeLists.txt` — `find_package(Odeum CONFIG REQUIRED)` で `odeum_sender` を link。
- `apps/tela-resident/src/sharing/` — `media_arguments.*`、`process.*` (FFmpeg 引数組み立て・子プロセス管理) を削除。
  `srt_url.*` を `odeum://present?relay=<wss>&ticket=<JWS>` 形式の送信リンク解析 (slot 必須) に置き換え、DPAPI 保護 (`protected_text`) はそのまま使う。
  `window.*` の UI は「対象の選択 / 送信リンクの貼り付け / 返送リンクの貼り付け / 開始 / 停止」の構成を保つ (FFmpeg パス欄は削除)。
- 返送プレビュー `preview.*` — viewer チケットで relay に接続し、program スロットの H.264 を Media Foundation で復号して既存の窓に BGRA で描く。
  「待ち / 停止 / live」の表示と、古いフレームを live と見せない規則、取り込み除外、角の配置はそのまま。
- 送信対象が閉じた / 最小化 / 置き換わったら送信を止め、デスクトップへ fallback しない (既存どおり)。
- `spec/SCREEN-SHARING.md` を書き換える (FFmpeg / SRT / OBS / MediaMTX の記述を外し、odeum_sender と relay の入力スロットに置き換える)。
- `spec/domains/` の所属更新。

## 対象外

- Odeum 側の変更。GLab 側のチケット発行。Cocoiru の他機能 (ojisan 等)。

## 受入条件

1. 単体テスト: 送信リンクの解析 (slot 必須・`wss://` 以外拒否)、プレビューの状態遷移 (待ち / live / 停止)、設定の保存と DPAPI 往復 (既存テストの更新)。
2. `grep -ri "ffmpeg\|srt://\|mpegts" apps/tela-resident/src` が 0 件。PR 説明にこの grep の結果を書く。
3. Windows でビルドが通る構成。実機の送信・返送確認は人間の許可後。未実施は PR に書く。

## 作業環境の注意 (必読)

- 作業場所は Cocoiru の worktree。`git worktree` / `git switch` で他の場所へ移らない。
- Claude 系の委託先はその worktree で `git add <paths>` + `git commit` を使ってよい。Codex 系は `.concordia-commit.json` を worktree 直下に置いて Concordia にコミットを依頼し、5 分待って `git log -1` で確認する。
- Anatomia は `node E:/Document/Ars/Anatomia/bin/anatomia.mjs` で呼び、`ANATOMIA_VESTIGIUM=0` と `VESTIGIUM_LOGS_DIR=<worktree>/.vestigium-logs` を設定する。
- local PR は Concordia の `/v1/prs/local/direct` で提出する。
- 完了報告は delegation status へ送り、報告してから終了指示を待つ。
- ビルドはフォアグラウンドで待つ (バックグラウンドにしない)。
