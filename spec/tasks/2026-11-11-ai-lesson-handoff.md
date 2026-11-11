---
task: ai-lesson-handoff
project: Od
kind: handoff
created: 2026-11-11
memory_links:
  - spec/feature/ai-lesson.md
  - spec/domains/ai-lesson.domain.json
---

# Odeum AI 授業 (`odeum-lesson`) 引き継ぎ

作業を途中で切り上げ、別の担当へ渡すための指示書。仕様の正本は `spec/feature/ai-lesson.md`。ここには
「どこまで終わったか」「次に何をするか」「判断待ちの事項」「再現手順」だけを書く。

## 依頼 (人間の指示の要約)

Odeum をベースに AI 授業を作る。講師モデルは後で届くので仮とし、次を実装する。

1. プロットを用意してタイムラインを起こす
2. 授業内容の予習シートを pptx で用意する
3. 課題を与え、生徒ごとの課題状況を監視し、生徒の声を聞き分けて個別対応する
4. ふざけた生徒をロックアウト / 退出させる
5. 良い質問を共有する
6. サンプル教材は「アクションゲームの作り方」、基盤は Ergo Studio、元は <https://github.com/vtn-team/ActionGameSample>。
   プレイヤーの武器・敵の配置・よく使われるテクニックを、実装を抜いて何が足りないかを当てる授業にする。
   「ヒットストップをどう設計するか」「ノックバックで何が起きるか」「カメラはどうするのが良いか (答えはない)」を
   ClassPoint のような形で答えさせる。

## 現在地

- ブランチ `claude/charming-goldberg-p51pyj` (LUDIARS/Odeum)。GitHub PR #5 (open、未レビュー、未マージ)。
- 変更は Odeum だけ。同じ名前のブランチが指定されていた他の 9 リポ (Officina / Curare / Ingenium / Discutere / Manus /
  Tirocinium / Revisor / Lorica / AIFormat) は無変更。
- 上の 1〜6 は一通り実装済みで、授業側の単体テスト 14 本が通る。ブラウザでの通し確認も済み (下の「再現手順」)。
- AI の生徒を実際に出席させて見つけた欠陥 (発話が提出済みの回答を置き換える、仮モデルが守れない約束・根拠の無い講評、
  キーワードの取りこぼし、選択問題の解説が無い、模範解答と採点観点の食い違い、課題が実在しない手順を前提にしていた、
  生徒ページの表示の食い違い) を直した。2 回目の出席で見つけた欠陥 (回答後の発言に反応が無く質問扱いになる、先生に回した質問の
  行方が分からない、薄い答えが「いい質問」として共有される、課題のコマンドとポートが不十分、道具がつながったことが分からない、
  採点の語が広すぎる、ハーネスの窓の上限・実演の取りこぼし・個別メッセージの既読) も直した。振る舞いは仕様 (`spec/feature/ai-lesson.md`) に反映済み。
- 確認したのは Linux / GCC 13 だけ。

## 構成 (読む順)

