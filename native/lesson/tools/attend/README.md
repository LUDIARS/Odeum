# 授業に「出席」するためのハーネス (`tools/attend/`)

`odeum-lesson` のサンプル授業に、AI エージェントが **生徒の 1 席** として出席し、その様子を動画に残すための道具。
操作者 (先生) の側は台本で動かし、90 分の授業を十数分に縮めて進める。エージェントは `seat.cjs` だけを使って
画面を読み、問いに答え、課題を進め、質問する。すべて本物のページ (生徒ページと操作卓) の要素を読む・押すことで行う。

| ファイル | 役目 |
| --- | --- |
| `classroom.cjs` | セッション本体 (長く動く)。サービス起動、操作卓と席の録画、縮めた台本の実行、制御 API、終了時の動画合成 |
| `seat.cjs` | 出席者の CLI。1 回の呼び出しで 1 操作、JSON を標準出力に返して終わる |
| `smoke.cjs` | ハーネスの通し確認 (台本どおりに動く簡単な出席者。AI ではない) |
| `teacher-answers.action-game.json` | サンプル授業で「人間の先生」役が答える内容 (`--teacher-answers`)。教材に照らして正しいこと、または一般的なことだけ |
| `lib/` | 1 ファイル 1 役目: `service` (起動)、`schedule` (台本の導出)、`operator` (操作卓の操作)、`teacher` (台本の先生の回答)、`runner` (台本の実行)、`seat-dom` (画面の読み取り)、`seat-actions` (席の操作)、`observation` (observe の形)、`watcher` (画面の変化の追跡)、`classmates` (級友)、`control-server` (制御 API)、`event-log` (JSONL)、`video` (合成と確認) |

要るもの: ビルド済みの `native/build/bin/odeum-lesson`、Node と Playwright (Chromium)、PATH 上の `ffmpeg` / `ffprobe` (libx264)。
ポート 4410 / 4411 が空いていること (使われていれば起動を止める)。

## セッションを始める

```sh
export NO_PROXY=127.0.0.1,localhost
W=/path/to/work                         # 作業フォルダ (動画・ログ・トークン・ソケットが入る)
node native/lesson/tools/attend/classroom.cjs --work $W --students agent,りく,はな
```

- `--students` の先頭が出席者の席 (表示名)、残りは台本で動く級友。級友は問いが出て 10 秒 / 17 秒後に決まった答えを送り、
  課題を 20 秒ごとに 1 手順ずつ進め、1 人目はヒントを 1 回求め、決まったセクションで 1 問ずつ質問し、最後のセクションで「私も知りたい」を押す。
- 起動すると操作卓で開講し、全員が入室したところで **出席者の最初の `seat.cjs` 呼び出しを待ってから** 授業を始める
  (`--no-wait-attendee` で待たない。待つ上限は `--attendee-timeout 600` 秒)。
- 授業が終わると `--linger 20` 秒後に自動で止まる (`0` なら `seat.cjs stop` か Ctrl-C まで待つ)。止めるとページを閉じて動画を確定し、
  1 本に合成して、結果の JSON (動画のパス・長さ・解像度・切り出した画像) を表示する。
- 台本の途中で操作卓の操作が効かない・要素が無い・操作卓がエラーを出したときは、その手順の名前を出して止まる (動画は作る)。
- `--teacher-answers <file.json>` で「人間の先生」役を付ける。操作卓に「先生の回答待ち」の質問が出ると、`--teacher-delay 12` 秒後に、
  質問の文にファイルの `all` の語が全部 (`any` があればそのどれかも) 含まれる最初の答えを、操作卓の回答欄に打って「答える」を押す
  (動画に映る)。どれにも当たらない質問は答えずに残す (忙しい先生と同じ)。「見えない・聞こえない」の知らせ (操作卓上部の
  「対応が要る知らせ」) は、同じ遅れの後に「確認した」を押す (`teacher.ack_report` を記録)。「まだ分からない」で回答待ちに戻った質問には、
  まだ使っていない次の答えがあればそれを、無ければ残す。`after` (問いの id) の付いた答えは、その問いの答えを明かしてしまうので、
  台本がその問いの結果を見せた後にだけ使う (それまでは回答待ちのまま残し、`teacher.held` を記録する)。形式はファイルの先頭のコメントと `lib/teacher.cjs`。

## 出席する (`seat.cjs`)

