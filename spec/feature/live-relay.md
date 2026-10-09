# Odeum native live relay

Actio 参照: `actio:3d1e10f6-079a-43a3-9a98-50b361b800d4`
設計の正本: [ライブ配信設計](../plan/2026-10-04-odeum-live-presentation-design.md)。
入力スロットと producer: Actio 参照 `actio:325dfdf6-06a3-4b84-85b7-3483b5a41150`、
設計の正本 [番組配信の自作 設計](../plan/2026-10-09-odeum-self-made-program-pipeline.md) の「odeum-relay の拡張 (run 1)」節、
タスク定義 [odeum-relay-slots](../tasks/2026-10-09-odeum-relay-slots.md)。

## 責務と依存

`odeum_protocol` は JSON の型・境界と Ed25519 JWS を検証し、OS・通信を参照しない。
relay の認証リプレイ台帳、リアクション窓、投票は独立した状態クラス。
Hub と HTTP/WS は単一 Asio executor 上で動作し、libdatachannel callback は同 executor に戻す。
メディア待ち行列は room ごと512パケット、WS 出力は接続ごと256メッセージ/256 KiB。

既存 .NET の動画レビューは責務・ランタイムとも異なり、再利用しない。
libdatachannel の RTP transport と nlohmann/json を利用する。HTTP と WebSocket の同一ポートでの
認証・upgrade を扱うため Boost.Beast を追加。独自 WebSocket フレーム実装は持たない。
ライセンス: libdatachannel MPL-2.0、nlohmann/json MIT、Boost BSL-1.0、OpenSSL Apache-2.0。
libdatachannel は v0.23.3 の commit、JSON 3.11.3 と Boost 1.87.0 は SHA256 付き archive で取得する。
OpenSSL 3 は libdatachannel と共通のシステム開発依存 (`find_package`)。

## API

- `GET /health`: `{ok:true,version:"0.1.0"}`。
- `GET /v1/sessions`: `Authorization: Bearer <service-ticket>` 必須。認証失敗401、役割違い403。
  `[{sid,presenter_connected,viewer_count,started_at,program_connected,slots}]`、時刻は Unix epoch ミリ秒。
  `slots` と `program_connected` は presence と同じ (後述「入力スロットと producer」)。
- `GET /v1/ws?ticket=<compact-JWS>`: viewer/presenter/producer 専用。JWS は URL-safe なのでそのまま query に渡す。
  認証後 welcome、presence、開いている投票を送る。接続上限などの参加拒否は WS error。
  送信キュー超過・不正フレーム・毎秒120を超えるメッセージでは切断する。

- `GET /join`, `GET /overlay`, `GET /web/*`: ゲスト参加ページと番組オーバーレイ (バイナリ埋め込み)。
- `GET /v1/guest?code=&name=` / `GET /v1/overlay?key=`: 参加コード・overlay 鍵で WS 参加。
  照合結果は upgrade 後に WS error で返す。詳細は [SPEC-PROGRAM-OVERLAY-GUEST-JOIN](program-overlay-guest-join.md)。

チケットの `alg=EdDSA`、kid、Ed25519 署名、iss=glab、aud=odeum-relay、sub/name/role/sid/jti/exp を検証。
`now < exp <= now+300`。任意の iat があれば発行から300秒以内も検証。
接続確立後の期限では切断しない。jti は HTTP と WS で共通、exp まで再利用を拒否。
使用済み jti は最大100000、超過は認証を拒否してメモリを制限する。
role は presenter / viewer / service / producer。任意の `slot` claim は presenter と producer だけが持てる
(値は `input1`〜`input8` か `program`、未知値・他 role に付いたら `invalid_ticket`)。
presenter の `slot` 省略は `program`。producer の `slot` は省略か `program` のみ。`invite` claim は presenter と producer が持てる。

