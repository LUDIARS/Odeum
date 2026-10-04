---
task: odeum-relay
project: Od
kind: implementation
created: 2026-10-04
memory_links:
  - spec/plan/2026-10-04-odeum-live-presentation-design.md
---

# Odeum 中継サーバ (odeum-relay) と共通プロトコル

設計の正本: `spec/plan/2026-10-04-odeum-live-presentation-design.md` (このリポ内のコピー)。設計書の「チケット」「中継 API」「WebSocket メッセージ」「メディア」「Excubitor / ブートストラップ」節をすべて実装する。最小版・スタブで済ませない。

## 目的

GLab のライブプレゼン配信で、発表者アプリが送る画面映像を GLab の Web 視聴者へ中継し、グッド・スタンプ・コメント・投票回答を発表者へ返す。Node は使わずネイティブ (C++20) で作る (neco 指示)。

## 対象 (このリポに新設)

- `native/CMakeLists.txt` (トップ)、`native/protocol/` (ライブラリ `odeum_protocol`)、`native/relay/` (実行ファイル `odeum-relay`)、`native/tests/`。
- 依存: libdatachannel (WebRTC/WebSocket)、JSON (nlohmann/json)、Ed25519 検証 (libsodium または OpenSSL 3 の EVP)。取得は CMake `FetchContent` で版を固定 (タグとハッシュ)。Windows (MSVC) / macOS (Apple Clang) / Linux (GCC) でビルドできる書き方にする。
- `odeum_protocol`: メッセージ型の定義・JSON との相互変換・長さ/件数の検証・チケット (JWS EdDSA) の解析と検証。発表者アプリも同じライブラリを使うので、OS API と中継の都合を持ち込まない。
- `odeum-relay`: HTTP (`/health`, `GET /v1/sessions`)、WebSocket (`/v1/ws?ticket=`)、SFU 転送 (RTP をそのまま視聴者へ、PLI/FIR の集約転送、新規視聴者でキーフレーム要求)、グッド/スタンプ/コメント/投票、流量制限、上限、`jti` 再利用拒否、250ms ごとの `reaction.burst`、1 秒以上間隔の `tally`、`presence`。
- 設定は環境変数: `ODEUM_RELAY_PORT` (既定 4400)、`ODEUM_RELAY_BIND` (既定 127.0.0.1)、`ODEUM_RELAY_TICKET_PUBKEYS` (kid→PEM の JSON ファイルパス、必須。無ければ起動失敗)、`ODEUM_RELAY_UDP_PORT_RANGE`、`ODEUM_RELAY_PUBLIC_IP`、`ODEUM_RELAY_ICE_SERVERS` (JSON)、`ODEUM_RELAY_MAX_VIEWERS` (既定 300)、`ODEUM_RELAY_MAX_SESSIONS` (既定 32)。不正値は起動時に失敗させる。
- ログ: 表示名・コメント本文・チケットを出さない。session_id と件数のみ。
- `excubitor.catalog.yaml` (code `odeum-relay`, port 4400, runtime native, autostart false, health `http://localhost:4400/health`, `provides: ODEUM_RELAY_URL`)。
- `excubitor.bootstrap.json` と `scripts/site/setup.mjs` (CMake configure + build、再実行可能) / `scripts/site/export-data.mjs` / `scripts/site/import-data.mjs` (永続データ無しを明示した空形式。`--output` / `--input --sha256` の契約は Castra `E:/Document/Ars/.agents/skills/service-bootstrap/SKILL.md` 準拠)。
- `spec/domains/` に `live-protocol.domain.json` (native/protocol) と `live-relay.domain.json` (native/relay, scripts/site, excubitor.*) を追加し、新規ファイルをすべてどれかのドメインに属させる。`native/tests/` は既存 `test-support` の pattern に含まれるか確認し、含まれなければ追加する。
- `spec/feature/live-relay.md` に実装した API・設定・制限を書く。

## 対象外

- 既存 .NET アプリ (`src/Spectator.*`) と `Spectator.sln` は触らない。
- 発表者アプリ (`native/presenter/`) は別タスク。GLab 側も別タスク。

## 受入条件

1. `native/tests/` に単体テスト: プロトコルの JSON 往復と境界値 (文字数・件数・未知 type)、チケット検証 (正常 / 署名不正 / aud 違い / 期限切れ / jti 再利用 / kid 不明)、流量制限 (グッドは切り捨て受理、stamp/comment は rate_limited)、投票 (1 人 1 回・上書き・複数選択)、burst 集計 (250ms 窓・0 件は送らない)。CTest で実行できる。
2. Windows で `cmake -S native -B native/build` と `cmake --build native/build --config Release` が通る構成。テストの実行は依頼者の許可範囲に従い、実行しなかったものは PR に未実施と書く。
3. SFU 転送部はメディアを再符号化しない。
4. `GET /v1/sessions` は `role=service` チケット以外を 401/403 で拒否する。

## 作業環境の注意 (必読)

- git の書き込み (branch/commit/push) は実行しない。コミットはこの worktree 直下に `.concordia-commit.json` (`{"message": "...", "paths": [...]}`) を置いて Concordia に依頼し、5 分待って `git log -1` で確認する。
- Anatomia は `node E:/Document/Ars/Anatomia/bin/anatomia.mjs` で呼び、`ANATOMIA_VESTIGIUM=0` と `VESTIGIUM_LOGS_DIR=<この worktree>/.vestigium-logs` を設定する。
- local PR は Concordia の `/v1/prs/local/direct` で提出する。
- 完了報告は delegation status へ送る。Actio の本文 API が取れなければこのファイルを本文として扱う。
