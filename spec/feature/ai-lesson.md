# Odeum AI 授業 (`odeum-lesson`)

AI が進行する授業を Odeum の上で行うためのサービス。授業の著者が書いた **プロット** から、進行表 (タイムライン)・
予習シート (pptx)・授業中の問いかけ・課題の見守り・質問の共有までを一つのデータから作る。
映像と音声の配信は既存の `odeum-presenter` → `odeum-relay` が受け持ち、`odeum-lesson` は授業の進行と
生徒とのやり取り (回答・課題・質問・発話) を受け持つ。

- 置き場所: `native/lesson/` (C++20、`odeum_protocol`・`odeum_relay_state`・Boost.Beast を利用)
- サンプル教材: `native/lesson/courses/action-game.plot.json`「アクションゲームの作り方」(Ergo Studio、90 分)。
  参考実装は <https://github.com/vtn-team/ActionGameSample>。演出の実装 (ヒットストップ・ノックバック・カメラの揺れなど) を
  抜いた状態を見せ、何が足りないかを当てさせる。

## 誰が何をするか

| 立場 | 画面 | できること |
| --- | --- | --- |
| 生徒 | `GET /lesson` (参加コード + 表示名、ログイン不要) | 先生の言葉とスライドを見る、問いに答える、課題のチェック、ヒントを求める、質問する、声で話す |
| 操作者 (人間の先生) | `GET /console` (この機械の loopback のみ) | 開講、進行の開始・停止・移動、問いを出す・締める・結果を見せる、課題を出す、発言停止・退出、質問の共有 |
| 講師モデル | `LessonModel` (C++ interface) | 語り、採点、発話の読み取り、質問の評価と回答、ヒント |

**講師モデルは未着のため仮実装** (`make_provisional_model()`、名前 `provisional-rules`)。プロットに書かれた採点観点・ヒント・FAQ を
キーワードで引くだけで、プロットに無い内容は作らない。本物のモデルが届いたら `LessonModel` を実装して差し替える
(`native/lesson/core/model.hpp`)。教室の制御の流れは変わらない。

## プロットからタイムラインを起こす

プロット (JSON) は授業全体の時間、ゴール、準備、用語、FAQ と、セクションの並びを持つ。セクションは
語りの要点 (`points`)、実演の切替 (`demos`)、問い (`activity`)、課題 (`assignment`)、予習の問い (`prep`) を持てる。
道具 (Ergo Studio) と連携する教材は、実演の名前とゲームの変数の対応 (`tool.features`) と、課題の手順ごとの自動確認 (`check`) も書く
(下の「道具との連携」)。
検証に通らないプロットは `invalid_plot` (どの項目かを添える)。

`build_timeline(plot, model)`:

- セクションの長さは `minutes` の比で授業時間 (`plot.minutes`) に割り付ける。
- セクション内は「語り → 問い (出す → 締める → 結果) → 課題」の順。語りの各行は文字数の比で時間を取り、
  少なくともセクションの 2 割は語りに残す。行ごとの持ち時間 (`seconds`) をモデルに渡す。
- 問いは回答時間 (`seconds`) の後に結果の説明 45 秒を取る。課題は 60 秒ごとに進み具合の確認を入れる。
- `share_questions: true` のセクションは終わりに良い質問の共有を入れる。

`TimelinePlayer` は外から渡された単調時計で進む (開始・一時停止・再開・位置指定・次のセクションへ)。
CLI: `odeum-lesson timeline <コース id | plot.json>` でタイムラインを JSON で出す。

## 予習シート (pptx)

`build_prep_sheet` → `write_pptx`。表紙・今日のゴール・準備・キーワード・予習の問い (セクションごと)・授業の流れ。
授業の流れの分数はタイムラインから取る。**問いの答え (模範解答・採点観点) は載せない。**
pptx は OOXML を直接書き、ZIP は無圧縮。時刻を埋め込まないので同じプロットからは同じバイト列になる。
取得: `GET /v1/prep.pptx?plot=<id>` または `odeum-lesson prep-sheet <コース id | plot.json> <出力.pptx>`。

## 授業中の問い (ClassPoint 風)

問いは生徒の端末に出て、結果を見せるまで他人の回答は見えない。後から出した回答が前の回答を置き換える。

