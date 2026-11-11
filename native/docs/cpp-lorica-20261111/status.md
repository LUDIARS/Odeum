# Odeum の作業状況 (job cpp-lorica-20261111, W1) — 引き継ぎ用

ブランチ `cpp/odeum-node-port` (着手時 `9070773`)。push していない。

積んだコミット (古い順):

| SHA | 内容 | 検証 |
|---|---|---|
| 49fd09d | docs: 棚卸し (`spec/plan/2026-11-11-lorica-adoption.md`、live-relay の membership に登録) | 文書のみ |
| 206e765 | fix(presenter tests): `odeum.presenter.keyframe` の偽エンコーダの集成体初期化を修正 | 着手時の構成のビルドで ctest (下記 a) |
| 33fcb39 | **WIP(未完・未検証)**: Lorica の固定変更・protocol の上限ヘルパ・fuzz の入口 | 未ビルド・未テスト |
| (この文書のコミット) | docs: 引き継ぎ文書 | — |

## (a) 済んで確かめたこと

いずれも Lorica の固定変更 (33fcb39) を入れる**前**の CMake (= 着手時の `9070773` の CMake、Lorica `cab4f120` の代わりに
ローカルの `/home/user/Lorica` = `5e8281b` を `LORICA_SOURCE_DIR` で与えた) での結果。

- configure (下記 i の option): 成功。Lorica の baseline probe (stack-clash, cf-protection=full, FORTIFY 3) は C/C++ とも成功。
- `cmake --build native/build -j2`: 成功 (306 ステップ)。警告 28 件 (自前コードの `-Wmissing-field-initializers` /
  `-Wmisleading-indentation` と libdatachannel ヘッダの `-Wpedantic`)。つまり runner のコードは Lorica `5e8281b` の API でもビルドが通る。
- `ctest --test-dir native/build -j2`: 着手時は 32 件中 `odeum.presenter.keyframe` が毎回失敗 → 206e765 で修正後、
  その 1 本を再ビルドして通過。全体は 32 件中 32 件通過の回と、`odeum.overlay` が 1 件 SegFault する回がある (e 参照)。

## (b) 済んだが確かめていないこと

なし (WIP コミットの中身はすべて (c))。

## (c) 途中のこと (WIP コミット `33fcb39`)

- `native/CMakeLists.txt`: Lorica を常に取り込み、固定を `5e8281b0e60fa16f814e16eaa620715937e8618d` に変更
  (ローカルの binary dir 名は `_lorica`)。**この CMake での configure・ビルド・ctest は未実行** (ビルド枠の待ちで止めた)。
  runner 以外のビルド単位への `Lorica::boundary` / `lorica_harden_target` の適用はまだ書いていない。
- `native/protocol/json_limits.{hpp,cpp}`: `parse_json_within(text, JsonLimits)` (Lorica `input_budget` で本文・値の数・深さを課金)。
  上限案: WS 1 メッセージ {16KiB, 1024, 8}、チケットの 1 パート {8192, 256, 4}、設定ファイル {1MiB, 16384, 16}。
- `native/protocol/bounded_file.{hpp,cpp}`: `read_file_within(path, max)` (16KiB ずつ読み、`checked_add` で上限超過を拒否)。
- `native/protocol/rtcp.{hpp,cpp}`: `is_keyframe_request(span)` を `byte_view` / `checked_*` で実装 (relay の
  `contains_keyframe_request` と presenter の `is_keyframe_request` の重複を 1 本にする予定)。パケット上限 65535。
- 上の 3 組はいずれも `native/protocol/CMakeLists.txt` に未登録で、呼び出し側も未変更。
- `native/fuzz/`: libFuzzer 入口 7 本 (ws_message, ticket, http_target, rtcp, rtmp, launch_url, runner_json)、
  seed、seed を 1 回ずつ流す `regress_main.cpp`。CMake 未登録 (予定: `ODEUM_FUZZ` option 既定 OFF、Clang のみ。
  回帰用実行ファイルは GCC でも ctest に入れる)。`fuzz_runner_json.cpp` の `parse_*_message` は戻り値の型をまだ確かめていない。
- `native/fuzz/` の membership は引き継ぎ文書のコミットで live-relay に登録した。

## (d) 未着手のこと

1. 全ビルド単位への Lorica 適用 (案: トップの CMake に `odeum_lorica(<t>)` を作り、自前ディレクトリの
   `BUILDSYSTEM_TARGETS` を集めて `Lorica::boundary` (exported な lib は `$<BUILD_INTERFACE:...>`) と
   `lorica_harden_target` を入れ、未適用が残れば FATAL_ERROR。`odeum-relay` と `odeum-runner` は
   `lorica_extended_hardening_supported` が真なら EXTENDED)。`lorica_deprecate_unsafe_apis` を既存単位に入れられるかの確認。
2. 第三者 (datachannel, juice, srtp2, usrsctp, opus) に `lorica_harden_target` を差し込めるかの確認。
3. 入力経路の上限チェックと境界テスト: relay (WS メッセージ → `parse_message`、チケット、HTTP target の
   `parse_target`、鍵ファイル `keys >> c.keys` は上限なし、ICE の env JSON、RTP パケットの大きさ)、
   program (RTMP `ChunkReader` のチャンクストリーム数と保留バイト合計が無制限、AMF0 の要素数が無制限、
   archive manifest の JSON、設定ファイル、WebRTC の入力フレームの大きさ)、presenter (起動 URL のクエリ件数、
   `ticket_expiry` の JSON、設定ファイル)。
