# MPX68K ダブルクリック不具合の詳細分析

## 問題の核心

オリジナル版とMPX68K版の**最も重要な違い**は、`SCC_Write()`関数内のマウスデータ生成条件にある。

### オリジナル版の動作
```c
if ( (!(SCC_RegsB[5]&2))&&(data&2)&&(SCC_RegsB[3]&1)&&(!SCC_DatNum) )
{
    Mouse_SetData();
    SCC_DatNum = 3;
    SCC_Dat[2] = MouseSt;
    SCC_Dat[1] = MouseX;
    SCC_Dat[0] = MouseY;
}
```

条件:
1. `!(SCC_RegsB[5]&2)` - 前回のRTSが0
2. `(data&2)` - 今回のRTSが1（RTS立ち上がりエッジ）
3. `(SCC_RegsB[3]&1)` - 受信有効
4. **`(!SCC_DatNum)` - 前回のデータが完全に読み取られている**

### MPX68K版の動作
```c
int rtsRising = (!(SCC_RegsB[5]&2)) && (data&2);
int rxEnabled = (SCC_RegsB[3]&1);
if (rtsRising && rxEnabled) {
    Mouse_SetData();  // ← ここで常に呼ばれる
    if (!SCC_DatNum) {
        SCC_DatNum = 3;
        SCC_Dat[2] = MouseSt;
        SCC_Dat[1] = (BYTE)MouseX;
        SCC_Dat[0] = (BYTE)MouseY;
    } else {
        SCC_MouseQEnqueue(MouseSt, (BYTE)MouseX, (BYTE)MouseY);
    }
}
```

条件:
1. `!(SCC_RegsB[5]&2)` - 前回のRTSが0
2. `(data&2)` - 今回のRTSが1（RTS立ち上がりエッジ）
3. `(SCC_RegsB[3]&1)` - 受信有効
4. **`(!SCC_DatNum)` チェックは`Mouse_SetData()`の後**

## ダブルクリックが失敗する理由

### シナリオ: 高速なダブルクリック

X68000のマウスドライバは、通常以下のようにポーリングする:
1. RTSを立ち上げる
2. マウスデータを読み取る（3バイト: Status, X, Y）
3. RTSを下げる
4. 一定時間待つ
5. 再び1に戻る

ダブルクリックの場合:
- 1回目のクリック（ボタンダウン）
- ボタンアップ
- 2回目のクリック（ボタンダウン）
- ボタンアップ

これらのイベントが非常に短時間で発生する。

### オリジナル版の動作（正常）

**ポーリング1回目**（1回目のボタンダウン後）:
1. RTS立ち上がり
2. `(!SCC_DatNum)` = true
3. `Mouse_SetData()` 呼び出し
   - `MouseDX = 0, MouseDY = 0`（蓄積された移動量）
   - `MouseSt = 1`（ボタン1が押されている）
   - `MouseDX`, `MouseDY` をクリア
4. `SCC_Dat[2] = 1, SCC_Dat[1] = 0, SCC_Dat[0] = 0`
5. X68000側が3バイト読み取る

**ポーリング2回目**（ボタンアップ後）:
1. RTS立ち上がり
2. `(!SCC_DatNum)` = true（前回のデータは読み取り済み）
3. `Mouse_SetData()` 呼び出し
   - `MouseDX = 0, MouseDY = 0`
   - `MouseSt = 0`（ボタンが離されている）
   - `MouseDX`, `MouseDY` をクリア
4. `SCC_Dat[2] = 0, SCC_Dat[1] = 0, SCC_Dat[0] = 0`
5. X68000側が3バイト読み取る

**ポーリング3回目**（2回目のボタンダウン後）:
1. RTS立ち上がり
2. `(!SCC_DatNum)` = true
3. `Mouse_SetData()` 呼び出し
   - `MouseDX = 0, MouseDY = 0`
   - `MouseSt = 1`（ボタン1が再び押されている）
   - `MouseDX`, `MouseDY` をクリア
4. `SCC_Dat[2] = 1, SCC_Dat[1] = 0, SCC_Dat[0] = 0`
5. X68000側が3バイト読み取る

→ X68000側は、短時間に2回の`MouseSt = 1`を検出できる

### MPX68K版の動作（不具合）

**問題のケース: ポーリングよりも速いクリック**

もし、ユーザーのダブルクリックが非常に速く、X68000のポーリング間隔よりも短い場合:

**時刻T0**: 1回目のボタンダウン
- `Mouse_Event()` で `MouseStat |= 1`

**時刻T1**: 1回目のボタンアップ
- `Mouse_Event()` で `MouseStat &= 0xfe` → `MouseStat = 0`

