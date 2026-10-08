# SPEC-PROGRAM-OVERLAY-GUEST-JOIN

neco (2026-10-09) の選択:
- OBS 番組へのリアクション合成は **ブラウザソース案**。
- スマホ参加は **QR + 参加コード (ログイン不要)** と **GLab ログイン** の両方を受け付ける。

Actio: `actio:dbf75f13-a845-4b0d-82d0-f20df07c429c` (スマホリアクション全体)。
GLab 側の発行・表示は `actio:af82f06f-582b-4512-8afb-34135ad5dc8c` の続き。

## 全体

```
GLab (発表開始) ── 参加コード / 番組オーバーレイ鍵を発表セッションごとに生成
   │  presenter チケットに両者の SHA-256 だけを載せる (invite claim)
   ▼
odeum-presenter ──WS──▶ odeum-relay ── room に invite hash を登録
                                │
   スマホ (同じ Wi-Fi) ── http://<relay>/join  ── コード照合 → ゲスト viewer として参加
   OBS ブラウザソース ─── http://127.0.0.1:4400/overlay#key=… ── 鍵照合 → overlay として受信のみ
   GLab ログイン視聴 ─── 従来どおり viewer チケット
```

## 責務

- **GLab が正本**: コードと鍵の生成・保持・表示 (QR)。発表セッション (`glab_odeum_sessions`) の
  寿命に合わせ、再接続しても同じ値を使う。relay には平文を渡さず presenter チケットの hash だけ渡す。
- **odeum-relay**: hash の照合と、ゲスト/overlay 接続の受け入れ。ページ (join / overlay) を
  自分で配信する (LAN のスマホと OBS が GLab に届かなくても動くように)。永続データは持たない。
- 既存のログイン視聴・チケット形式・非公開投稿の経路は変えない。

## チケットの invite claim

presenter チケットだけが任意の `invite` を持てる:
`{"join": "<base64url(SHA-256(参加コード))>", "overlay": "<base64url(SHA-256(鍵))>"}`。
どちらも 43 文字の base64url。viewer/service チケットに `invite` があれば invalid。
無ければその room のゲスト参加・overlay は無効 (従来互換)。

- 参加コード: 10 文字、Crockford base32 (`0-9A-HJKMNP-TV-Z`)。照合前に大文字化し、`O→0`, `I/L→1`、
  ハイフン・空白を除く。50 bit。
- overlay 鍵: 32 byte 乱数の base64url (43 文字)。URL の fragment (`#key=`) に載せ、HTTP ログに出さない。

## relay の受け口

| 経路 | 内容 |
|---|---|
| `GET /join` | ゲスト用ページ (コード入力 / `#code=` で自動入力、表示名入力) |
| `GET /overlay` | 透明背景の番組オーバーレイページ |
| `GET /web/<file>` | 上記ページの JS/CSS。バイナリに埋め込み、ファイルシステムを読まない |
| `GET /v1/guest?code=<c>&name=<n>` | WS upgrade。コード一致の room に **viewer (ゲスト)** として参加 |
| `GET /v1/overlay?key=<k>` | WS upgrade。鍵一致の room に **overlay** として参加 |

- ゲスト: `sub = "guest:" + 128bit 乱数 hex`、`name` は 1〜32 文字 (前後空白除去)。
  viewer と同じ制限 (リアクション流量・視聴者上限) に従う。メディア (WebRTC) には参加しない。
- overlay: 視聴者数に数えない。room あたり 4 接続まで。クライアントからの送信はすべて `forbidden`。
  受け取るのは全員向け broadcast (telop / 公開 submission / reaction.burst / poll / tally / presence) だけ。
  非公開 submission・stamp/comment の個別通知は presenter 宛てなので overlay には届かない。
- room は presenter 接続中だけ存在する (既存仕様)。presenter 不在時の照合は `session_not_live`。
  ページは指数バックオフ (1→30 秒) で再接続する。
- 照合失敗はコードを推測させないため、relay 全体で 1 分あたり 30 回を超えたら `rate_limited` で拒否する。
  成功・失敗とも本文・コード・鍵・表示名をログに出さない。
- 同じ hash を別の live room が持っていたら presenter の参加を `invite_conflict` で拒否する。

## Origin

relay 自身が配信したページからの接続は、`Origin` が `http(s)://<Host ヘッダ>` と一致し、Host が許可済みなら
許可する (same-origin)。対象はゲスト/overlay の受け口とページだけで、チケット接続の Origin 規則は変えない。
これらは cookie を使わず能力値 (コード/鍵) で認可するので CSRF の対象にならない。
LAN のスマホから届くには `ODEUM_RELAY_BIND` を LAN 側アドレスにし、LAN の IP/名前を
`LUDIARS_ALLOWED_HOSTS` に入れる (Ex 注入)。HTTP (非 TLS) は LAN 内だけで使う。

## 描画 (overlay ページ)

GLab の viewer-effects と同じ上限: telop 最大 3 件・5 秒、公開質問/感想 最大 2 件・8 秒、
グッド粒子 最大 20 件・2 秒、スタンプはバーストの種類ごと。投票は開いている間だけ集計バーを出す。
背景は透明。本文は textContent で入れる。`prefers-reduced-motion` で粒子を止める。
OBS では 1920x1080 のブラウザソースとして番組シーンの最前面に置く。
番組出力 (= 送信側への戻り映像) にもそのまま入る。

## ゲストページ

グッド (幅いっぱいの大ボタン、200ms ごとにまとめ送信) / スタンプ 5 種 / ツッコミ (telop, 60 文字) /
質問・感想 (280 文字、既定は非公開、チェックで画面表示)。GLab の composer と同じ規則:
超過は切り詰めず拒否、未接続時は入力を消さない、投稿後は非公開に戻す。
`presence.reaction_version` が 1 になるまでテキスト投稿を送らない。

## 検証

- 単体: invite claim の検証、コード正規化・照合・失敗レート、overlay の受信範囲と送信拒否、
  ゲストの viewer 扱い、視聴者数に overlay を数えないこと、invite_conflict。
- 実機 (LAN のスマホ・OBS ブラウザソース) は人間の許可範囲で別に行う。静的確認で完成と扱わない。