```sh
export ATTEND_WORK=$W
node native/lesson/tools/attend/seat.cjs observe            # いま画面に出ていること
node native/lesson/tools/attend/seat.cjs wait --for change --timeout 30
node native/lesson/tools/attend/seat.cjs answer --text "当たった瞬間に一瞬止まる。相手が押し返される"
node native/lesson/tools/attend/seat.cjs answer --choices 1,2      # 選択式 (0 始まり)。1 つだけ選ぶ問いは 1 個
node native/lesson/tools/attend/seat.cjs tick --step 1 [--undo]    # 課題の手順 (ページの番号どおり 1 始まり)
node native/lesson/tools/attend/seat.cjs help                      # 「ヒントがほしい」
node native/lesson/tools/attend/seat.cjs ask --text "..." [--show-name]
node native/lesson/tools/attend/seat.cjs say --text "..."          # 「先生に話す」の文字入力
node native/lesson/tools/attend/seat.cjs vote --n 1                # 共有された質問に「私も知りたい」 (n は observe の shared_questions[].n)
node native/lesson/tools/attend/seat.cjs tool-link                 # 「接続文字列を出す」を押し、道具に渡す接続文字列を返す ("link")
node native/lesson/tools/attend/seat.cjs unresolved --n 1          # 自分の質問 (observe の my_questions[].n) に「まだ分からない」
node native/lesson/tools/attend/seat.cjs status                    # 台本の位置 (次に先生が何をするか、あと何秒か)
node native/lesson/tools/attend/seat.cjs stop
```

終了コード: 0 = 成功、1 = ページかハーネスが断った (`error` を見る)、2 = セッションが無い・使い方の誤り。

`observe` (と `wait`、各操作の返す `observe`) の主な項目。すべて出席者のページの DOM から読む (動画に映っているものと同じ):

```json
{
  "ok": true,
  "lesson_state": "running",
  "section": { "index": 4, "total": 9, "title": "ヒットストップをどう設計するか" },
  "slide": { "title": "ヒットストップをどう設計するか", "points": ["ヒットストップだけを戻します。…", "…"] },
  "narration": "ヒットストップだけを戻します。違いを見てください。当たった瞬間に、ほんの少し止まります。",
  "demo": "先生の画面 (配信・教室のスクリーン) で実演中: Ergo のサンプルでヒットストップだけ ON にする (ON)。 見るところ: 2 秒ごとのログで world= の進みが約 1.7 秒に縮む (当たるたびに 0.08 秒止まる)",
  "activity": {
    "id": "hitstop-design", "kind": "text", "prompt": "ヒットストップをどう設計しますか。…", "code": null,
    "input": "text", "choices": null, "state": "open", "state_note": "受け取りました。締め切りまで変更できます。",
    "answer_on_page": "…", "results": null, "personal_feedback": null, "my_answer": "…", "my_answer_acked": true
  },
  "assignment": null,
  "private_new": [{ "seq": 1, "kind": "質問の答え", "text": "決まった正解はありません。…" }],
  "private_count": 1,
  "shared_questions": [],
  "my_questions": [{ "n": 1, "question": "…", "state": "先生の回答待ち", "answer": null, "detail": [], "can_unresolve": false }],
  "my_reports": [{ "n": 1, "text": "デモの画面が小さくて…よく見えませんでした。", "state": "先生が確認しました", "reply": null }],
  "said": { "taken_as": "remark", "text": "「なるほど…」→ 先生に伝えました。問いへの回答はすでに受け取っているので、…" },
  "conduct": { "standing": "good", "text": null },
  "toast": null,
  "private_notice": "先生からあなたへ 新着 1 件: …",
  "tool": { "state": "", "link": null, "linked": null },
  "video_seconds": 212.4
}
```

- `lesson_state`: `waiting` (開始前) / `running` / `ended` (授業の終わり) / `closed` (授業が閉じられた) / `not_joined`。
- `activity.state`: `open` (受付中) / `closed` (締め切り) / `revealed` (結果表示中)。`results` は棒 (`label`・`count`・`correct`)、模範解答、
  解説 (`explanation`、選択式など作者が書いた問いだけ)、代表的な回答。`personal_feedback` は自分への一言 (「あなたの回答「…」について: …」)。
  `choices[].selected` はいま画面で選ばれているもの。
- `activity.answer_on_page`: ページが「いまのあなたの回答」として出している文 (サーバが持っている回答)。`my_answer` はハーネスが
  `answer` で送ったもの。`say` が回答として受け取られた (まだ答えていなかった) ときは前者だけが変わる。答えた後の `say` は回答を
  置き換えない (ページは「置き換えていません」と出す)。
- `assignment.steps[]`: `n` (1 始まり)・`label`・`done`・`auto` (道具の自動確認付き)・`watches` (自動の手順が見る変数と条件の文、
  例 `自動: 道具が feel.hit_stop_last_seconds > 0 を見たらチェック`)。`active` は「ヒントがほしい」が押せるか。課題が終わると
  チェック欄は押せず、次のセクションに入ると課題の欄は消える。