**時刻T2**: 2回目のボタンダウン
- `Mouse_Event()` で `MouseStat |= 1` → `MouseStat = 1`

**時刻T3**: X68000がRTSを立ち上げる（最初のポーリング）
- `Mouse_SetData()` 呼び出し
  - `MouseDX = 0, MouseDY = 0`
  - `MouseSt = MouseStat = 1`（**現在の状態のみ**）
  - `MouseDX`, `MouseDY` をクリア

この時点で、**1回目のクリックと2回目のクリックの間のボタンアップイベントが失われている**！

### なぜMPX68K版で問題が顕在化するのか？

オリジナル版では、`(!SCC_DatNum)` 条件により、**前回のデータが読み取られるまで新しいデータを生成しない**。これにより、イベントの順序が保持される。

MPX68K版では、`Mouse_SetData()` が常に呼ばれるが、**`Mouse_SetData()` は現在の蓄積状態をスナップショットするだけ**で、イベントの履歴を保持しない。

さらに、MPX68K版の`Mouse_SetData()`には以下のコードがある:
```c
if (x == 0 && y == 0 && MouseSt == LastMouseSt) {
    return;  // 変化がなければ送らない
}
```

このコードは、**同じボタン状態が連続する場合にデータを送らない**ようにしている。しかし、これは**ダブルクリックの検出を妨げる可能性がある**。

### 具体的な問題シナリオ

**ケース1: 高速ダブルクリック + 低速ポーリング**

1. ユーザーが高速でダブルクリック（ボタンダウン→アップ→ダウン→アップ）
2. X68000が1回目のポーリング
   - `Mouse_SetData()` 呼び出し
   - この時点で`MouseStat`が最終状態（例: 0）になっている
   - 中間のイベント（ダウン→アップ→ダウン）が失われる

**ケース2: Mouse_SetData()の呼び出しタイミング**

オリジナル版:
- `(!SCC_DatNum)` 条件により、X68000がデータを読み取った**後**にのみ`Mouse_SetData()`が呼ばれる
- これにより、各ポーリングサイクルで1つのマウス状態が確実に取得される

MPX68K版:
- RTS立ち上がり時に**常に**`Mouse_SetData()`が呼ばれる
- しかし、`Mouse_SetData()`内で`MouseDX`と`MouseDY`がクリアされる
- もし複数のイベントがポーリング間隔内に発生した場合、最後の状態のみが取得される

## 結論

問題の根本原因は、**MPX68K版がオリジナル版の`(!SCC_DatNum)`条件を`Mouse_SetData()`呼び出しの前に適用していない**こと。

オリジナル版では、この条件により:
1. 前回のデータが読み取られるまで新しいデータを生成しない
2. 各ポーリングサイクルで確実に1つのマウス状態が取得される
3. イベントの順序が保持される

MPX68K版では、この条件が`Mouse_SetData()`呼び出しの後にチェックされるため:
1. `Mouse_SetData()`が常に呼ばれ、`MouseDX`/`MouseDY`がクリアされる
2. 複数のイベントがポーリング間隔内に発生した場合、最後の状態のみが取得される
3. ダブルクリックの中間イベント（ボタンアップ）が失われる可能性がある

## 修正案

`Mouse_SetData()`の呼び出しを`(!SCC_DatNum)`条件の内側に戻す。ただし、キュー機能は維持する。

```c
if (rtsRising && rxEnabled) {
    if (!SCC_DatNum) {
        // データが無い時だけMouse_SetData()を呼ぶ
        Mouse_SetData();
        SCC_DatNum = 3;
        SCC_Dat[2] = MouseSt;
        SCC_Dat[1] = (BYTE)MouseX;
        SCC_Dat[0] = (BYTE)MouseY;
    } else {
        // データが残っている場合は、現在の状態をキューに追加
        // ただし、Mouse_SetData()は呼ばない
        // 代わりに、現在のMouseStatを直接使用
        SCC_MouseQEnqueue(MouseStat, 0, 0);
    }
}
```

しかし、この修正では不十分かもしれない。なぜなら、`MouseDX`/`MouseDY`の蓄積がクリアされないため。

より良い修正案:

```c
if (rtsRising && rxEnabled) {
    if (!SCC_DatNum) {
        // データが無い時だけMouse_SetData()を呼ぶ（オリジナル版と同じ）
        Mouse_SetData();
        SCC_DatNum = 3;
        SCC_Dat[2] = MouseSt;
        SCC_Dat[1] = (BYTE)MouseX;
        SCC_Dat[0] = (BYTE)MouseY;
    }
    // キュー機能は削除または別の方法で実装
}
```

これにより、オリジナル版と同じ動作になり、ダブルクリックが正常に動作するはずである。