| 種類 | 内容 | 採点 |
| --- | --- | --- |
| `choice` | 選択肢 (単一 / 複数)。`correct` が空ならアンケート | 選んだ組が正解と一致 |
| `text` | 短い記述 | 採点観点のうち `min_points` 個以上に触れている |
| `missing` | 実装を抜いたコードを見せ、足りないものを挙げる | 同上 (観点 = 抜いた実装) |
| `open` | 正解のない問い (観点は意見の分類にだけ使う) | 判定しない。模範解答も持てない |

結果を見せるとき、全員にはクラスの集計 (選択肢ごとの数 / 観点ごとの数)・模範解答・名前を伏せた代表的な回答を、
各生徒には自分の回答への一言を個別に送る。サンプル教材の主な問い:
「ヒットストップをどう設計するか」(`text`)、「ノックバックで何が起きるか」(`text`)、
「カメラはどうするのが良いか」(`open`、正解なし)。

## 課題の見守りと個別対応

課題は手順 (ステップ) の列。進み具合は生徒のチェック (`progress`)、生徒の機械の道具からの報告 (`POST /v1/tool/progress`、
下の「道具との連携」)、操作者の代理チェック (操作卓の `progress.report`) のどれかで入る。どの経路でも生徒のページに反映する。

- 出して 90 秒手を付けない → 最初の一歩を個別に促す。
- `stall_seconds` 進まない / 「ヒントがほしい」/ 声で助けを求めた → その生徒のいるステップのヒントを個別に出す。
  ヒントは段階的で、出し切ったら操作者に「直接見る」を通知し、自動のヒントは止める。
- 同じステップで 3 人以上かつ 4 割以上が止まった → 個別ではなく全員に一度だけ話す。
- 全部終えた → 褒めて、発展課題 (`extra`) を示す。

## 道具との連携 (Ergo Studio)

生徒や実演者の機械で動く道具 (Ergo Studio の「授業」プラグイン。Ergo 側の仕様は Ergo `spec/feature/tool/lesson.md`) が、
授業と HTTP でやり取りする。道具は HTTP しか持たなくてよい (WebSocket は使わない)。

**つなぎ方。** 生徒はページの「接続文字列を出す」、操作者は操作卓の「実演用の道具をつなぐ」を押す。10 文字の接続コード
(120 秒有効・1 回だけ・出し直すと前のコードは無効) が出るので、`<授業の origin>/v1/tool#<コード>` の形で道具に貼り付ける。
道具はコードをトークンに引き換える。トークンは「その授業のその生徒 (または実演者)」だけを表し、1 人につき 2 台まで
(3 台目は最も古いものと入れ替わる)。退出・授業を閉じる・プロセス終了で無効になる。コードもトークンも保存せず、ログにも出さない。
間違ったコードはサービス全体で 1 分 30 回まで。

| 経路 (生徒側の待受) | 内容 |
| --- | --- |
| `POST /v1/tool/pair` `{link, tool}` | → `{token, role: student \| presenter, class: {title}, poll_ms}`。`link_not_found` (404) / `rate_limited` (429) |
| `GET /v1/tool/state` (`Authorization: Bearer`) | → `{role, class, features, assignment, demo}`。無効なトークンは `tool_unlinked` (401) |
| `POST /v1/tool/progress` `{assignment_id, step, done: true}` | 生徒の道具だけ。`not_assigned` (409) / `forbidden_role` (403)。`done: false` は受け付けない |

- POST は `Content-Type: application/json`、本文 4 KiB まで。Host / Origin の検査は他の経路と同じ (別サイトからは呼べない)。
- `features`: 実演の名前 → ゲームの bool 変数名 (プロットの `tool.features`)。
- `assignment` (生徒): 出ている課題の手順と、その生徒の済み・未済み。`check` (`{variable, op, value}`、`op` は
  `gt` `ge` `lt` `le` `eq` `ne`) の付いた手順は、道具がゲームの変数を見て満たしたときに `progress` を送る。一度付けたチェックを道具は外さない。
- `demo` (実演者): `{seq, states}`。`states` はタイムラインがここまでに切り替えた実演の ON / OFF、`seq` は切替のたびに増える。
  `feature: "all"` は `tool.features` の全部を切り替える。位置の移動 (seek / 次のセクションへ) をしたときは、飛ばした切替も
  反映した状態に作り直す。道具は `states` に合わせて変数を設定する。
