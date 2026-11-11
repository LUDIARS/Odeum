# 授業に「出席」するためのハーネス (`tools/attend/`)

`odeum-lesson` のサンプル授業に、AI エージェントが **生徒の 1 席** として出席し、その様子を動画に残すための道具。
操作者 (先生) の側は台本で動かし、90 分の授業を十数分に縮めて進める。エージェントは `seat.cjs` だけを使って
画面を読み、問いに答え、課題を進め、質問する。すべて本物のページ (生徒ページと操作卓) の要素を読む・押すことで行う。

| ファイル | 役目 |
| --- | --- |
| `classroom.cjs` | セッション本体 (長く動く)。サービス起動、操作卓と席の録画、縮めた台本の実行、制御 API、終了時の動画合成 |
| `seat.cjs` | 出席者の CLI。1 回の呼び出しで 1 操作、JSON を標準出力に返して終わる |
| `smoke.cjs` | ハーネスの通し確認 (台本どおりに動く簡単な出席者。AI ではない) |
| `lib/` | 1 ファイル 1 役目: `service` (起動)、`schedule` (台本の導出)、`operator` (操作卓の操作)、`runner` (台本の実行)、`seat-dom` (画面の読み取り)、`seat-actions` (席の操作)、`observation` (observe の形)、`watcher` (画面の変化の追跡)、`classmates` (級友)、`control-server` (制御 API)、`event-log` (JSONL)、`video` (合成と確認) |

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
  "demo": "画面で実演: ヒットストップだけ ON にして当てる (ON)",
  "activity": {
    "id": "hitstop-design", "kind": "text", "prompt": "ヒットストップをどう設計しますか。…", "code": null,
    "input": "text", "choices": null, "state": "open", "state_note": "受け取りました。締め切りまで変更できます。",
    "results": null, "personal_feedback": null, "my_answer": "…", "my_answer_acked": true
  },
  "assignment": null,
  "private_new": [{ "seq": 1, "kind": "質問の答え", "text": "決まった正解はありません。…" }],
  "private_count": 1,
  "shared_questions": [],
  "conduct": { "standing": "good", "text": null },
  "toast": null,
  "video_seconds": 212.4
}
```

- `lesson_state`: `waiting` (開始前) / `running` / `ended` (授業の終わり) / `closed` (授業が閉じられた) / `not_joined`。
- `activity.state`: `open` (受付中) / `closed` (締め切り) / `revealed` (結果表示中)。`results` は棒 (`label`・`count`・`correct`)、模範解答、
  代表的な回答。`personal_feedback` は自分への一言。`choices[].selected` はいま画面で選ばれているもの。
- `assignment.steps[]`: `n` (1 始まり)・`label`・`done`・`auto` (道具の自動確認付き)。`active` は「ヒントがほしい」が押せるか。
- `private_new`: 「先生からあなたへ」に届いた分のうち、まだ返していないもの (ヒント・質問の答え・先生から)。`observe --all` で `private_all` に全部。
- `wait --for change`: 前回の observe 以降に画面 (セクション・語り・スライド・実演・問いの状態・課題・個別メッセージ・共有質問・素行) が
  変わったらすぐ、変わらなければ `--timeout` 秒で返る。`changed` が付く。自分の入力中の文字は変化に数えない。
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
セクションごとに作り、操作卓の本物のボタンだけで進める (サーバに早送りの機能は足していない):

1. 最初のセクションは「開始」、以降は「次のセクションへ」(タイムラインがそのセクションの頭に飛び、題・スライド・最初の語りが出る)。
2. 語りを `--narration-seconds 8` 秒見せる (問いも課題も無いセクションは `--section-seconds 15`)。
3. 問いがあれば「出す」→ `--activity-seconds 60` 秒 (問いごとに `--windows hitstop-design=90,camera-open=90`) →「締め切る」→ 2 秒 →
   「結果を見せる」→ `--reveal-seconds 12` 秒 (クラスの結果と自分への一言)。
4. 課題があれば「出す」→ `--assignment-seconds 120` 秒 →「終える」。
5. 良い質問の共有があるセクションは `--share-after 20` 秒後に「良い質問を共有する」。
6. 最後のセクションは、タイムライン自身の終わり (授業の終わり) まで待つ。操作卓に終わりへ飛ぶ操作が無いため、
   サンプルでは最後の「まとめ」3 分は実時間になる (語り 3 行と自動の質問共有がその間に出る)。

タイムラインはセクションの中で、語りの後に問いや課題を自分で出す。台本はその前に手で出して次へ進むので、
起動時に「各セクションの手順の合計 + 10 秒 < そのセクションでタイムラインが最初に自分で問い・課題・共有を出す時刻」を確かめ、
収まらなければ起動しない (どのセクションかを出す)。実行中も操作の前に操作卓の時計で同じことを確かめる。
既定値でのサンプルの所要はおよそ 13〜14 分 (6 問 × 約 82 秒 + 課題 約 2.5 分 + 導入 + まとめ 3 分)。
セクションに飛んだ直後は最初の語りだけが出る (2 行目以降は実時間で数分後のため、縮めた台本では多くの場合出ない)。スライドには要点が全部出る。

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
終わったら止める。`$W/smoke-report.json` に確認結果 (`checks`) を書く。

## 限界

- 出席者の席の動画は 1 席だけ。級友の席は録らない。音声は無い (読み上げは使わない)。
- 縮めた台本では、2 行目以降の語りと、課題中の定期確認 (60 秒ごと) や「止まっている」自動ヒント (150 秒) の多くが出る前に次へ進む。
- 課題の「自動」手順も手でチェックする (Ergo Studio をつながない)。道具の接続、マイク、声の登録は扱わない。
- `tick` はページが確かめの表示を出さないため、1.5 秒以内にエラー表示が出なければ成功とみなす。
- 共有された質問には id が画面に出ないので、`vote` は observe の番号 `n` で指す。
- 講師は仮のルール (`provisional-rules`) なので、採点と答えはキーワード一致による。