- `private_new`: 「先生からあなたへ」に届いた分のうち、まだ `observe` / `wait` が返していないもの (ヒント・質問の答え・先生の答え・
  先生から・答えられなかった質問)。既読にするのは `observe` と `wait` だけで、操作の返事の `observe` にも同じものが出るが既読にはしない。
  だから個別メッセージは必ず 1 回、`observe` か `wait` から新着として返る。`observe --all` で `private_all` に全部。
  `private_notice` は一覧が画面に見えていないときに画面下に出る新着の知らせ (見えていれば `null`)。
- `shared_questions[]`: `mine` (自分の質問。「私も知りたい」は出ない)、`can_vote`、`votes` (サーバが送った数)。
- `tool`: 道具をつなぐ欄 (道具と連携する授業だけ)。`link` は出ている接続文字列、`linked` は道具が接続文字列を受け取ったあとに
  ページが出す「道具がつながりました: <道具の名前>…」(つながっていなければ `null`)。
- `my_questions[]`: 自分の質問 (と「私も知りたい」を押した質問) とその状態 (`答えあり (用意した答え)` / `先生の回答待ち` /
  `先生から回答` / `見送り`)。`can_unresolve` は「まだ分からない」が押せるか (自分の質問で答えがあるとき)。
- `my_reports[]`: 「見えない・聞こえない・ついていけない」と受け取られた自分の知らせとその状態 (`知らせました (先生の確認待ち)` /
  `先生が確認しました` / `先生から返事`、返事は `reply`)。誰も確認しないまま授業が終わると「確認されなかった知らせ」が個別に届く。
- `said`: 「先生に話す」で最後に送った言葉がどう受け取られたか (`taken_as`: `answer` / `remark` / `question` / `help` /
  `trouble` (見えない・聞こえない・ついていけない) / `chatter` / `disruption`、`text` はページの表示)。
- `wait --for change`: 前回の observe 以降に画面 (セクション・語り・スライド・実演・問いの状態・課題・個別メッセージ・共有質問・素行) が
  変わったらすぐ、変わらなければ `--timeout` 秒で返る。`changed` が付く。自分の入力中の文字は変化に数えない。
- `say` の返事は `taken_as` (上と同じ) と `page_message` (「先生に話す」の下に出た文)。
- 操作は返事に `ok` と、ページが出した文 (`page_message`) とエラー (`error.source` が `page` ならページの表示そのもの、例:
  「この質問は締め切られました。」「いまは発言できません。課題は続けられます。」) と、操作後の `observe` を付ける。
  締め切り後に答えようとすると `{"ok": false, "error": {"source": "harness", "code": "activity_closed", "text": "締め切りました。"}}`
  (ボタンが押せない状態の画面の文)。

`status` の例:

```json
{ "ok": true, "schedule": { "state": "running",
    "step": { "n": 14, "of": 60, "kind": "hold", "label": "回答受付: hitstop-design", "elapsed_seconds": 21, "left_seconds": 39 },
    "upcoming": [ { "op": "activity.close", "label": "締め切る: hitstop-design", "in_seconds": 39 },
                  { "op": "activity.reveal", "label": "結果を見せる: hitstop-design", "in_seconds": 41 } ] },
  "estimate_seconds": 805, "lesson_clock": "28:21 / 90:00", "waiting_for_attendee": false }
```

## 縮めた台本の作り方

サービスはタイムラインを実時間で再生する (サンプルは 90 分)。台本は `odeum-lesson timeline <コース>` の出力から
セクションごとに作り、操作卓の本物の操作だけで進める (サーバに早送りの機能は足していない):

1. 最初のセクションは「開始」、以降は「次のセクションへ」(タイムラインがそのセクションの頭に飛び、題・スライド・最初の語りが出る)。
   セクションの頭に実演の合図があれば、その実演の行が出席者のページに出るのを確かめる。
2. 語りを `--narration-seconds 8` 秒見せる。
3. セクションの中のタイムラインの合図を時刻の順に、操作卓の「合図」から選んで「この合図まで進める」で呼ぶ (操作卓がその時刻へ
   移り、合図はタイムライン自身が出す):
   - 実演 → 出席者のページに「先生の画面…で実演中: …」が出るのを待ち (10 秒まで。出なければ止まる)、`--demo-seconds 8` 秒見せる。
   - 問い → タイムラインが問いを出す → `--activity-seconds 60` 秒 (問いごとに `--windows hitstop-design=150,camera-open=150`) →
     「締め切る」→ 2 秒 →「結果を見せる」→ `--reveal-seconds 12` 秒。
   - 課題 → タイムラインが課題を出す → `--assignment-seconds 240` 秒 →「終える」(60 秒ごとの進み具合の確認と、
     150 秒止まった人への自動ヒントはこの間にタイムライン自身が出す)。
   - 良い質問の共有 → `--share-after 20` 秒 →「良い質問を共有する」→ `--share-hold 20` 秒。