- 操作卓の生徒一覧には道具をつないだ生徒に「道具」が付き、実演用の道具の接続有無も出る。

サンプル教材は `feel.hit_stop` / `feel.knockback` / `feel.camera_shake` (実演) と、`feel.hit_stop_last_seconds > 0` /
`feel.knockback_last_impulse > 0` (課題の自動確認) を使う。**これらの変数を公開する Ergo 版のサンプルゲームはまだ無い。**
変数名は Ergo 版サンプルを作るときに合わせる前提の取り決めで、実機では確かめていない。

## 生徒の声の聞き分け

- **一人一台**: 声は生徒ごとの接続で届くので、誰の声かは接続で決まる。端末は音量 (0–100) だけを送り、
  サーバが「話し中」を判定する (700ms 以内の間は同じ発話)。複数人が同時に話しても、それぞれ別に扱う。
- **発話の中身**: 既定では生徒が文字で送る。`ODEUM_LESSON_BROWSER_STT=1` のときだけブラウザの音声認識を使う
  (ブラウザによっては音声がブラウザ提供元へ送られるため、既定は無効)。サーバ側の音声認識はモデル到着後。
- **教室にマイクが 1 本**: 外部の話者分離が付けた話者ラベルを `voice.diarized` で受ける。生徒は自分の端末に出た
  3 桁の番号を読み上げて、自分のラベルを登録する (60 秒有効)。未登録のラベルの声は操作者にだけ見せる。
- 発話はモデルが読み取り、質問 → 個別に回答、助けて → ヒント、問いが開いていれば回答として受け取る、
  ふざけ → 素行の記録、に振り分ける。読み取り結果は操作卓に出す。

## ふざけた生徒の発言停止と退出

| 出来事 | 点 |
| --- | --- |
| 中身のない連投 (同じ文字 6 連続、記号だけ) | 1 |
| 暴言 | 2 |
| 10 秒に 9 件以上の投稿 | 1 (連投 1 回につき) |
| 授業と関係ない発言が 1 分に 4 回 | 1 |

点は 10 分で消える。初回は注意、3 点で発言停止 (60 秒、次は倍、最長 15 分)、6 点または 3 回目の発言停止で退出。
発言停止中は授業を見ること・課題・ヒントの要求はできるが、回答・質問・発話はできず、「話し中」にも出ない。
退出は接続を切り、その授業が終わるまで戻れない。同時に入室を締め切る (別名での入り直しを防ぐ。操作者が解除できる)。
操作者は手動で発言停止・解除・退出・復帰ができる。黙っている生徒が罰を受けることはない。

## 良い質問の共有

質問には必ず本人だけに答えを返す。似た質問 (文字 2-gram の一致 0.6 以上) は 1 件にまとめ「私も知りたい」に数える。
モデルの評価 70 点以上で、答えを用意できた質問だけが自動共有の対象 (1 回に 2 件まで)。
共有は名前を付けない (本人が選んだときだけ名前を出す)。答えを用意できない質問は操作者に通知する。
操作者はどの質問も手動で共有・見送りにできる。

## API

生徒側 (`ODEUM_LESSON_BIND`:`ODEUM_LESSON_PORT`):

- `GET /health` → `{ok:true,version}`
- `GET /lesson`, `GET /web/*` — 埋め込みページ
- `GET /v1/prep.pptx?plot=<id>` — 予習シート
- `POST /v1/tool/pair`, `GET /v1/tool/state`, `POST /v1/tool/progress` — 道具 (上の「道具との連携」)
- `GET /v1/join?code=&name=[&resume=]` — WebSocket。拒否理由は upgrade 後に `error` で返す
  (`class_not_found` / `rate_limited` / `invalid_name` / `entry_locked` / `ejected` / `capacity`)

操作卓 (`127.0.0.1`:`ODEUM_LESSON_CONSOLE_PORT` 固定):

- `GET /console`, `GET /web/*`, `GET /v1/prep.pptx`, `GET /health`
- `GET /v1/console?token=` — WebSocket

メッセージは JSON、受信は 16 KiB まで、毎秒 120 件を超えたら切断。種類と項目は
`native/lesson/core/classroom.cpp` (受信) と `classroom_cues.cpp` / `classroom_views.cpp` (送信) が正本。