JSON メッセージは16 KiB。文字数は Unicode scalar 数。メッセージ種別は設計の表に準拠。
comment 280文字、name 64文字、選択肢2–6件・各60文字、good 1–50。
補助上限: sub/sid/jti 256文字、poll_id 128文字、question 1000文字、ICE candidate 4096文字。
未知typeは `unknown_type`、型や長さの違反は `invalid_message`。
クライアントからサーバー専用typeや役割の違う操作を受信したら `forbidden`。

## 集計

同一 session/sub の rolling window: good 30/秒を超えた部分は切捨て、stamp 2/秒、comment 1/3秒。
stamp/comment 超過は `rate_limited`。stamp/comment は本人情報と epoch ミリ秒 at を発表者だけに返す。
250ms 以上の間隔で good/stamps の非空 `reaction.burst` を全員へ送る。
poll は1件だけ open。sub ごと回答を上書き、multi=false は1選択のみ。
重複・範囲外 index を拒否。tally は変更時のみ1秒以上の間隔、close で最終集計を返す。
投票参加者は最大100000人。退出しても回答は close/次の open まで保持する。
presence は参加・退出時。空 session は破棄。送信スロットが全て空になったら視聴者も切断し、新しいチケットで再接続する
(`slot` 省略の 1 発表者構成では従来どおり「発表者の切断 = 視聴者の切断」)。

## 入力スロットと producer

1 room は入力スロット `input1`〜`inputN` (N = `ODEUM_RELAY_MAX_INPUTS`) と番組スロット `program` を持つ。
各スロットの送信者は 1 接続まで。埋まっているスロットへの参加は `presenter_exists`、
N を超える入力スロットは `slot_unavailable`。welcome は送信者に `slot` を返す。

- producer (odeum-program) は room に 1 接続まで (`producer_exists`)。`program` スロットを占め、
  `program` が presenter で埋まっていれば `presenter_exists`。視聴者数に数えない。
- producer の good / stamp / comment / telop / submission / poll.* は `forbidden`。全員向け broadcast
  (公開 telop / submission、reaction.burst、poll、tally、presence) は受け取る。本人情報付きの stamp / comment 通知と
  非公開 submission は presenter (全スロット) にだけ送り、producer と視聴者には送らない。
  `reaction.ready` は presenter と producer が送れる。
- room の寿命は「いずれかのスロットに送信者がいる間」。`program` の切断では閉じず、入力が残っていれば
  次の producer が再接続できる。全スロットが空になったら room を破棄し、招待ダイジェストを解放する。
- presence に `program_connected` (bool) と `slots` (`{"input1":true,...,"program":false}`、設定上の全入力と program) を足す。
  `presenter_connected` はいずれかのスロットに送信者がいれば true。

## メディア

発表者が sendonly offer を送信、中継が answer。H.264 constrained baseline / packetization-mode=1 の
映像1本と任意の Opus 音声1本、各1 SSRC を要求する。simulcast/datachannel は拒否。
中継が視聴者へ sendonly offer、ブラウザは recvonly answer を返す。
source の PT/SSRC/MID を保持して RTP と sender RTCP を転送し、符号化・復号は行わない。
視聴者の PLI/FIR と新規加入・track open を集約して最短1秒間隔で source に PLI を返す (集約はスロットごと)。

source はスロットごとに 1 本 (送信者の offer 受理時に登録、切断で削除)。中継が受信者へ出す m-section の mid は
`<slot>-<n>` (n は接続ごとの通番) で、送信元の MID / RID ヘッダ拡張は落とし SSRC で振り分けさせる。

- 視聴者の配信元は `program` があればそれ、無ければ番号の最も若い入力。配信元が変わったら旧 section を removed
  (port 0) にした新しい offer を送り、新しい配信元へキーフレームを要求する。offer の応答待ちに変化があれば、
  answer 受理後にまとめて再 offer する。
  answer の検証は中継が現在送っている section だけを数える (libdatachannel の受信側は中継が removed にした section を
  active のまま返すため、退役済み mid は無視し、中継が割り当てていない mid は `invalid_sdp`)。