| 場所 | 中身 |
| --- | --- |
| `spec/feature/ai-lesson.md` | 仕様の正本 (振る舞い・API・設定・保護・未実装) |
| `native/lesson/courses/action-game.plot.json` | サンプル教材 (9 セクション 90 分、問い 6、課題 1、FAQ) |
| `native/lesson/core/plot.*` | プロットの型と検証 |
| `native/lesson/core/model.hpp` / `provisional_model.cpp` | 講師モデルの interface と仮実装 (**差し替え口**) |
| `native/lesson/core/timeline.*` / `timeline_player.*` | プロット → タイムライン、再生 |
| `native/lesson/core/prep_sheet.*` / `pptx.cpp` / `zip.*` | 予習シートと pptx 書き出し |
| `native/lesson/core/activity.*` | 授業中の問い |
| `native/lesson/core/assignment.*` | 課題の見守りとヒント |
| `native/lesson/core/voice.*` | 話し中の判定、教室マイクの話者ラベル登録 |
| `native/lesson/core/conduct.*` | 注意・発言停止・退出 |
| `native/lesson/core/questions.*` | 質問ボード |
| `native/lesson/core/classroom*.cpp` | 上を束ねる教室 (I/O なし・時計なし)。受信は `classroom.cpp`、進行は `classroom_cues.cpp`、表示用は `classroom_views.cpp` |
| `native/lesson/{school,catalog,console_guard}.*` | 開講と参加コード、コース一覧、操作卓の保護 |
| `native/lesson/{config,server,connection,main}.*` | 設定、HTTP / WebSocket、CLI |
| `native/lesson/web/` | 生徒ページ (`lesson.*`、`voice.js`) と操作卓 (`console.*`)。`socket.js` は relay のものを埋め込む |
| `native/tests/lesson_*_tests.cpp` | 単体テスト (`lesson_fixture.hpp` がサンプル教材を読む) |
| `native/lesson/tools/e2e.cjs` | ブラウザ通し確認の手動スクリプト |

既存コードへの変更: `native/CMakeLists.txt` (`ODEUM_BUILD_LESSON`)、`native/relay/CMakeLists.txt` と `web_access.*`
(`WebAccess` を `odeum_relay_state` へ移し、環境変数名を受ける overload を追加)、`native/tests/CMakeLists.txt`、
`excubitor.catalog.yaml` (`odeum-lesson`)、`README.md`、`spec/feature/live-relay.md`。

## 再現手順

```sh
export LC_ALL=C.UTF-8        # 無いと Boost の zip 展開が configure で失敗する
cmake -S native -B native/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build native/build
ctest --test-dir native/build -R lesson --output-on-failure      # 14 本

native/build/bin/odeum-lesson courses
native/build/bin/odeum-lesson timeline action-game
native/build/bin/odeum-lesson prep-sheet action-game /tmp/prep.pptx

ODEUM_LESSON_CONSOLE_TOKEN_FILE=/tmp/console.token native/build/bin/odeum-lesson &
# 生徒: http://127.0.0.1:4410/lesson   操作卓: http://127.0.0.1:4411/console#token=$(cat /tmp/console.token)
NO_PROXY=127.0.0.1,localhost node native/lesson/tools/e2e.cjs /tmp   # Playwright + Chromium が要る
```

## 判断待ち (人間に確認する)

1. **GitHub PR の扱い。** AIFormat `RULE.md` §6.5 は GitHub PR と main 以外への push を禁じ、PR は Revisor 経由としている。
   クラウドのセッション指定に従って PR #5 を作った。Revisor の local PR に切り替えるなら #5 は閉じる。
2. **退出時に入室を自動で締め切る既定** (`Classroom::apply`)。別名での入り直しを防ぐためだが、遅刻者も入れなくなる。
3. **ポート 4410 / 4411** は仮。Excubitor の割当と照合していない。
4. **「モデル」の指す物。** 講師の LLM (語り・採点・発話の読み取り) と解釈して `LessonModel` を切った。
   音声認識・音声合成・話者分離・アバターを含むなら interface の追加が要る。
5. **ブラウザの音声認識** (`ODEUM_LESSON_BROWSER_STT`) を既定で無効にした。音声がブラウザ提供元へ送られうるため。

## 残作業 (優先順)

1. **講師モデルの差し替え。** `LessonModel` (`core/model.hpp`) を実装し、`main.cpp` の `make_provisional_model()` を置き換える。
   教室は同期で呼ぶので、応答の遅いモデルは結果を非同期で返す形に `Classroom` 側を直す必要がある
   (`hear` / `ask` / `Activity::answer` の採点が呼び出し箇所)。LLM を入れるときは AIFormat `RULE_AI_SECURITY.md` §2.7
   (生徒の入力とシステム指示の分離、出力の検証、回数・時間の上限、個人データを外へ送らない) を受入条件にする。
