# ネイティブ中継の実装記録

Actio: `actio:3d1e10f6-079a-43a3-9a98-50b361b800d4`

## 作業単位

- 共通プロトコル: JSON 境界、UTF-8 文字数、JWS Ed25519、公開鍵読込。
- 中継ドメイン: リプレイ防止、流量制限、投票、250ms burst、presence。
- 通信: HTTP/WS 単一ポート、認証、WebRTC RTP 転送、集約キーフレーム要求。
- 運用: 環境設定、CMake、Excubitor、永続データなしの export/import。
- 提出: Concordia コミット依頼、Revisor local PR、delegation status。

## テスト計画

`augur plan --kind new_feature --domain service` の正常系・利用側契約・不正入力の3提案を、
CTest の protocol / ticket / relay-state テストに対応させる。実時刻を使わず ms / epoch 秒を注入する。
署名を実際に生成して誤署名、aud、exp、kid、jti を検証する。
Unicode 境界、未知 type、個人単位の制限、投票上書き、250ms と 1000ms の境界を検証する。
サービス起動・実機視聴は行わない。

## 契約観測の制約

実装前に `augur.contracts.json` と述語を定義した。Augur の現行注入器は JS/TS 宣言を対象とし、
C++ のクラスメソッドへ JS wrapper を挿入できない。C++ テストの成功を Augur 観測済みとして偽装しない。
注入・report の結果とビルド可否を提出時に記録する。

## 実装・検証結果

共通プロトコル、署名検証、状態管理、HTTP/WS、SFU、環境設定、CTest 4 executable、
Excubitor と移行スクリプト、ドメイン定義を追加。

- `Anatomia verify --json`: pass（C++ のビルド検証を代替するものではない）。
- `node --check`: 導入・移行スクリプト5本を通過。
- `git diff --check`: 通過。
- CMake configure: 子プロセスの Path/PATH 重複を整理すると MSVC 19.39 と OpenSSL 3.2.1 を検出。
  その後、GitHub:443 への接続が Bad access で拒否され、JSON archive の取得で停止。
- CMake build: configure 未完了のため ALL_BUILD.vcxproj がなく失敗。コンパイル成功は未確認。
- 単体・統合・サービス起動・ブラウザ視聴は未実施。サービス起動なし、テスト実行の追加指示なし。
- Augur inject: C++ の parse_message / authorize_sessions は unresolved、applied=0。
  report は C-1〜C-6 の全件が `uncovered (not-injected)`。観測済みの申告はしない。
- `DELEGATION_STARTED_AT` は環境に存在しないため、status API の run.created_at
  (`2026-10-04T02:12:32.127Z`) を report の since に使用。

## 提出時の未充足事項

依存取得可能な環境での Windows ビルドと CTest、C++ 対応の契約観測が必要。
OpenSSL 3 は upstream libdatachannel と共有する system dependency として検出しており、
タスクの「全依存を FetchContent で取得」に対してこの1件は未充足。
HTTP/WS 同居には Boost.Beast を採用しているため、WebSocket 部も libdatachannel に限定する解釈なら差異がある。
macOS/Linux ビルドと実機視聴も未確認。マージ・反映は今回の完了条件に含めない。
