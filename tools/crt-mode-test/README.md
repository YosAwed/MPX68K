# crtmodetest — CRT画面モード巡回テストディスク

MPX68Kの手動回帰(モード切替・1024ドット・WinStrech・ASan実機確認)用の
セルフブートX68000プログラムです。Human68kなどの著作物は一切含まず、
IPLROMのIOCS(`_CRTMOD`/`_G_CLR_ON`/`_B_PRINT`)だけで動きます。

## 動作

起動すると約2秒間隔で以下を無限に巡回します。各モードでグラフィックに
カラーバンドを描き、テキストプレーンにモード名を出力し続けます
(テキストは消さないので、切替時の残像・ゴミが見えれば異常です)。

| 順 | 設定 | 期待される表示 |
|---|---|---|
| 1 | CRTMOD 16 | 768×512 16色 31kHz |
| 2 | CRTMOD 4 | 512×512 16色 31kHz |
| 3 | CRTMOD 8 | 512×512 256色 31kHz |
| 4 | CRTMOD 6 | 256×256 16色 31kHz(ラスタ二度読み) |
| 5 | CRTMOD 7 | 256×256 16色 15.98kHz |
| 6 | CRTMOD 5 | 512×512 16色 15.98kHz(インターレース) |
| 7 | CRTC直接設定 | 768×512から**128カラム=1024ドット幅**へ(R00=$A9, R03=$9C。タイミングモデル上もvalidな組合せ) |

## 確認ポイント

- DebugビルドでAddress Sanitizerを有効にして一巡以上放置(特に7番の
  1024ドットでText_TrFlag/ScrBufのオーバーランが出ないこと)
- WinStrech 0〜3それぞれで残像・崩れ・クラッシュがないこと
- Debug/Release双方で起動し、巡回が安定して続くこと

## 使い方

`crtmodetest.xdf` をFDD0に挿入して起動(HDDは外すかブート順をFDに)。
ROM(IPLROM/CGROM)は通常どおり必要です。

## ビルド

```bash
./build.sh
```

GNU binutils (m68k) が必要です(Debian/Ubuntu: `binutils-m68k-linux-gnu`)。
Windows/macOSではDockerで:

```bash
docker run --rm -v "$PWD:/w" -w /w/tools/crt-mode-test ubuntu:22.04 \
  bash -c "apt-get update -qq && apt-get install -y -qq binutils-m68k-linux-gnu && ./build.sh"
```

生成される `crtmodetest.xdf` は生の2HDダンプ(77シリンダ×2面×8セクタ×1KB)で、
先頭1KBのブートセクタにプログラム全体が入っています。