2. **Ergo Studio 連携の残り。** 授業側の経路 (`/v1/tool/*`、`native/lesson/tool_desk.*` / `server_tools.cpp`) と
   Ergo Studio の「授業」プラグイン (Ergo `tools/studio/plugins/lesson/`、ブランチ `claude/ai-lesson-odeum-j57vy6`) は実装済み。
   `feel.*` を公開するヘッドレスのサンプル `examples/lesson_action_feel` は Ergo 側にできた (同ブランチ、LUDIARS/Ergo#95、未マージ)。
   サンプル教材の準備・課題・ヒントはこのサンプルでできることだけに書き直した (遊ぶ手順は無い。演出は Studio の `/variable/ws` に
   `set` を送って ON に戻し、ログの数値で確かめる)。手順の `how` のコマンドは Linux の Ergo のビルドで書いたとおりに打って確かめた。
   残りは (a) Windows で両者をつないだ確認 (手順のコマンドは bash 前提)、(b) 遊べる Ergo 版アクションゲーム
   (ActionGameSample の移植) の作成と、それに合わせた教材の手順の書き直し、(c) Studio に変数を切り替える画面ができたら、
   課題の手順から Node の 1 行 (`/variable/ws` への `set`) を外す、(d) 人が実際に授業として通す確認。
3. **映像の統合。** 生徒ページに relay の視聴を埋め込むか、GLab の視聴ページに授業の操作を載せるかを決める。
   いまは別ページ。ゲスト参加 (参加コード) は relay では映像を受けられない。
4. **本人確認。** いまは参加コード + 表示名。GLab / Cernere のチケットで入るようにすれば退出が人に結び付く。
5. **サーバ側の音声。** 生徒端末の音声を受けて認識する経路 (いまは音量の数値と文字だけ)。
6. **未確認の環境。** Windows (MSVC) / macOS のビルド、PowerPoint 実機での pptx 表示、実マイクでの音量判定、
   スマホ実機での生徒ページ。トークンファイルの権限は Windows ではフォルダの ACL 頼み。
7. **リポの決まり事の残り。** `augur.contracts.json` / `contracts/` への契約の追加、`.augur/tests.jsonl` へのテスト登録、
   `praesidium.json` の更新はしていない。Revisor の登録テスト・local PR も未実施。
8. **細かい改善。** 仮モデルの語りは要点 1 行だけで持ち時間が余る / 課題の確認 (`assignment.status`) は人数を出すだけ。

## 既知の問題

- 既存テスト `odeum.overlay` が不安定 (今回の変更を外しても 30 回中 25 回 SEGFAULT)。プロセス終了時に libdatachannel の
  スレッドが OpenSSL を触っている。今回は直していない。`ctest -E overlay` なら全て通る。
- 仮モデルのキーワード判定は粗い (例: 「全部止める」だけで「何を止めるか」に触れた扱いになる)。「見えない・聞こえない」の知らせの
  読み取りも語の組み合わせなので、カメラの問いの答えに「画面の敵が見えなかった」と書くと知らせと読み違えうる (本人には受け取り方が
  返るので、回答欄から答え直せる)。送り仮名の揺れと半角カナは
  吸収するようにしたが、言い換え (「敵がフレームに入らない」など) は語を足さない限り当たらない。モデル差し替えで解消する前提。

## 守ること

- 表示名・回答・質問・発話をファイル・DB・ログに書かない (メモリだけ)。予習シートに問いの答えを載せない。
- 操作卓は loopback 固定。転送経路の転送先に操作卓のポートを登録しない。
- 新しいファイルは `spec/domains/ai-lesson.domain.json` の membership に入る場所 (`native/lesson/`、`native/tests/lesson_*`) に置く。
- コミット・PR・コードに、作業した AI のモデル名を書かない。