4. 何も無いセクションは `--section-seconds 15` 秒。最後のセクションは「授業の終わり」の合図まで進めて終わる。

問いと課題はタイムライン自身が出すので、窓の長さの上限は授業自身の長さ (合図から、タイムラインが自分で締め切るまで) から
余裕 5 秒を引いたもの。上限は `classroom.cjs --help` の最後と `seat.cjs status` の `schedule.limits` に出る。サンプルでは
問いが 70〜175 秒、課題が 619 秒 (授業の 12 分から語りの分を引いた残り)。上限を超える指定は起動時に断る (どれの上限かを出す)。
実行中も「締め切る」「終える」の前に操作卓の時計で、窓が上限の中にあることを確かめる。
既定値でのサンプルの所要はおよそ 15〜16 分 (6 問 × 約 90 秒 + 課題 4 分 + 実演 3 回 + 共有)。
セクションに入った直後は最初の語りだけが出る (2 行目以降は実時間で数分後のため、縮めた台本では多くの場合出ない)。スライドには要点が全部出る。

## 出力 (作業フォルダ)

| パス | 中身 |
| --- | --- |
| `session.mp4` | 合成した動画 (H.264)。左が出席者の席 (480×900)、右が操作卓 (1360×900)。時刻はそろえてある |
| `video/attendee/*.webm`, `video/console/*.webm` | Playwright が録った元の動画 (VP8) |
| `frames/frame-*.png` | 確認用に動画から切り出した 4 枚 |
| `attendee.jsonl` | 1 行 1 件。`v` は動画の中の秒。`shown` (出席者の画面に出た変化)、`action` (出席者の操作と結果)、`operator` (台本の操作と操作卓の表示)、`classmate`、`error`、`session` |
| `session.json` | 状態と、終了後は結果 (動画の長さ・解像度・バイト数・画像のパス) |
| `odeum-lesson.stderr.log` | サービスの標準エラー (名前や本文は出さない) |
| `console.token`, `seat.sock` | 操作卓トークン (サービスが書く) と制御 API のソケット (所有者のみ) |

これらは試験の成果物で、生徒役の表示名・回答・質問を含む。**リポジトリに入れない。** サービス自身は従来どおり何もディスクに書かない。

## 通し確認

```sh
NO_PROXY=127.0.0.1,localhost node native/lesson/tools/attend/smoke.cjs --work $W
```

`classroom.cjs` を起動し、`seat.cjs` だけで全部の問いに答え、課題を全部チェックし、ヒントを求め、質問し、話し、投票して、
接続文字列を出し、記述の問いに答えた後に話しても回答が置き換わらないこと、「デモがよく見えなかった」が質問ではなく知らせとして
受け取られること、導入の実演の行が見えたこと、個別メッセージが `observe` / `wait` から 1 回ずつ返ることを確かめ、終わったら止める。
`--teacher-answers native/lesson/tools/attend/teacher-answers.action-game.json` を付けると、先生役の回答が出席者に届くこと、
「まだ分からない」で回答待ちに戻って先生が答えること、誰も答えない質問が授業の終わりに「答えられなかった」と伝わること、
「よく見えなかった」の知らせが「あなたの知らせ」に出て先生が確認すると「先生が確認しました」に変わることも確かめる
(付けないとこの 4 つは失敗になる)。`$W/smoke-report.json` に確認結果 (`checks`) を書く。

## 限界

- 出席者の席の動画は 1 席だけ。級友の席は録らない。音声は無い (読み上げは使わない)。
- 縮めた台本では、2 行目以降の語りの多くが出る前に次へ進む (合図まで進めると、その間の語りは飛ばす)。
- 課題の「自動」手順も、道具をつながなければ手でチェックする。ハーネスは接続文字列を出すところまでで、Ergo Studio やサンプルは
  動かさない。マイクと声の登録は扱わない。
- `tick` はページが確かめの表示を出さないため、1.5 秒以内にエラー表示が出なければ成功とみなす。サーバが断ったときはページが
  チェックを元に戻し、その文 (例「いまは課題が出ていません。」) がエラーとして返る。
- `say` の返事の `taken_as_answer: true` は `taken_as: "answer"` と同じ (話した言葉が開いている問いへの回答として受け取られた)。
- `tool-link` の接続文字列は 120 秒・1 回だけ有効。返事には入るが `attendee.jsonl` には書かない (画面には出るので動画には映る)。
  道具 (Ergo Studio の「授業」タブ、窓の無い Studio なら `POST /lesson/api/connect` に `{"link": "…"}`) に渡すのは出席者の役目で、
  ハーネスは Ergo を起動しない。
- 共有された質問には id が画面に出ないので、`vote` は observe の番号 `n` で指す。
- 講師は仮のルール (`provisional-rules`) なので、採点と答えはキーワード一致による。
