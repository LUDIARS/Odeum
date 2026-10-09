---
task: odeum-relay-slots
project: Od
kind: implementation
created: 2026-10-09
memory_links:
  - spec/plan/2026-10-09-odeum-self-made-program-pipeline.md
  - spec/plan/2026-10-04-odeum-live-presentation-design.md
  - spec/feature/live-relay.md
---

# Odeum 中継の入力スロットと producer (run 1)

設計の正本: `spec/plan/2026-10-09-odeum-self-made-program-pipeline.md` の「odeum-relay の拡張 (run 1)」節。
その節に書かれた内容をすべて実装する。最小版・スタブで済ませない。

## 目的

番組配信を自作するため、1 room に複数の送信元 (input1〜input4) と番組 (program) を持たせ、
親機の `odeum-program` (役割 `producer`) が全入力を受けて番組を返せるようにする。既存の 1 発表者構成 (`slot` 省略) は互換のまま。

## 対象 (このリポ)

- `native/protocol/` — `Role::producer` の追加、Ticket の任意 `slot` claim (`input1`〜`input4` / `program`、省略時 `program`)。
  viewer / service / overlay に `slot` が付いていたら invalid。未知値は invalid。`role_name` / `parse_role` の対応。
- `native/relay/hub.*` — `Room.presenter` をスロット別 (`std::map<std::string, std::uint64_t>`) に。producer は room に 1 接続まで、
  視聴者数に数えない、クライアントからの投稿 (good / stamp / comment / telop / submission / poll.*) は `forbidden`、全員向け broadcast は受け取る。
  room の寿命は「いずれかのスロットに接続がある間」。program の切断では閉じない。全スロットが空になったら破棄。
  `presence` と `GET /v1/sessions` に `slots` (スロット名 → 接続中 bool) と `program_connected` を足す。
- `native/relay/media.*` — `sources_` をスロット別に。viewer の配信元は program があればそれ、無ければ input1〜 の若い順。
  配信元の切替で viewer へ再 offer + キーフレーム要求。producer には接続中の全入力スロットの recvonly offer を送り、
  入力の増減に追随する。producer からの sendonly (program) を受ける。PLI/FIR の集約はスロットごと。
- `native/relay/config.*` — `ODEUM_RELAY_MAX_INPUTS` (既定 4、1〜8)。不正値は起動失敗。
- `native/tests/` — 受入条件のテストを追加。既存テストは通る構成のまま。
- `spec/feature/live-relay.md` — slot / producer / presence / 設定の追加を書く。
- `spec/domains/live-relay.domain.json` / `live-protocol.domain.json` — 新規ファイルがあれば所属させる。

## 対象外

- `native/presenter/`、`broadcast/`、`src/Spectator.*`、GLab 側のチケット発行。
- 符号化・復号 (中継は再符号化しない)。

## 受入条件

1. 単体テスト: `slot` claim の検証 (未知値 / viewer に付いたら invalid / 省略時 program)、producer の権限 (投稿 forbidden、視聴者数に不算入、broadcast は受信)、
   スロット別の presenter 重複拒否 (`presenter_exists`)、配信元の選択順序と切替、room 寿命 (program 切断で閉じない / 全空で閉じる)、presence と sessions の項目。
2. 既存テスト (protocol / ticket / relay-state / guest / overlay / media) が全部通る構成のまま。`slot` 省略の既存クライアントの振る舞いが変わらない。
3. Windows で `cmake -S native -B native/build` と `cmake --build native/build --config Release` が通る構成。テストの実行は依頼者の許可範囲に従い、実行しなかったものは PR に未実施と書く。
4. ログに表示名・本文・チケットを出さない (既存不変)。

## 作業環境の注意 (必読)

- 作業場所はこの worktree。`git worktree` / `git switch` で他の場所へ移らない。
- Claude 系の委託先はこの worktree で `git add <paths>` + `git commit` を使ってよい。Codex 系は `.concordia-commit.json` (`{"message": "...", "paths": [...]}`) を worktree 直下に置いて Concordia にコミットを依頼し、5 分待って `git log -1` で確認する。
- Anatomia は `node E:/Document/Ars/Anatomia/bin/anatomia.mjs` で呼び、`ANATOMIA_VESTIGIUM=0` と `VESTIGIUM_LOGS_DIR=<この worktree>/.vestigium-logs` を設定する。
- local PR は Concordia の `/v1/prs/local/direct` (`{"repo_path": "<この worktree>", "branch": "<このブランチ>", "session_id": "<自分の session id>"}`) で提出する。
- 完了報告は delegation status へ送り、報告してから終了指示を待つ。Actio の本文 API が取れなければこのファイルを本文として扱う。
- ビルドはフォアグラウンドで待つ (バックグラウンドにしない)。
