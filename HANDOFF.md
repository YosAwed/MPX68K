# HANDOFF — CRTC可変タイミング化 (Windows→Macセッション引き継ぎ)

> Mac側のClaude Codeへ: このファイルは前セッション(Windows, 2026-08-18)からの
> 引き継ぎ文書。読んだら「メモリへの保存」節の内容を自動メモリに保存し、
> 「次の作業」から続行すること。このファイル自体は#25マージ前に削除してよい。

## 全体計画(合意済みの7段階)

CRTCを固定55.45Hzフレーム(`VSYNC_HIGH`/`VSYNC_NORM`二択、`winx68k.cpp:813`)から
レジスタ由来の可変タイミングへ移行する:

1. ✅ 純粋タイミングモデル分離(副作用なし)
2. ✅ 既存モード一致テスト
3. ✅ バッファ/stride/画面情報APIの可変化
4. ⬜ R0/R4/R20/HRLからの動的ライン・フレーム周期生成
5. ⬜ Swift側55.45Hz固定実行のCPU時間基準化(`GameScene.swift` update loop)
6. ⬜ HSYNC 4フェーズ/ラスタIRQ/ラスタコピー/高速クリア精密化
7. ⬜ インターレース・二度読み・スリット・途中モード変更の検証

## 現状 (2026-08-18時点)

- **PR #24** `crtc-pure-timing-model` — 段階1+2。全CIグリーン。**マージ待ち**。
  - `x68k/crtc_timing.{c,h}`: CrtcTimingモデル。発振器 38,863,632 / 69,551,900 /
    50,349,800 Hz(XEiJ現行値)。分周比表 index=HRL<<3|HF<<2|HRES。
    走査モードはXEiJ行列(normal/slit/duplication/interlace、VRES>HFでインターレース。
    レガシー`(R20&0x14)`はHF=1,VRES≥2を誤判定 — テストで固定済み)。
    VDISP実表示は[R06+1, R07]で`v_disp_first`を追加(レガシー窓[R06,R07)は1ラスタ早い)。
    valid条件: 表示幅1..128カラム、R03≤R00+1、垂直は厳密に R05<R06<R07<R04。
  - `tests/core/test_crtc_timing.c`: 実crtc.cとリンクしレガシーglobalsと一致検証。
  - ⚠️ 履歴に誤コミットされた`xeij-CRTC.java`が残存(削除コミットd1d8ca7で
    先端からは除去済み)。完全除去するならマージ前にrebaseで7788972と d1d8ca7 を
    整理してforce-push(XEiJライセンス配慮)。そのままマージも動作上は問題なし。
- **PR #25** `crtc-frame-snapshot` — 段階3。**#24にstack(base=crtc-pure-timing-model)**。
  全CIグリーン(macOSビルドでpbxproj手編集も検証済み)。#24マージ後にbaseをmasterへ変更。
  - `x11/scrbuf.{c,h}`: ScrBuf所有権を分離。stride 1024×1024行+ガード2行。
    `X68000_GetFrameInfo`: buffer/width/height/stride/scan_mode/field_parity(暫定0)/
    refresh_hz/timing_valid/世代番号を一括返却。
    **API契約: 寸法とscan_modeは「現行レンダラーが実際に生成したフレーム」**
    (TextDotX/Y+v_step由来)。refresh_hz/timing_validのみハードウェアモデル由来。
  - windraw.c: `VLINE≥1024`・stride超の行を配列アクセス前に破棄。
    `X68000_GetImageInto(data, capacityBytes)`(容量チェック付きRGBA変換)追加。
    死配列Draw_BitMask/Draw_TextBitMask削除。stride定数は`SCRBUF_STRIDE`。
  - tvram: `Text_TrFlag`を`SCRBUF_STRIDE+16`に拡張(1024ドットで16バイト
    オーバーランしていた — レビューP1)。
  - tests: ASan/UBSan付きビルド(`Makefile`の`SAN`変数、`SAN=`で無効化)。
    1024ドット描画・スナップショット整合・ガードのテストあり。
  - `.github/workflows/build.yml`: pull_requestのブランチフィルタを削除
    (stacked PRでもCIが走る)。
  - `tools/crt-mode-test/`: セルフブートXDF生成ツール(下記)。

## 次の作業(このMacでやること)

1. #24をマージ(必要なら先にxeij-CRTC.java履歴掃除のrebase)
2. #25のbaseをmasterへretarget、CI再確認、マージ
3. **実機検証**: `sh tools/crt-mode-test/build.sh`(要 m68k binutils、DockerでもOK、
   README参照)で`crtmodetest.xdf`を生成しFDD0から起動。
   CRTMOD 16→4→8→6→7→5→CRTC直叩き1024ドット を約2秒間隔で無限巡回する。
   - Debug+Address Sanitizer(Xcodeスキーム設定)で一巡以上放置。特に最終フェーズ
     (1024ドット)がText_TrFlag修正の本丸。
   - 巡回中にWinStrech 0〜3切替で残像・崩れ・クラッシュがないこと。
   - Debug/Release双方でビルド・起動、FDD/HDD・入力・回転の回帰確認。
   - XDFは前セッションで未実機検証。ブートしなければFDブート順とROM配置を確認、
     それでも駄目ならブートセクタのロード(IPLがtrack0/side0/sector1の1KBを
     $2000にロードして実行する前提)を疑う。
4. **Swift統合**(段階3の仕上げ): GameScene.swiftの
   「Update前に`X68000_GetScreenWidth/Height` → Update後に`X68000_GetImage`」を
   「Update後に`X68000_GetFrameInfo` → `X68000_GetImageInto`」へ置換。
   ブリッジングヘッダ(`px68k/fmgen/X68000 macOS-Bridging-Header.h`)への宣言追加が必要
   (scrbuf.hを相対includeするか宣言をミラー)。
5. その後段階4へ: `CrtcTiming_CyclesPerRaster/PerField`(既約分数)を使い
   `WinX68k_Exec`の固定`clk_total`を動的化。

## 環境メモ

- テスト実行: `make -C tests/core`(Linux/CI)。ASan込み。
- リポジトリ規約: AGENTS.md — 4スペース・タブ禁止、新規テストは本来XCTest
  (ホストCテストは既存構成として許容との判断済み)。
- レビューではXEiJ (https://stdkmd.net/xeij/) を参照仕様として照合している。
  コード移植はしない(独立実装、ライセンス上も)。

## メモリへの保存(Mac側Claude Codeへ)

上記「全体計画」「現状」「次の作業」を要約して、自動メモリに
`crtc-variable-timing-roadmap` として保存すること(type: project)。
