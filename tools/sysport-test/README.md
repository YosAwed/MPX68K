# sysporttest — システムポート / ILLEGAL 例外確認ディスク

MPX68K の次の修正を確認する、Human68k 不要のセルフブート XDF です。
IPLROM の IOCS だけで動き、著作物は収録していません。

| # | 項目 | 期待値 |
|---|---|---|
| 1 | ILLEGAL($4AFC)例外でスタックされる PC | `stacked PC` が `ILLEGAL at` と同じ値で `PASS` |
| 2 | $E8E00B 機種判定 | 16/24MHz → `$FE`、それ以外のクロック → `$FF`。Clock メニューで切り替えると即座に更新 |
| 3 | ゲストからの電源OFF | `P` キー($00,$0F,$0F を $E8E00F へ)で POWER OFF 表示になり停止。リセットで復帰 |
| 4 | 不完全な電源OFF手順 | `N` キー($00,$0F,$05,$0F)では停止せず動作し続ける |

修正前の MPX68K では 1 が `FAIL`(+2 した値)、2 はクロックによらず `$DC`、
3 は何も起きません。

## 使い方

`sysporttest.xdf` を FDD0 に挿入して起動します(HDD は外すか、ブート順を FD に)。
ROM(IPLROM/CGROM)は通常どおり必要です。`N` を押した後に `P` を押しても電源OFF
されるのが正しい動作です。

## ビルド

```bash
./build.sh
```

GNU binutils (m68k) が必要です。Homebrew の elf2x68k ツールチェーンなら
`PREFIX=m68k-xelf- ./build.sh`。