## 操作卓の保護

操作卓は開講・発言停止・退出を行えるので、この機械を直接操作している人だけが使える。

1. 待受は `127.0.0.1` 固定 (設定で変えられない)。
2. `Forwarded` / `X-Forwarded-*` / `X-Real-IP` / `CF-Connecting-IP` の付いた要求は拒否 (転送経路は接続元が loopback に見えるため)。
3. `Host` は `127.0.0.1` / `localhost` / `[::1]` + 操作卓のポート、`Origin` は同じ origin、`Sec-Fetch-Site` が cross-site / same-site なら拒否。
4. WebSocket は起動のたびに作り直すトークンが要る。トークンは `ODEUM_LESSON_CONSOLE_TOKEN_FILE` に所有者だけが読める権限で書く
   (Windows はファイルを置くフォルダの ACL に従う)。ページは `/console#token=<値>` で開き、読み取った後に URL から消す。
5. 操作は種類・授業 id・生徒 id・結果だけを標準エラーに記録する (名前と本文は記録しない)。

**転送経路 (Cloudflare Tunnel 等) の転送先に操作卓のポートを登録しないこと。**

## 設定

| 環境変数 | 既定・形式 |
| --- | --- |
| ODEUM_LESSON_CONSOLE_TOKEN_FILE | 必須。操作卓トークンを書くファイル |
| ODEUM_LESSON_BIND | 127.0.0.1、IP literal |
| ODEUM_LESSON_PORT | 4410、1–65535 |
| ODEUM_LESSON_CONSOLE_PORT | 4411、1–65535、`ODEUM_LESSON_PORT` と別 |
| ODEUM_LESSON_MAX_STUDENTS | 60、1–300 (1 授業あたり) |
| ODEUM_LESSON_PLOT | 任意。追加するプロットの JSON ファイル |
| ODEUM_LESSON_BROWSER_STT | 0。1 でブラウザの音声認識を使う |
| ODEUM_LESSON_PUBLIC_URL / ODEUM_LESSON_ALLOWED_ORIGINS / LUDIARS_ALLOWED_HOSTS | relay の同名設定と同じ規則 (`WebAccess`) |

明示された空値・不正値は起動失敗。同時に開ける授業は 8 件。参加コードは Crockford base32 の 10 文字で、
間違ったコードはサービス全体で 1 分 30 回まで。

## 個人データ

表示名・回答・質問・発話は授業が動いている間だけメモリに持ち、ファイルにも DB にも書かない。授業を閉じるかプロセスを
終えると消える。音声そのものはサーバに送らない (音量の数値だけ)。ログに名前と本文を出さない。

## ビルドと検証

`cmake -S native -B native/build && cmake --build native/build` で `native/build/bin/odeum-lesson`。
`ODEUM_BUILD_LESSON=OFF` で外せる。テストは `ctest --test-dir native/build -R lesson` (`native/tests/lesson_*_tests.cpp`)。

確認済み (2026-11-11、Linux / GCC 13): 単体テスト 12 本、道具の HTTP 経路 (接続コードの発行と引き換え、状態の取得、進み具合の報告、
拒否の各種、退出・閉講での失効) とページの表示、CLI での pptx 生成 (python-pptx で 12 枚として開ける、
LibreOffice で描画)、ブラウザでの通し (操作卓 1 + 生徒 2: 開講 → 問い → 結果 → 課題とヒント → 質問共有 → 発言停止 → 退出 → 入り直し拒否)。

## 未実装・未確認

- 講師モデル本体 (語りの肉付け、採点、音声認識、音声合成、話者分離)。いまは仮のルール。
- Ergo Studio との実接続。授業側の経路と Ergo Studio 側のプラグインはそれぞれ単体で確かめたが、両者をつないだ確認はしていない
  (Ergo Studio の HTTP クライアントが Windows のみのため)。変数を公開する Ergo 版のサンプルゲームも未作成。
- 生徒ページへの配信映像の埋め込み (映像は relay の視聴ページで見る)。
- GLab / Cernere のチケットによる本人確認 (いまは参加コード + 表示名。退出の強制力は入室締め切りに頼る)。
- Windows / macOS でのビルド、PowerPoint 実機での表示、マイク入力の実機確認。
- ポート 4410 / 4411 は仮の値 (Excubitor の割当と未照合)。