4. sanitizer の option (`ODEUM_SANITIZE=address,undefined` / `thread`) — runner を含め repo に無い。
5. fuzz の CMake 登録と短時間の実行。
6. `verify_binary.py` の実行 (`odeum-relay` `odeum-runner` `odeum-unityctl`)、`compile_commands.json` での守りの確認。
7. 報告 `/home/user/.cpp-deps/reports/Odeum-report.md` (指示書 11 章の形) — 未作成。
8. 移植の残り: 前回の runner の移植に未了の項目は見つかっていない (棚卸し 4 章)。

## (e) 分かっている失敗や落ちるテスト

- `odeum.overlay` (`native/tests/overlay_tests.cpp`) が時々 SegFault する。着手時のコードのままで再現
  (単体で 6 回実行し 3 回 exit 139)。原因は未調査 (libdatachannel のスレッドとの競合を疑うが未確認)。
- `odeum.presenter.keyframe` は着手時に毎回失敗していた → 206e765 で修正済み。

## (f) 人間の判断が要ること

1. **Tela (+ Pictor)** は許可リスト外の既存の依存 (presenter / program のアプリ部分)。外していない。
2. **macOS**: Lorica の `lorica_harden_target` は macOS を拒否する (configure で FATAL_ERROR)。presenter / program /
   sender は Windows と macOS 専用で、macOS の親機 (`odeum-program`) と Cocoiru の `find_package(Odeum)` が使っている。
   全ビルド単位に適用すると macOS の configure が通らなくなる。指示書の決まりでは守りを外して通すことはしない。
3. presenter / sender / program のアプリ部分は Linux でビルドできない (CMake が Windows・macOS 以外を拒否、Tela 必須)。
   クラウドでは CMake の記述までしか確かめられない。
4. 上限チェックで新しく断るようになる入力 (WS メッセージの深さ 8・値 1024 など) の値の承認。

## (g) Lorica の適用表と上限チェックの場所 (いま時点)

| ビルド単位 | Lorica::boundary | lorica_harden_target | deprecate_unsafe_apis |
|---|---|---|---|
| odeum_runner_core (PUBLIC) と runner の lib・実行・テスト | あり | baseline | なし |
| それ以外 (protocol, relay, presenter, program, そのテスト、第三者) | なし | なし | なし |

固定 commit: コミット済みの CMake は `5e8281b0…` (WIP、未検証)。検証済みのビルドは `/home/user/Lorica` (`5e8281b`) を
`LORICA_SOURCE_DIR` で与えたもの。`verify_binary.py` は未実行。

上限チェック: 今回の job で呼び出し側に入れたものはまだ無い。既存は runner (前回 job、`native/runner/core/json.cpp` など)。

## (h) 次にやる順番

1. WIP の CMake で configure → build → ctest (i の手順)。通れば Lorica 固定のコミットとして分け直す。
2. (d)-1 の適用 (+ EXTENDED の試行、第三者) → build → ctest → `verify_binary.py` → コミット。
3. protocol のヘルパを CMake に登録し、呼び出し側を差し替え、境界テスト (上限ちょうど / +1 / 壊れた / 巨大) を追加 → コミット。
4. program の RTMP / AMF0 / archive、presenter の URL / 設定の上限 → テスト → コミット。
5. sanitizer option と fuzz の登録 → ASan+UBSan / TSan の ctest、fuzz 数十秒 → コミット。
6. 報告を書く。

## (i) ローカルのビルドとテストの手順

前提: Linux x86_64、GCC 13 / CMake 3.24 以上 / Ninja / OpenSSL 3 の開発ファイル。libdatachannel は
FetchContent が git で取得する (ネットワークが要る)。

```sh
cd /home/user/Odeum
export CCACHE_DIR=/home/user/.cpp-deps/ccache
/home/user/.cpp-deps/bin/buildslot cmake -S native -B native/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DLORICA_SOURCE_DIR=/home/user/Lorica \
  -DFETCHCONTENT_SOURCE_DIR_BOOST=/home/user/.cpp-deps/src/boost-1.87.0 \
  -DFETCHCONTENT_SOURCE_DIR_JSON=/home/user/.cpp-deps/src/nlohmann_json/json \
  -DODEUM_BUILD_PRESENTER_CORE=ON -DODEUM_BUILD_PROGRAM_CORE=ON \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
/home/user/.cpp-deps/bin/buildslot cmake --build native/build -j2
ctest --test-dir native/build -j2 --output-on-failure
```

新しいコンテナで `/home/user/Lorica` などのキャッシュが無いときは、`-DLORICA_SOURCE_DIR` と
`-DFETCHCONTENT_SOURCE_DIR_*` を外すと CMake に書いた固定の commit / URL+SHA256 から取得する。
relay と runner は既定 ON。`ODEUM_RUNNER_WERROR=ON` で runner の警告をエラーにできる。
