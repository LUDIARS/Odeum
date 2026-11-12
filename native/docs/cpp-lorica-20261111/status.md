# Odeum の作業状況 (job cpp-lorica-20261111, W1)

ブランチ `cpp/odeum-node-port` (job の着手時 `9070773`、2 代目の担当の着手時 `ff34ca4`)。push していない。
最終報告は `/home/user/.cpp-deps/reports/Odeum-report.md`。

積んだコミット (古い順):

| SHA | 段階 | 内容 |
|---|---|---|
| 49fd09d | 棚卸し | `spec/plan/2026-11-11-lorica-adoption.md` (前任) |
| 206e765 | 修正 | presenter tests の偽エンコーダの集成体初期化 (前任) |
| 33fcb39 | WIP | Lorica 固定 5e8281b0・protocol の上限ヘルパ・fuzz の入口 (前任、未検証のまま積まれたもの) |
| ff34ca4 | 文書 | 引き継ぎ文書 (前任) |
| b28187f | Lorica の適用 | WIP の CMake を確認。全自前ターゲットに `odeum_harden`、未適用は configure で FATAL_ERROR、第三者に baseline |
| aa9add9 | 入力の上限 | protocol のヘルパを呼び出し側へ。relay / presenter / program の上限と境界テスト 4 本 |
| a5a7514 | 不具合修正 | `odeum.overlay` の SIGSEGV (libdatachannel のワーカーと OpenSSL の atexit の競合) |
| 3f579e4 | sanitizer と fuzz | `ODEUM_SANITIZE`、`ODEUM_FUZZ`、fuzz 入口 10 本と seed の回帰、Clang 18 の回避、runner のログ初期化の競合 |
| d7d47fa | 文書 | `.anatomia/layers.json` に native/ の層 (要確認) |
| a44d382 | 文書 | この文書と plan の 5 章 |
| 7e3338f | merge | origin/main `bb2775e` (Cloudflare TURN) を merge。衝突 3 か所を手で合わせた |
| b8f8490 | 入力の上限 | Cloudflare TURN の応答に JSON の予算と件数の上限、境界テスト、fuzz 入口 cloudflare_turn |
| 62a206c | Lorica の確認 | `odeum.lorica_applied.*` (COMMON-4 作業 B) |
| 7a029e0 | merge | origin/main `913f08d` (odeum-lesson #5、Revisor の登録 #7) を merge。衝突 3 か所、lesson の 5 ターゲットを EXTENDED |
| 07e141c | Lorica の適用 | `odeum.lorica_applied.binary.odeum-lesson` |
| 30077bf | 入力の上限 | odeum-lesson の WebSocket・道具の POST・プロットのファイルに上限、`odeum.lesson.limits`、fuzz 入口 lesson_message / lesson_plot |
| (このコミット) | 文書 | この節 |

## (a) 確かめたこと (このコンテナ、GCC 13.3 / Clang 18.1.3)

- GCC Release (`native/build`): configure・build 成功、ctest 46/46 通過 (merge 後は 48/48: odeum.turn と odeum.fuzz.cloudflare_turn を含む) (relay 10、protocol.limits と relay.limits、
  presenter 7、program 6、runner 11、fuzz の seed 回帰 10)。
- ASan + UBSan (`-DODEUM_SANITIZE=address,undefined`、GCC RelWithDebInfo `-g1`): ctest 46/46 通過、sanitizer の報告なし。
- TSan (`-DODEUM_SANITIZE=thread`、GCC): ctest 46/46 通過。`tests/tsan.supp` で libdatachannel 内部の
  `synchronized_stored_callback` の競合だけを抑制 (第三者)。抑制なしでも odeum_overlay_tests 30 回中 0 回失敗。
- Clang 18 `-DODEUM_FUZZ=ON` (ASan+UBSan): ctest 46/46、10 入口を各 30 秒、クラッシュなし。
- sanitizer / fuzz のビルドディレクトリは結果を記録してから消した (`-g1` で ASan 2.1GB)。
- Lorica: 自前 44 ターゲット (fuzz を含めて 58、ODEUM_FUZZ で 68) が `odeum_lorica_require_coverage` を通る。
  compile_commands.json で自前・第三者の全コンパイルに `-fstack-protector-strong` と FORTIFY 3、打ち消すフラグなし。
- `verify_binary.py`: odeum-relay / odeum-runner / odeum-unityctl とも PIE / NX stack / full RELRO。
- `odeum.overlay`: 修正前 Release で 40 回中 22 回 SIGSEGV、修正後 60 回中 0 回 (ASan のビルドで 30 回中 0 回)。

- main の Cloudflare TURN (merge 後): 新しいターゲットとテスト (`odeum.turn`) も Lorica の網羅確認を通る。
  応答 (HTTP 64KiB は既存) の JSON に {64KiB, 値 2048, 深さ 6} と 16 件 × 16 URL の上限を入れ、境界テストを
  `odeum.relay.limits` に足した。秘密値 (TURN の資格情報、API token) はエラー文・ログに出ない (テストで確認)。

- 追加 (COMMON-4 作業 B): `odeum.lorica_applied.{baseline,extended,compile_commands}` と
  `odeum.lorica_applied.binary.{odeum-relay,odeum-runner,odeum-unityctl}` の 6 本。ctest 54/54 通過。
  わざと壊して落ちること: `-fcf-protection=none -U_GLIBCXX_ASSERTIONS` → `.baseline` と `.compile_commands`、
  odeum_relay_state に `-fno-stack-protector` と juice に `-D_FORTIFY_SOURCE=0` → `.compile_commands` (自前・第三者を分けて列挙)、
  odeum-relay に `-ftrivial-auto-var-init=uninitialized` → `.compile_commands`、unsafe API の拒否を外す → `.baseline` / `.extended`
  (configure の網羅確認は通ってしまう構成)、odeum_harden を 1 本外す → configure の FATAL_ERROR。MSVC と macOS の枝は未実行。

- main の odeum-lesson (merge `7a029e0` 以降): lesson の lib 4 本と odeum-lesson は EXTENDED (外から HTTP / WebSocket を
  受け続けるサーバ。relay と同じ扱い)、lesson のテスト 17 本・fuzz の回帰 2 本は baseline。unsafe API の拒否で落ちる呼び出しは
  無かった。Lorica の網羅 86 ターゲット通過、GCC Release ctest 74/74 (merge 前 54 + lesson 16 + binary.odeum-lesson +
  lesson.limits + fuzz.lesson_message / lesson_plot)。
- 上限 (`native/lesson/input_limits.hpp`、表は `spec/feature/ai-lesson.md` の「外から入る入力の上限」): WebSocket は値 1024・深さ 8、
  道具の POST は値 64・深さ 4、プロットのファイルは 1 MiB を読みながら判定・値 16384・深さ 16。HTTP のヘッダ 16 KiB・本文 4 KiB・
  16 KiB の受信・毎秒 120 件・接続 2048・操作卓 8 は main の既存値。
- lesson まわり (lesson 17 本 + fuzz 回帰 2 本 + lorica_applied 2 本) を GCC の TSan と ASan + UBSan で実行し、各 22/22 通過・報告なし。
  Clang 18 ODEUM_FUZZ で lesson_message を 30 秒 (342,608 回)、lesson_plot を 30 秒 (138,899 回)、クラッシュなし。
  ctest に lesson のサーバ (connection.cpp / server.cpp) を立てるテストは無く、sanitizer でサーバの経路は通っていない。
- `odeum.overlay`: merge 後に 20 回繰り返して失敗 0。
- `.anatomia/layers.json`: main の語彙 (shared / domain-logic / ...) に合わせ、branch 側の native/fuzz と native/runner/* を足した。
  native/ の未分類 0。Augur の台帳 (`.augur/tests.jsonl`) には branch 側のテスト (relay.limits、runner、lorica_applied、fuzz、lesson.limits
  など) を足していない (id は Augur が振るため)。

## (b) 確かめていないこと

- Windows (MSVC) と macOS の configure / build。presenter / sender / program のアプリ部分 (`settings_file.cpp`、
  `program_settings_file.cpp`、`input_receiver.cpp` の変更を含む) は Linux ではコンパイルされない。
- Cloudflare への実際の HTTPS 取得 (ネットワークと本物の token が要る)。
- MSVC での EXTENDED (19.44 以上が要る) と `lorica_deprecate_unsafe_apis` (入れていない)。

## (c) 途中のこと

なし。

## (d) 分かっている失敗・振る舞いの変化

- 新しく断る入力: 報告の 3 章と plan の 5.2 の表 (WebSocket の深さ 9 以上・値 1025 以上、クエリ 17 件以上など)。
- relay の PLI/FIR 判定は presenter と同じ実装になり、複合 RTCP の 2 つ目以降の版が 2 でなければ要求とみなさない。
- archive.json は 16KiB を超えると (読みながら判定) カタログに出ない。深さ 3 以上・値 65 以上は従来どおりカタログのエラー。
- GCC 13 の `-Wstringop-overflow` 誤検知 (program/core/flv.cpp の vector::insert、既存) が FORTIFY 3 で出る。エラーではない。

## (e) 人間の判断が要ること

1. Tela (+ Pictor) は許可リスト外の既存の依存 (presenter / program のアプリ部分)。外していない。
2. macOS: Lorica の hardening は macOS を拒否する。macOS の configure は `-DODEUM_LORICA_MACOS_UNHARDENED=ON` を
   明示しないと止まるようにした (黙って外さない)。Cocoiru の `find_package(Odeum)` 用の sender の配布手順と
   macOS の odeum-program のビルド手順に、この option を足すかどうか。
3. MSVC で `lorica_deprecate_unsafe_apis` を入れるか (`/we4996` で `getenv` などの CRT の非推奨も拒否される。
   入れるなら `_CRT_SECURE_NO_WARNINGS` の扱いか置き換えが要る)。
4. 新しく断る入力の上限値の承認 (報告の 3 章)。
5. 第三者のコードは baseline の hardening のみ (boundary は呼び出し側にしか入れられない)。
6. TSan の抑制 (`tests/tsan.supp`、libdatachannel 内部の競合) と、Clang UBSan での libjuice の `-fno-sanitize=function`。
7. `.anatomia/layers.json` の native/ の層の割り当て。

## (f) ローカルのビルドとテストの手順

```sh
cd /home/user/Odeum
export CCACHE_DIR=/home/user/.cpp-deps/ccache
cmake -S native -B native/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DLORICA_SOURCE_DIR=/home/user/Lorica \
  -DFETCHCONTENT_SOURCE_DIR_BOOST=/home/user/.cpp-deps/src/boost-1.87.0 \
  -DFETCHCONTENT_SOURCE_DIR_JSON=/home/user/.cpp-deps/src/nlohmann_json/json \
  -DODEUM_BUILD_PRESENTER_CORE=ON -DODEUM_BUILD_PROGRAM_CORE=ON \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
/home/user/.cpp-deps/bin/buildslot cmake --build native/build -j2
/home/user/.cpp-deps/bin/buildslot ctest --test-dir native/build -j2 --output-on-failure
```

- sanitizer: 別のビルドディレクトリで `-DODEUM_SANITIZE=address,undefined` または `-DODEUM_SANITIZE=thread`
  (`-DCMAKE_BUILD_TYPE=RelWithDebInfo "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O2 -g1 -DNDEBUG"` で 2GB 程度)。1 つずつ作り、終わったら消す。
- fuzz: `-DODEUM_FUZZ=ON -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++`、
  `<build>/fuzz/odeum_fuzz_<name> -max_total_time=30 <corpus> native/fuzz/seeds/<name>`。
- キャッシュが無いときは `-DLORICA_SOURCE_DIR` と `-DFETCHCONTENT_SOURCE_DIR_*` を外す (固定の commit / URL+SHA256 から取得)。
