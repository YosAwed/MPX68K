# MPX68K

[English](README.md) | 日本語

px68k エミュレータコアをベースにした、macOS 用の SHARP X68000 エミュレータです。
このリポジトリは、ひっそりぃ氏の [px68k](https://github.com/hissorii/px68k) の間接的なフォークです。同氏のソースコードを [GOROman](https://github.com/GOROman) 氏が iOS 向けに移植し、それを私が macOS で動くようにしました。

## 概要

MPX68K は、C で書かれた低レベルのエミュレーションコードを SpriteKit と Swift の UI フレームワークにつなぎ、Apple Silicon と Intel の両方の Mac で SHARP X68000 を忠実にエミュレートします。

> メニュー名などの UI 表記は英語のため、本書でもメニュー項目は英語のまま記載しています。

## 機能

### コアエミュレーション
- **X68000 ハードウェアエミュレーション**: CPU、サウンド、グラフィック、I/O
- **M68000 CPU**: C68K エミュレータコアを使用
- **CPU クロック切替**: 1 / 10 / 16 / 24（既定）/ 40 / 50 MHz。各クロックの本来のサイクル数で動作します（16 MHz は XVI の 50/3 MHz）
- **エミュレーション速度**: 通常 / Turbo 2x〜5x / No-Wait による早送り（音程は変わりません）
- **FM 音源**: fmgen による OPM + ADPCM の高品質な再生
- **MIDI 出力**: 外部 MIDI、またはユーザー自身の ROM ダンプを使う内蔵 SC-55。出力遅延とバッファを設定可能
- **多様なディスク形式**: フロッピー（.dim、.xdf、.2hd、.d88、.hdm）とハードディスク（.hdf、.hds）
- **インターレース画面モード**: フィールドを交互に描画。CRTC タイミングはゲスト側の CRTC レジスタから算出
- **メインメモリ容量**: メニューからメインメモリ容量を選択（`SWITCH.X` と同じ SRAM の設定）
- **ソフトウェア電源オフ**: ゲストから電源オフ（`$E8E00F`）すると、次のリセットまで POWER OFF 画面を表示

### ストレージ
- **2 台の FDD**: ドライブ 0 とドライブ 1 を個別に管理。ドライブごとに書き込み禁止を設定できます。ホスト側で書き込めないイメージファイルは読み取り専用でマウントします
- **SASI ハードディスク**: 本体内蔵の SASI HDD からの起動
- **SCSI ハードディスク**: 外付け CZ-6BS1 互換の SCSI 起動。バスは実行中に切替可能。Human68k のツール（`FORMAT.X`、`SWITCH.X`、`HDID.X`）がボードを認識します
- **SCSI-U（すかじーUくん）**: SCSI-U USB アダプタ経由で実機の SCSI デバイスを使用
- **ふろっぴU君（FLOPPY Ukun）**: TNB製作所の USB FDC ユニット「ふろっぴU君」経由で、実物の 5 インチフロッピーディスクを読み書き
- **HDD イメージ作成**: 空の SASI（`.hdf`）または SCSI（`.hds`、100 MB / 250 MB / 500 MB / 1 GB）イメージをアプリから作成し、Human68k 上の `FORMAT.X` で初期化
- **ディスク状態の管理**: 自動マウントモード（Disabled / Restore Last Session / Smart Load / Manual）と、状態の保存・復元・確認
- **どこからでも開ける**: サンドボックスが許すフォルダならどこにあるディスクイメージでも開けます（Finder でのダブルクリック、ドラッグ＆ドロップ、ファイル選択パネル）。iCloud 上のイメージは先にダウンロードしてから読み込みます

### 表示
- **CRT 表示パイプライン**: スキャンライン、ブルーム、ビネット、ノイズ、画面の湾曲、色収差、残像
  - プリセット: Off / Subtle / Standard / Enhanced
  - SwiftUI の専用設定パネル（⇧⌘,）
- **画面回転**: 縦画面ゲーム向けの 90 度回転（縦モード）
- **背景動画のスーパーインポーズ**: ローカルの動画ファイルを輝度キーで背景に重ねる。しきい値・柔らかさ・強さと、動画の明るさ（3 段階）を調整可能
- **スクリーンショットと録画**: 画面を PNG で保存（⇧⌘P）、または音声付きで `.mov` に録画（⇧⌘R）

### 入力
- **キーボードとマウス**: macOS ネイティブの入力に対応。VS.X のダブルクリックを安定させる SCC 互換モードあり
  - **System → US Keyboard Layout** で ANSI 配列の記号を変換します。F11〜F20 は **System → Key Remapping (F11–F20)** で、JIS・US どちらの配列でも個別に割り当てられます。割り当て先は「ろ」、ローマ字、OPT.1/2、HELP、COPY などの X68000 のキーです。**Default** は通常の動作（F12 でマウスキャプチャを解除）、**Disabled** はキーを無効にします。F12 に明示的に割り当てるとキャプチャ解除より優先されますが、⇧⌘M でのキャプチャ切替はそのまま使えます。一度も割り当てていないキーはキーごとの既定値（F13 = ろ、F14 = ローマ字、F15 = OPT.1、F11・F12・F16〜F20 は Disabled）になります。以前のバージョンで保存した割り当てはそのまま引き継がれます。設定は次回起動時にも保持されます。ジョイカードモード中のキーボード転送の制限は従来どおりです
- **X68000 マウスキャプチャ**: ⇧⌘M で切替。ポインタのワープは無効化して、キャプチャを安定させています
- **ジョイカードモード**: キーボードまたは画面上のジョイカードでジョイスティックポート 0 を操作。このモード中、文字キーはジョイカード操作だけに使われます
- **JoyportU 連携**: ATARI 仕様ジョイスティックに対応（Notify / Command モード）
- **GameController フレームワーク**: MFi / Xbox / PlayStation などの外部コントローラ
- **汎用 USB HID ジョイスティック**: X68000 Z JOYCARD など GameController が認識しないデバイスも直接読み取り、ジョイスティックポート 0 に合成

### システムと接続
- **RS-232C**: SCC（Z8530）のチャネル A としてエミュレート。ボーレートに応じた送信タイミング、3 段の受信 FIFO、CTS/DSR/DCD、割り込みに対応し、PTY、TCP サーバ、TCP クライアントに接続できます。CTS/DSR/DCD はホスト側の接続状態に連動します
- **マシンモニタ**: SwiftUI のモニタウィンドウ（**Debug → Machine Monitor**、⌥⌘M）と、外部からエミュレータを操作できる UNIX ドメインソケット（任意、後述）
- **セッション状態**: macOS のセキュアな状態復元に対応。保存したフロッピーと HDD は、App Sandbox 下でもセキュリティスコープ付きブックマークで復元します
- **iCloud 書類同期**: ROM やディスクイメージをデバイス間で同期

## 動作環境

### macOS
- macOS 12.0（Monterey）以降
- Apple Silicon または Intel Mac
- メモリ 4 GB 以上
- ROM とディスクイメージ用に 1 GB 以上の空き容量

## ROM ファイルの準備

MPX68K を動かすには、SHARP X68000 実機のシステム ROM ファイルが必要です。ROM はエミュレータに**含まれていない**ため、別途用意してください。

### 必要な ROM ファイル

X68000 実機から次の ROM ファイルを用意します。

| ファイル | 内容 | サイズ | 必須 |
|------|-------------|------|----------|
| `CGROM.DAT` | キャラクタジェネレータ ROM | 768KB | 必須 |
| `IPLROM.DAT` | IPL ROM | 128KB | 必須 |
| `IPLROM0.DAT` | SASI 時代の IPL ROM（Storage Bus Mode = SASI のとき使用） | 128KB | 任意 |
| `SCSIEXROM.DAT` | 外付け SCSI ROM（CZ-6BS1 互換、Storage Bus Mode = SCSI のとき使用） | 8KB | 任意（本来の SCSI 起動には必要） |

> **SCSI ROM の種類について**: 対応しているのは*外付け* SCSI ROM（`SCSIEXROM.DAT`、"SCSIEX" タイプ）だけです。内蔵 SCSI ROM（`SCSIINROM.DAT`、"SCSIIN" タイプ）は IOCS テーブルの配置が異なるため動作しません。

### 配置場所

ROM ファイルはサンドボックス内の Documents フォルダに置きます。

```
~/Library/Containers/NANKIN.X68000/Data/Documents/X68000/
├── README.txt          (初回起動時に自動生成)
├── CGROM.DAT           (必須)
├── IPLROM.DAT          (必須)
├── IPLROM0.DAT         (任意 — SASI 用 IPL)
└── SCSIEXROM.DAT       (任意 — 外付け SCSI ROM)
```

### 注意事項

- **権利について**: ROM ファイルの著作権は SHARP CORPORATION にあります。合法的に使用するには X68000 実機を所有している必要があります。
- **ファイル名**: ROM のファイル名は上記と完全に一致している必要があります（ファイルシステムによっては大文字・小文字も区別されます）。
- **ファイルの破損**: ROM ファイルが壊れていないか確認してください。必要なファイルがない、または不正な場合はエラーが表示されます。
- **バックアップ**: ROM ファイルのバックアップは必ず安全な場所に保管してください。

### 動作確認

ROM ファイルが正しく配置されていれば、次のようになります。
1. ROM 関連のエラーが出ずに起動する
2. X68000 特有の起動画面が表示される
3. 日本語の文字が正しく表示される

文字化けや起動失敗が起きる場合は、ROM ファイルの配置場所とファイル名を確認してください。

## インストール

### ソースからビルドする
Xcode での署名に Apple Developer アカウントが必要です。

1. **リポジトリをクローンする:**
   ```bash
   git clone https://github.com/YosAwed/MPX68K.git
   cd MPX68K
   ```

2. **Xcode で開く:**
   ```bash
   open X68000.xcodeproj
   ```

3. **ビルドする:**
   - **X68000 macOS** スキームを選択
   - ビルドして実行（⌘R）

### 依存関係
アプリは c68k CPU エミュレータの静的ライブラリにリンクします。ライブラリが見つからないというエラーが出る場合は、先に `c68k/c68k.xcodeproj` の `c68k mac` スキームをビルドしてください（[ビルド](#ビルド)を参照）。

## 使い方

### 起動ディスクがない場合

セッション復元後に起動ディスクがマウントされていないと、FDD 0、FDD 1、HDD のどれを選ぶかを尋ねます。どちらかのダイアログをキャンセルすると、ウィンドウにメッセージと **ディスクを選択…** ボタンが表示されます。そこから選び直すか、FDD / HDD メニューやドラッグ＆ドロップを使ってください。ディスク選択のエラーは再試行ボタンとともに表示されたままになります。起動時のリセットは一定時間後ではなく、メディアがマウントされてから行われます。起動後の通常のディスク入れ替えではこのリセットは起きません。

### macOS メニュー一覧

#### FDD メニュー
- **Open Drive 0…** / **Open Drive 1…**: フロッピーイメージを挿入
- **Eject Drive 0** / **Eject Drive 1**: 挿入中のフロッピーを取り出す
- **Write-Protect Drive 0** / **Write-Protect Drive 1**: ディスクの書き込み禁止ノッチを切替（取り出すと解除され、ディスク状態とともに保存されます）
- **FLOPPY Ukun → Connect / Disconnect**: フロッピーコントローラを接続中のふろっぴU君に任せ、エミュレートしている X68000 から実物のドライブを使います。Disconnect でディスクイメージに戻ります。どちらもシステムをリセットします
- **FLOPPY Ukun → Test Connection**: ユニットの 2 つの USB シリアルポートを開いて識別できるか確認

#### HDD メニュー
- **Open Hard Disk…** / **Eject Hard Disk**: HDD イメージをマウント・取り外し
- **Create Empty HDD…**: 空の `.hdf` イメージを作成（あとで Human68k 上の `FORMAT.X` で初期化）
- **Save HDD**（⇧⌘S）: 現在の HDD の状態を保存
- **Storage Bus Mode → SASI / SCSI / SCSI-U**: ハードディスクのバスを実行中に切替
- **SCSI Devices → Open SCSI (ID 0)… / Eject SCSI (ID 0)**: 外付け SCSI デバイスの管理
- **SCSI Devices → Create Empty SCSI HDD…**: 空の `.hds` イメージ（100 MB〜1 GB）を作成し、SCSI ID 0 としてマウントしてリセット。空のイメージのときはフロッピーから起動するので、そこで `FORMAT.X` を実行できます
- **SCSI-U Device → Connect SCSI-U / Disconnect SCSI-U**: SCSI-U USB アダプタに接続・切断。ステータス行に接続状態が表示されます

#### Clock メニュー
- **1 / 10 / 16 / 24 (Default) / 40 / 50 MHz**（⌃1〜⌃6）: CPU クロックを変更
- **Normal Speed / Turbo 2x–5x / No-Wait**: エミュレーション速度（次回起動時には保持されません）

#### Display メニュー
- **Rotate Screen**（⌘R）: 横画面と縦画面を切替
- **Landscape Mode** / **Portrait Mode**: 向きを直接指定
- **Take Screenshot…**（⇧⌘P）: 現在の画面を PNG で保存
- **Start Recording…** / **Stop Recording**（⇧⌘R）: 画面と音声を `.mov` に録画
- **CRT Settings…**（⇧⌘,）: SwiftUI の CRT 設定パネルを開く
- **CRT Display Mode → Off / Subtle / Standard / Enhanced**: CRT プリセットをすばやく選択
- **Background Video → Set Video File… / Remove Video**: 背景動画を選択・解除
- **Background Video → Superimpose**: 輝度キー合成のオン・オフ
- **Background Video → Threshold / Softness / Intensity**: 輝度キーの微調整
- **Background Video → Video Brightness → High / Medium / Low**: 背景動画の明るさを 100% / 60% / 30% に

#### System メニュー
- **Reset System**: エミュレータをハードリセット
- **Use MPX68K Mouse**（⇧⌘M）: X68000 マウスのキャプチャを切替
- **Joycard Mode**: キーボード入力とジョイカード入力を切替
- **US Keyboard Layout**: ANSI 配列の記号キーを変換
- **Key Remapping (F11–F20)**: F11〜F20 に X68000 のキーを割り当て
- **MIDI Output → External MIDI / Internal SC-55 / Choose SC-55 ROM Folder… / SC-55 Volume… / Test SC-55 Sound**: MIDI の出力先の選択と設定
- **MIDI Output Delay…**: MIDI 出力の遅延を設定（どちらの出力先にも適用）
- **Main Memory**: メインメモリ容量を設定（`SWITCH.X` と同じ設定。リセット後に反映）
- **Delete IPLROM.DAT…**: キャッシュされた IPL ROM を削除
- **Serial Communication → Mouse Only (Default) / PTY (Terminal Access) / TCP Connection… / TCP Server… / Disconnect**: RS-232C の接続先を選択
- **Serial Communication → Original Mouse SCC (Compat)**: VS.X のダブルクリック用の SCC 互換モードを有効化
- **Disk State Management → Auto-Mount Mode**: Disabled / Restore Last Session / Smart Load / Manual Selection
- **Disk State Management → Save Current State（⌥⌘S）/ Clear Saved State / Show State Information**
- **JoyportU Settings → Disabled / Notify Mode / Command Mode**

#### Debug メニュー
- **Machine Monitor**（⌥⌘M）: メモリとレジスタを確認・編集するモニタウィンドウを開く

#### ジョイカード入力
**Joycard Mode** では次のキーを使います。
- **矢印キー** または **WASD**: 8 方向の移動
- **Space** または **J**: A ボタン
- **Z** または **K**: B ボタン
- 画面上のジョイカードもクリックで操作でき、A/B は X68000 のトリガー A/B に対応します

実物のゲームコントローラ（GameController と汎用 USB HID ジョイスティック）は、入力モードに関係なくジョイスティックポート 0 を操作します。

## 内蔵 SC-55 MIDI 音源

**System → MIDI Output → Choose SC-55 ROM Folder…** を選び、ユーザー自身が吸い出した**オリジナルの SC-55（mk1）**の ROM ダンプが入ったフォルダを指定します。フォルダを読み込むと内蔵音源が有効になり、**Test SC-55 Sound** で中央のド（C4）が鳴ります。外部 MIDI 機器や仮想 MIDI ケーブルは不要です。**External MIDI** を選ぶと外部出力に戻ります。フォルダは macOS のセキュリティスコープ付きブックマークで記憶され、次回起動時に復元されます。フォルダを移動したりアクセスできなくなったりした場合は、選び直してください。

| ファイル | 必要なサイズ |
| --- | --- |
| `sc55_rom1.bin` | 32,768 バイト |
| `sc55_rom2.bin` | 262,144 または 524,288 バイト |
| `sc55_waverom1.bin` | 1,048,576 バイト |
| `sc55_waverom2.bin` | 1,048,576 バイト |
| `sc55_waverom3.bin` | 1,048,576 バイト |

MCU、プログラム、波形 ROM は対応する組み合わせを使ってください。上流のコアは SC-55 ファームウェア 1.00、1.21、2.00 に対応していますが、この統合で使えるのは現在 mk1 モデルだけで、SC-55mkII、SC-55ST などの Sound Canvas には対応していません。ファイルサイズと空データのチェックでよくある間違いは検出できますが、ファームウェアが本物であることや互換性を保証するものではありません。MPX68K が ROM を同梱・ダウンロードしたり、アプリ内にコピーしたりすることはありません。

既存の **MIDI Output Delay…** は内蔵音源にも適用されます。MIDI と FM がずれて聞こえる場合は調整してください。**System → MIDI Output → SC-55 Volume…** を選ぶと、内蔵 SC-55 だけの音量スライダーが開き、その場で調整できます（0% で消音、100% で本来の出力レベル）。設定は保存され、アプリや音源のリセット後も保持されます。全体の音量は Mac の音量で調整してください。

内蔵 SC-55 の MIDI 遅延は画面更新とは独立にスケジュールされ、リズムの揺れを抑えるため 24 ms のオーディオバッファを使います。**MIDI Output Delay…** は引き続き、MIDI を FM/ADPCM に合わせるための固定オフセットです。

システムをリセットすると内蔵 SC-55 も再起動します。
この音源は別のオーディオエンジンを使うため、既存の FM/ADPCM 録音タップには今のところ含まれません。

実装には [Nuked SC-55](https://github.com/nukeykt/Nuked-SC55)（GPL-2.0-or-later）を使用しています。`ATTRIBUTION.md` と `X68000 Shared/SC55/README.md` を参照してください。

## マシンモニタ

**Debug → Machine Monitor**（⌥⌘M）でウィンドウを開くと、ソフトウェアの開発やデバッグ中にメモリと CPU レジスタを確認・編集できます。

### マシンモニタソケット

マシンモニタソケットは、エミュレータの内部状態を UNIX ドメインソケットで公開します。スクリプト、テストハーネス、デバッガなどの外部ツールから、GUI を使わずにエミュレーションの一時停止・再開、メモリの読み書き、CPU レジスタの確認、ディスクイメージの入れ替えができます。

ソケットは**既定では無効**です。有効にするには、アプリを起動する前に `monitorSocketEnabled` のユーザーデフォルトを設定します。

```bash
defaults write NANKIN.X68000 monitorSocketEnabled -bool YES
```

無効に戻すには:

```bash
defaults delete NANKIN.X68000 monitorSocketEnabled
```

有効にすると、起動時にモード `0600` でソケットが作られ、終了時に削除されます。既定のパスは `$HOME/mpx68k_monitor.sock` です。サンドボックス化されたアプリでは、`$HOME` はアプリコンテナの `Data` ディレクトリになります。起動時の環境変数 `MPX68K_MONITOR_SOCK` でパスを変更できます。実際のパスはログに出力されます。
```text
[MPX68K] Machine Monitor socket: /Users/name/Library/Containers/NANKIN.X68000/Data/mpx68k_monitor.sock
```

#### プロトコル

プロトコルは、ストリーム型 UNIX ソケット上の行単位の UTF-8 です。1 行（`\n` 終端）が 1 リクエストで、レスポンスは成功なら `OK`、失敗なら `ERROR <メッセージ>` で終わります。

| コマンド | 内容 |
|---------|-------------|
| `DIAG` | 一時停止せずに起動状態のスナップショットを読む |
| `PAUSE` | エミュレーションを一時停止（書き込み前に必要） |
| `RESUME` | エミュレーションを再開 |
| `STATUS` | `PAUSED` または `RUNNING` を返す |
| `REGS` | D0〜D7、A0〜A7、PC、SR を 16 進で表示（`PAUSE` が必要） |
| `SETPC <addr>` | PC を設定（`PAUSE` が必要） |
| `SETD/SETA <n> <val>` | データ／アドレスレジスタを設定（`PAUSE` が必要） |
| `SETSR <val>` | SR を設定（`PAUSE` が必要） |
| `TRACE [n]` | 命令をステップ実行して PC とオペコードを表示（`PAUSE` が必要） |
| `TRACER [n]` | D0/D1/A0/A1 付きの `TRACE`（`PAUSE` が必要） |
| `STEPTO <pc> [maxsteps]` | PC が指定値に達するか上限までステップ実行（`PAUSE` が必要） |
| `READ <addr> <n>` | `addr` から `n` バイトを 16 進ダンプ（`PAUSE` が必要） |
| `READB/READW/READD <addr>` | バイト／16 ビットワード／32 ビットロングワードを読む（`PAUSE` が必要） |
| `WRITE <addr> <b0> [b1…]` | バイトを書き込む（`PAUSE` が必要） |
| `WRITEW/WRITED <addr> <val>` | ワード／ロングワードを書き込む（`PAUSE` が必要） |
| `HW [section]` | ハードウェア状態のスナップショット（`PAUSE` が必要） |
| `MOUNTFDD <drive> <path>` | FDD 0 または 1 のディスクイメージを入れ替える（`PAUSE` が必要） |
| `EJECTFDD <drive>` | FDD 0 または 1 のイメージを取り出す（`PAUSE` が必要） |
| `HELP` | コマンド一覧を表示 |
| `QUIT` | 接続を閉じる |

アドレスと値はすべて 16 進数です。簡単な例:

```console
$ nc -U /Users/name/Library/Containers/NANKIN.X68000/Data/mpx68k_monitor.sock
PAUSE
OK
READD 00ed0018
00000080
OK
RESUME
OK
QUIT
OK
```

サーバが同時に扱うクライアントは 1 つです。`DIAG` はエミュレーション実行中に同期したスナップショットを読みます。CPU、メモリ、デバイスの実際の状態にアクセスするコマンドは、データ競合を避けるため先に `PAUSE` が必要です。

#### FDD のディスク入れ替え

`MOUNTFDD` と `EJECTFDD`（`PAUSE` の後に実行）を使うと、GUI に触れずにスクリプトからどちらのドライブにもフロッピーイメージを入れ替えられます。複数枚組のゲームを自動化したり、テストケースごとに別のディスクイメージが必要なテストを実行したりするのに便利です。ドライブ番号は `0` または `1`、パスはエミュレータが対応する形式（`.dim`、`.xdf`、`.2hd`、`.d88`、`.hdm`）のディスクイメージの絶対パスです。

```text
PAUSE
OK
MOUNTFDD 0 /Users/you/Documents/X68000/disk1.xdf
OK
EJECTFDD 0
OK
RESUME
OK
```

#### ソケット機能のビルド

ソケットサーバは `X68000 Shared/px68k/x11/winx68k.cpp` に組み込まれていて、このファイルはすでに "X68000 macOS" ターゲットに含まれています。追加のビルド手順は不要で、通常どおりビルドしたあと `defaults write` で有効にするだけです。

## ファイル形式

| 拡張子 | 内容 | 種類 | 備考 |
|-----------|-------------|------|-------|
| `.dim` | 標準ディスクイメージ | フロッピー | X68000 で最も一般的な形式 |
| `.xdf` | 拡張ディスク形式 | フロッピー | 拡張容量 |
| `.2hd` | 2HD のベタイメージ | フロッピー | XDF 互換のベタイメージとして読み込み |
| `.d88` | D88 ディスクイメージ | フロッピー | レトロ PC エミュレータで一般的な形式 |
| `.hdm` | Human68k ディスクイメージ | フロッピー | 旧来の Human68k 形式 |
| `.hdf` | 汎用ハードディスクイメージ | ハードディスク | SASI / 汎用 HDD |
| `.hds` | SCSI ハードディスクイメージ | ハードディスク | SCSI HDD。開く・ドロップすると Storage Bus Mode が自動で SCSI に切り替わります |

### ハードディスクの使い方

ハードディスクイメージは、フロッピーよりアクセスが速く容量も大きくなります。
HDD イメージはこのアプリで作成できますが、エミュレータ上の Human68k で `FORMAT.X` を実行して初期化する必要があります。

1. **HDD イメージの読み込み**: **HDD → Open Hard Disk...** メニューを使うか、`.hdf` / `.hds` ファイルをドラッグします（SCSI イメージは **HDD → SCSI Devices → Open SCSI (ID 0)…** からも開けます）
2. **速度**: 大きなアプリケーションの読み込みが大幅に速くなります
3. **容量**: 大規模なソフトウェア一式も入る大きなイメージに対応します
4. **保存**: **HDD → Save HDD**（⇧⌘S）で、メモリ上の HDD イメージをディスクに書き戻します

#### 起動順序
多くのエミュレータと違い、MPX68K は X68000 本来の起動手順を再現しているため、単純に「フロッピーが常に優先」ではありません。起動デバイスは SRAM の `$ED0018` で決まります。これは実機で `SWITCH.X` が書き込むのと同じ場所で、MPX68K は最後に保存した値を保持します。具体的には次のとおりです。

1. **SASI モード、SRAM が既定値（`$ED0018 = $00`、SWITCH.X の `STD`）**:
   - まず FDD ドライブ 0 を調べ、起動可能なフロッピーが入っていればフロッピーから起動します。
   - なければ SASI HDD から起動します。
2. **SASI モード、SRAM が HDD 起動（`$ED0018 = $80`、`SWITCH.X` で Boot Device = `HD` にした場合など）**:
   - ドライブ 0 にフロッピーが入っていても HDD から先に起動します。これは X68000 本来の動作です。フロッピーから起動するはずなのに HDD から起動してしまう場合は、`SWITCH.X` の設定を確認してください。
   - 安全策として、SASI イメージに有効なブートセクタがない場合、そのリセットでは自動的にフロッピー起動に切り替えます（ログに *"SASI boot bypass"* と出ます）。
3. **SCSI モード（Storage Bus Mode → SCSI）**:
   - リセット時に `$ED0018 = $80` に設定し、SCSI HDD を優先します。IPL ROM が通常どおり初期化を行い、適切なタイミングで `SCSI_InjectBoot` が割り込んで SCSI デバイスドライバに処理を渡します。
   - SCSI モードでフロッピーから起動したい場合は、先に **Storage Bus Mode → SASI** に切り替える（または SCSI デバイスを取り外す）必要があります。

MPX68K は SASI モードの SRAM の状態をリセット後も保持するため（ログに `"SASI SRAM restored (SWITCH.X preserved)"` と出ます）、起動優先順位の設定で `SWITCH.X` を実行するのは一度だけで済みます。

#### おすすめの使い方
- **HDD からのシステム起動**: Human68k 上で一度 `SWITCH.X` を実行して Boot Device を `HD` にし、再起動します。以降は自動的に HDD から起動します。
- **アプリケーション**: 大きなソフトウェアは HDD に入れておく
- **開発**: コンパイラや開発ツールは HDD に置く
- **ゲーム**: 複数枚組のゲームを HDD にまとめる
- **起動の上書き**: `$ED0018 = $00`（SWITCH.X = `STD`）なら、HDD をマウントしていてもドライブ 0 にシステムフロッピーを入れればフロッピーから起動できます

### 画面回転（縦モード）

縦画面のゲーム向けに、画面を 90 度回転できます（いわゆる「縦置き」）。

#### 対応ゲーム
- **ドラゴンスピリット**: 縦画面が必要な定番の縦スクロールシューティング
- **その他の縦画面ゲーム**: 縦画面でのプレイを前提とした X68000 のゲーム全般

#### 使い方
1. **画面回転**: **Display → Rotate Screen**（⌘R）で向きを切替
2. **直接指定**: 縦画面のゲームでは **Display → Portrait Mode** を選択
3. **ウィンドウの調整**: macOS が表示に合わせてウィンドウサイズを自動で変更します
4. **操作の一貫性**: 回転してもジョイカードの操作は変わりません

#### 技術的な特徴
- **滑らかな回転**: SpriteKit による回転で、アスペクト比を正しく保ちます
- **設定の保持**: 回転の設定は保存され、次回起動時に復元されます
- **最適なスケーリング**: 画面いっぱいに表示されるよう自動で拡大縮小します
- **性能への影響なし**: 回転処理はエミュレーション速度に影響しません

## アーキテクチャ

MPX68K は、Swift の UI フレームワークと C/C++ の低レベルなエミュレーションコードをつなぐ多層構造になっています。

### 使用言語
- **Swift**: UI 層（AppKit のメニュー、SpriteKit、SwiftUI のパネル）、デバイス管理、ファイルシステム、ログ基盤
- **C**: エミュレーションの中核（CPU、ハードウェア、メモリ管理）
- **C++**: 音源（fmgen）、内蔵 SC-55（Nuked SC-55）、一部のエミュレーション部品
- **ブリッジングヘッダ**で C の API を Swift に公開

### 主なコンポーネント
- **GameScene.swift**: SpriteKit によるメインのエミュレーション画面
- **FileSystem.swift**: 安全なファイル処理とディスクイメージの管理
- **X68Logger.swift**: カテゴリ別のログ機能
- **X68Security.swift**: 入力の検証とセキュリティ関連の処理
- **JoyController.swift**: GameController と汎用 USB HID ジョイスティックの統合
- **X68JoyCard.swift**: 仮想ジョイカードの実装
- **SCCManager.swift**: シリアル（RS-232C）の接続先の選択
- **MIDIController.swift** / **SC55/**: 外部 MIDI と内蔵 SC-55 音源
- **CRT/** / **Superimpose/**: CRT 表示パイプラインと背景動画
- **MonitorView.swift**: マシンモニタのウィンドウ（macOS）
- **px68k/**: C/C++ による X68000 ハードウェアエミュレーション一式
- **c68k/**: 独立した M68000 CPU エミュレータ（静的ライブラリ）

図を含む詳しいアーキテクチャの説明は [ARCHITECTURE.md](ARCHITECTURE.md)（英語）を参照してください。

## 更新履歴

### バージョン 4.8
- **ふろっぴU君**: TNB製作所のふろっぴU君で実物のフロッピードライブを使用（XEiJ+t の FUK.java を作者の許可を得て移植）
- **68000 の正確さ**: SingleStepTests/680x0 のテストベクタで見つかった CHK、NEGX.l、ROXR/ROXL.l、32 ビットの ASL.l、UNLK A7、DIVS/DIVU のオーバーフローの不具合を修正
- **背景動画の明るさ**: High / Medium / Low
- **ファンクションキー**: 未割り当てのキーにキーごとの既定値。保存済みの割り当ては維持
- **画面とデバイス**: 特殊プライオリティのパレット読み出し、CRTC の動作ポート、FDC の READ ID、MFP の RSR バッファフルを修正。`SCSIEXROM.DAT` を SCSIEX シグネチャで検証
- **起動ディスクの確認画面**: メニュー、ドラッグ＆ドロップなどでメディアがマウントされたら閉じるように

### バージョン 4.7
- **メインメモリのメニュー**: 12 MB に固定せず、`SWITCH.X` と共通のメインメモリ容量設定に
- **ディスクの読み込み**: サンドボックスが許すフォルダならどこからでも開けるように。iCloud 上のイメージは先にダウンロードし、失敗したら知らせる
- **ADPCM**: 再生中の PLAY でデコーダをリセットし、ミックスから直流成分を除去
- **画面**: 特殊プライオリティと半透明が、すべてのモードで R2 のビット 10 に従うように
- **RS-232C**: RS-232C ポートをマウス用とは別の SCC チャネル A として実装。ボーレートに応じた送信タイミング、受信 FIFO、CTS/DSR/DCD、割り込みに対応。PTY と TCP の接続先はこのポートを使い、CTS/DSR/DCD はホスト側の接続状態に連動します
- **68000 バスエラー**: 応答のないバスアクセス（2 枚目の MIDI ボードの検出など）でバスエラーを発生させるようにしました。これで EXACT の SMIDRV が動き、ジオグラフシールが起動します
- **US Keyboard Layout と Key Remapping (F11–F20)**: ANSI 配列の記号変換と、キー配列によらないファンクションキーの割り当て
- **修正**: macOS での XDF のファイルハンドルを正しく復元

### バージョン 4.6
- **汎用 USB HID ジョイスティック**: GameController が認識しない X68000 Z JOYCARD などが使えるように
- **ジョイカードモード**: チェック付きのメニュー項目に変更。ジョイカードモード中は文字キーがジョイカード操作だけに使われます
- **修正**: App Sandbox 下で保存したフロッピーが再び復元されるように

### バージョン 4.5
- **正確な CPU クロック**: CPU が本来のクロックで動くように修正（以前はどの設定でも約 5 倍速で動いていました）。**Turbo 2x〜5x** と **No-Wait** を追加
- ドライブごとの **FDD 書き込み禁止**と、書き込めないイメージファイルの自動読み取り専用マウント
- **ソフトウェア電源オフ**画面、`$E8E00B` での機種判別、ILLEGAL 命令でスタックに積む PC の修正（MX68K から移植）
- **SCSI の初期設定**: **Create Empty SCSI HDD…**、空の SCSI イメージ使用時のフロッピー起動、`FORMAT.X` / `SWITCH.X` / `HDID.X` による外付けボードの認識
- **リリース**: `tools/release/make-dmg.sh` でドラッグしてインストールする DMG を作成

### バージョン 4.4
- **内蔵 SC-55**: ユーザー自身の SC-55 ROM による MIDI 音源。音量設定の保存とタイミングの揺れの低減（前述）
- **修正**: 起動メディア選択のキャンセルと読み込み中の表示、画面上のジョイカードの A/B の割り当て、アポストロフィ・コロン・セミコロンのキー
- 常時出力のトレースログを無効化。使っていない swift-crypto パッケージを削除

### バージョン 4.3
- **`.2hd` フロッピーイメージ**
- **CRTC タイミング**: CRTC レジスタからフィールドごとのタイミングを算出、水平フロントポーチ中のラスターコピー、インターレースモードのフィールド交互描画
- **スクリーンショットと録画**: PNG のスクリーンショットと音声付き `.mov` 録画
- **マシンモニタ**: モニタウィンドウと、FDD の入れ替えもできる UNIX ソケットインターフェース
- **SCSI-U（すかじーUくん）**のストレージバスモード
- **JoyportU** のコマンドモードの読み出しプロトコル
- ROM の内容の検証とエラーダイアログ。ホスト上で動くコアのユニットテストと GitHub Actions による CI

### バージョン 4.2

#### 新機能
- **SASI / SCSI の切替起動**: ハードディスクのバスを実行中に SASI と SCSI で切替。SCSI 起動は IPL ROM を先に動かす方式で、外付け CZ-6BS1 ROM を `$EA0000-$EA1FFF` に配置
- **CRT 表示パイプライン**: スキャンライン、ブルーム、ビネット、ノイズ、画面の湾曲、色収差、残像のシェーダー一式。SwiftUI の専用設定パネル（⌘,）と Off / Subtle / Standard / Enhanced のプリセット
- **背景動画のスーパーインポーズ**: ローカルの動画ファイルを輝度キーで背景に重ねる。しきい値・柔らかさ・強さを調整可能
- **CPU クロック切替**: Clock メニューで 1 / 10 / 16 / 24 / 40 / 50 MHz を選択
- **シリアル通信**: マウスのみ（既定）、PTY による端末接続、TCP クライアント、TCP サーバ
- **遅延設定付き MIDI 出力**: 外部 MIDI または内蔵 SC-55 への出力。遅延とバッファを設定可能
- **JoyportU 対応**: ATARI 仕様ジョイスティックとの連携（Notify / Command モード）
- **ディスク状態の管理**: 自動マウントモード（Disabled / Restore Last Session / Smart Load / Manual）と、状態の保存・復元・確認
- **HDD イメージ作成**: 空の `.hdf` イメージをアプリで作成し、Human68k 上の `FORMAT.X` で初期化
- **ディスク形式の追加**: `.d88` と `.hdm` のフロッピーイメージに対応

#### 修正と改善
- **マウスの安定性**: キャプチャの安定化、Y 軸反転の修正、ドリフトと慣性の調整、VS.X のダブルクリック用 SCC 互換モード
- **描画**: 変化のないフレームをスキップして GPU 負荷を軽減
- **音声**: ADPCM / OPM のリングバッファによるミキシングを作り直し、ADPCM のタイミングを改善
- **ビルド**: macOS のデプロイメントターゲットを 12.0 に引き下げ、Intel Mac 向けの x86_64 を再び有効化
- **セキュアな状態復元**: macOS で有効化
- **コンパイラ警告**: 使われていない古い SCSI 補助コードを削除し、警告ゼロを維持

## 開発

### プロジェクト構成
```
MPX68K/
├── X68000 Shared/          # 共通の Swift コードとエミュレーションコア
│   ├── px68k/              # C/C++ のエミュレーションコア
│   │   ├── x68k/           # X68000 のハードウェア部品
│   │   ├── fmgen/          # FM 音源（C++）
│   │   ├── m68000/         # CPU ラッパー
│   │   └── x11/            # プラットフォーム抽象化層
│   ├── *.swift             # Swift の UI とロジック
│   ├── X68Logger.swift     # ログ機能
│   ├── X68Security.swift   # セキュリティと検証
│   └── FileSystem.swift    # 安全なファイル管理
├── X68000 macOS/           # macOS 固有のコード
│   ├── AppDelegate.swift   # メニュー
│   └── GameViewController.swift # メインのビューコントローラ
├── c68k/                   # M68000 CPU エミュレータ（静的ライブラリ）
│   └── c68k.xcodeproj      # 独立したビルド設定
├── tests/                  # ホスト上で動くユニットテスト（コア、キーボード、SC-55）
├── tools/                  # 自己起動するテストディスクと DMG 作成スクリプト
├── CLAUDE.md               # 開発ガイドライン
├── ARCHITECTURE.md         # 詳しいアーキテクチャ資料
├── LICENSE                 # 全コンポーネントのライセンス概要
└── ATTRIBUTION.md          # 上流プロジェクトのクレジット
```

### ビルド

#### 必要なもの
- Xcode 26 以降（プロジェクトは Xcode のプロジェクト形式 90 を使用）
- デプロイメントターゲットは macOS 12.0（Monterey）以降

#### ビルドコマンド
```bash
# 先に C68K の静的ライブラリをビルド
xcodebuild -project c68k/c68k.xcodeproj -scheme "c68k mac" -configuration Debug

# クリーンビルド
xcodebuild -project X68000.xcodeproj -scheme "X68000 macOS" -configuration Debug clean build

# リリースビルド
xcodebuild -project X68000.xcodeproj -scheme "X68000 macOS" -configuration Release build

# 配布用アーカイブ
xcodebuild -project X68000.xcodeproj -scheme "X68000 macOS" -configuration Release archive

# ユニバーサルのリリースビルドを、ドラッグでインストールする DMG にまとめる（zip と SHA256SUMS.txt も作成）
tools/release/make-dmg.sh
```

#### テスト
```bash
# エミュレーションコアのユニットテスト（C コンパイラがあればどの環境でも可。CI は Linux で実行）
make -C tests/core
```
CI（GitHub Actions）は、push とプルリクエストのたびに Linux でコアのテストを実行し、macOS アプリをビルドします。

#### 依存関係のビルド順
1. **C68K ライブラリ**: `c68k mac` スキームをビルド（`libc68k_mac.a` / `libc68k_mac_DEBUG.a` ができます）
2. **本体**: C68K の静的ライブラリにリンク

#### コード品質
- **コンパイラ警告ゼロ**: 警告なしでビルドできる状態を維持
- **メモリ安全性**: 境界チェックを徹底
- **スレッド安全性**: 競合状態を防止
- **性能の計測**: 性能監視用のログを内蔵

## コントリビュート

1. リポジトリをフォークする
2. 機能ブランチを作る
3. 変更を加える
4. `make -C tests/core` を実行し、macOS アプリをビルドする
5. プルリクエストを送る

開発ガイドラインとアーキテクチャの詳細は `CLAUDE.md`（英語）を参照してください。

## ライセンス

ライセンスの概要は [LICENSE](LICENSE)、上流プロジェクトのクレジットの詳細は [ATTRIBUTION.md](ATTRIBUTION.md) を参照してください。

# サードパーティについて

- **px68k**（hissorii/px68k）— 上流のエミュレータコア。Kenjo 氏の WinX68k（けろぴー）から派生しています。
- **C68K**（Stephane Dallongeville 氏）— MC68000 CPU エミュレータ。GPL-2.0-or-later（`LICENSES/GPL-2.0.txt` を参照）。
- **fmgen**（cisc 氏）— FM/PSG 音源の実装。
- **FUK.java**（TNB製作所、XEiJ+t より）— ふろっぴU君への対応。作者の許可を得て C に移植しました（`X68000 Shared/px68k/x68k/fuk.c`）。

SHARP の ROM は配布していません。合法的に所有している ROM をご自身で用意してください。

## 謝辞

- **px68k**: オリジナルの X68000 エミュレータコア
- **C68K**: M68000 CPU エミュレータ
- **fmgen**: FM 音源ライブラリ
- **TNB製作所（たんぼ氏）**: ふろっぴU君、XEiJ+t の FUK.java の移植の許可、そしてユニットの正しい動かし方を教えていただいたことに
- **SHARP**: オリジナルの X68000

## リンク

- [GitHub リポジトリ](https://github.com/YosAwed/MPX68K)
- [Issue・不具合報告](https://github.com/YosAwed/MPX68K/issues)

---

*SHARP X68000 は SHARP CORPORATION の商標です。本プロジェクトは SHARP CORPORATION とは関係ありません。*