- producer は WS 1 本で PeerConnection を 2 本持つ。受信側は中継が offer し (接続中の全入力スロット、`program` は含めない)、
  入力の増減に追随する。入力が終わったら `{"type":"track.closed","mids":[...]}` を送ってから再 offer する。
  送信側は producer が sendonly offer を 1 回送り (presenter と同じ検証)、`program` の source になる。
  producer からの `sdp` は offer なら送信側、answer なら受信側。`candidate` は中継が割り当てた mid なら受信側、それ以外は送信側。
- producer の PLI/FIR はその section のスロットの送信元へ、視聴者の PLI/FIR は配信元へ返す。
受信側の RR/NACK は中継で終端し、再送キャッシュは持たない。
PUBLIC_IP はローカル UDP host candidate の address を置換し、port を保持する。
NAT では同一 UDP port の転送が必要。TURN/srflx candidate は変更しない。

## 設定

| 環境変数 | 既定・形式 |
| --- | --- |
| ODEUM_RELAY_PORT | 4400、1–65535 |
| ODEUM_RELAY_BIND | 127.0.0.1、IP literal |
| ODEUM_RELAY_TICKET_PUBKEYS | 必須、kid→Ed25519 PEM の非空 JSON ファイル |
| ODEUM_RELAY_UDP_PORT_RANGE | 1024-65535、begin-end |
| ODEUM_RELAY_PUBLIC_IP | 任意、IP literal |
| ODEUM_RELAY_ICE_SERVERS | []、WebRTC形式 `{urls,username?,credential?}` の配列 |
| ODEUM_RELAY_MAX_VIEWERS | 300、1–10000 |
| ODEUM_RELAY_MAX_SESSIONS | 32、1–1024 |
| ODEUM_RELAY_MAX_INPUTS | 4、1–8。room あたりの入力スロット数 |
| LUDIARS_ALLOWED_HOSTS | Excubitor 注入の追加 Host、カンマ区切り。先頭ドットは親・サブドメイン |
| ODEUM_RELAY_PUBLIC_URL | 任意、公開 HTTPS Origin。Host と Origin の許可に共用 |
| ODEUM_RELAY_ALLOWED_ORIGINS | GLab の HTTPS Origin を完全一致で列挙。カンマ区切り |

明示された空値・不正値は起動失敗。表示名、本文、チケット、SDP をログに出さない。
`config.cpp` → `WebAccess::from_environment` → `Connection::route` で Ex 注入 Host を検証する。
loopback Host は常に許可。Origin のない native/Bearer 通信は許可し、ブラウザ Origin は完全一致のみ。
relay 自身が配信するページとゲスト/overlay の受け口だけは、Origin が `http(s)://<許可済み Host>` の same-origin を許可する。
GLab の公開 Origin を `ODEUM_RELAY_ALLOWED_ORIGINS` に設定する。公開URLの推測や公開設定変更は行わない。
公開 TLS は外部 proxy、サービス自体は HTTP。UDP は HTTP tunnel を経由しない。

## 導入と検証

MSVC / Apple Clang / GCC の C++20 と CMake >=3.24、Git、OpenSSL >=3 の開発環境が必要。
`cmake -S native -B native/build`、`cmake --build native/build --config Release`。
許可された環境で `ctest --test-dir native/build -C Release --output-on-failure`。
出力は `native/build/bin/odeum-relay[.exe]`。Node はセットアップと移行スクリプトのみ使用。
setup は鍵を事前検証して configure/build、再実行可能。サービスを起動しない。
export/import は `{service:"odeum-relay",version:1,persistentData:false,data:{}}` のみ。
export は既存出力を拒否し、import は通常ファイル・SHA256・形式を検証、書込みや展開は行わない。
既存 `test-support` のパターンは `native/tests/` も含む。
