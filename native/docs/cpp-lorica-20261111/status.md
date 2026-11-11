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
| (このコミット) | 文書 | この文書と plan の 5 章 |

## (a) 確かめたこと (このコンテナ、GCC 13.3 / Clang 18.1.3)

- GCC Release (`native/build`): configure・build 成功、ctest 46/46 通過 (relay 10、protocol.limits と relay.limits、
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

## (b) 確かめていないこと

- Windows (MSVC) と macOS の configure / build。presenter / sender / program のアプリ部分 (`settings_file.cpp`、
  `program_settings_file.cpp`、`input_receiver.cpp` の変更を含む) は Linux ではコンパイルされない。
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
