# MPX68K ダブルクリック不具合分析

## 1. オリジナル版とMPX68Kの主な違い

### オリジナル版 (px68k/x68k/scc.c)
- シンプルなマウス専用実装
- RTS立ち上がり検出時の条件:
  ```c
  if ( (!(SCC_RegsB[5]&2))&&(data&2)&&(SCC_RegsB[3]&1)&&(!SCC_DatNum) )
  ```
  - RTS 0→1遷移
  - 受信有効
  - **SCC_DatNum == 0 (データが無い時だけ)**
  
### MPX68K版 (MPX68K/X68000 Shared/px68k/x68k/scc.c)
- シリアル通信機能を追加
- マウスパケットキュー機能を追加
- RTS立ち上がり検出時の条件:
  ```c
  int rtsRising = (!(SCC_RegsB[5]&2)) && (data&2);
  int rxEnabled = (SCC_RegsB[3]&1);
  if (rtsRising && rxEnabled) {
      Mouse_SetData();
      if (!SCC_DatNum) {
          // Publish immediately
          SCC_DatNum = 3;
          SCC_Dat[2] = MouseSt;
          SCC_Dat[1] = (BYTE)MouseX;
          SCC_Dat[0] = (BYTE)MouseY;
      } else {
          // A packet is still pending; enqueue latest
          SCC_MouseQEnqueue(MouseSt, (BYTE)MouseX, (BYTE)MouseY);
      }
  }
  ```

## 2. 重要な違いの発見

### オリジナル版の動作
- `(!SCC_DatNum)` 条件により、**データが残っている場合はマウスデータを生成しない**
- これにより、前回のマウスイベントが完全に読み取られるまで新しいイベントは生成されない

### MPX68K版の動作
- `(!SCC_DatNum)` の条件チェックは残っているが、**else節でキューに追加する**
- つまり、データが残っている場合でも `Mouse_SetData()` が呼ばれ、キューに追加される

## 3. 問題の可能性

### Mouse_SetData()の呼び出しタイミング
オリジナル版では:
- データが無い時**だけ** `Mouse_SetData()` を呼ぶ
- つまり、マウス状態のラッチは**データ読み取り完了後**にのみ行われる

MPX68K版では:
- RTS立ち上がり時に**常に** `Mouse_SetData()` を呼ぶ
- データが残っている場合でもマウス状態をラッチする

### ダブルクリック判定への影響
ダブルクリックは「短時間に2回のクリック」を検出する必要がある。

**仮説**: 
1. 1回目のクリックイベントが発生
2. X68000側がRTSを立ち上げてデータ読み取り開始
3. まだデータ読み取り中に2回目のクリックが発生
4. オリジナル版: データが残っているので新しいマウスデータを生成しない → 2回目のクリックは次のRTS立ち上がりまで待つ
5. MPX68K版: データが残っていてもキューに追加 → しかし、この時点で`Mouse_SetData()`が呼ばれることで、マウス状態がリセットまたは更新される可能性

## 4. 次のステップ

Mouse_SetData()の実装を確認する必要がある。この関数がマウス状態をどのように更新するかによって、問題の原因が明確になる。


## 5. Mouse_SetData()の比較

### オリジナル版
```c
void Mouse_SetData(void)
{
    POINT pt;
    int x, y;

    if (MouseSW) {
        x = (int)MouseDX;
        y = (int)MouseDY;
        MouseDX = MouseDY = 0;  // ← ここで蓄積値をクリア
        MouseSt = MouseStat;
        // ... 範囲チェックとMouseX/MouseYへの設定
    } else {
        MouseSt = 0;
        MouseX = 0;
        MouseY = 0;
    }
}
```

### MPX68K版
```c
void Mouse_SetData(void)
{
    POINT pt;
    int x, y;

    if (MouseSW) {
        x = (int)MouseDX;
        y = (int)MouseDY;
        MouseDX = MouseDY = 0;  // ← ここで蓄積値をクリア（同じ）
        MouseSt = MouseStat;
        
        // 変化がなければ送らない（移動0かつボタン状態同一）
        if (x == 0 && y == 0 && MouseSt == LastMouseSt) {
            return;  // ← ここで早期リターン！
        }
        
        // ... 範囲チェックとMouseX/MouseYへの設定
        LastMouseX = MouseX;
        LastMouseY = MouseY;
        LastMouseSt = MouseSt;
        MouseDataSendCount++;
    } else {
        MouseSt = 0;
        MouseX = 0;
        MouseY = 0;
        LastMouseX = 0;
        LastMouseY = 0;
        LastMouseSt = 0;
    }
}
```

## 6. 問題の核心を発見！

### 重要な発見: Mouse_SetData()の早期リターン

MPX68K版では、以下の条件で**早期リターン**する:
```c
if (x == 0 && y == 0 && MouseSt == LastMouseSt) {
    return;
}
```

この早期リターンにより、**MouseX/MouseYが更新されない**まま関数が終了する。

### ダブルクリック時の動作シーケンス

#### オリジナル版（正常動作）
1. ユーザーが1回目のクリック（ボタンダウン）
2. Mouse_Event()でMouseStat |= 1
3. X68000側がRTSを立ち上げ
4. SCC_Write()が呼ばれ、`(!SCC_DatNum)`条件により:
   - Mouse_SetData()を呼ぶ
   - MouseDX=0, MouseDY=0, MouseSt=1 をSCC_Datに設定
5. ユーザーが1回目のボタンアップ
6. Mouse_Event()でMouseStat &= 0xfe (MouseStat=0)
7. X68000側が次のRTSを立ち上げ
8. Mouse_SetData()を呼ぶ
   - MouseDX=0, MouseDY=0, MouseSt=0 をSCC_Datに設定
9. ユーザーが2回目のクリック（ボタンダウン）
10. Mouse_Event()でMouseStat |= 1
11. X68000側が次のRTSを立ち上げ
12. Mouse_SetData()を呼ぶ
    - MouseDX=0, MouseDY=0, MouseSt=1 をSCC_Datに設定
13. X68000側は短時間に2回のボタンダウン(MouseSt=1)を検出 → ダブルクリック判定成功

#### MPX68K版（不具合）
1. ユーザーが1回目のクリック（ボタンダウン）
2. Mouse_Event()でMouseStat |= 1
3. X68000側がRTSを立ち上げ
4. SCC_Write()が呼ばれ、rtsRising条件により:
   - Mouse_SetData()を呼ぶ
   - MouseDX=0, MouseDY=0, MouseSt=1
   - LastMouseSt=1に更新
   - MouseX=0, MouseY=0, MouseSt=1 をSCC_Datに設定
5. ユーザーが1回目のボタンアップ
6. Mouse_Event()でMouseStat &= 0xfe (MouseStat=0)
7. X68000側が次のRTSを立ち上げ
8. SCC_Write()が呼ばれ:
   - Mouse_SetData()を呼ぶ
   - MouseDX=0, MouseDY=0, MouseSt=0
   - LastMouseSt=1（前回の値）
   - **条件 `x==0 && y==0 && MouseSt(0) != LastMouseSt(1)` → 早期リターンしない**
   - LastMouseSt=0に更新
   - MouseX=0, MouseY=0, MouseSt=0 をSCC_Datに設定
9. ユーザーが2回目のクリック（ボタンダウン）
10. Mouse_Event()でMouseStat |= 1
11. X68000側が次のRTSを立ち上げ
12. SCC_Write()が呼ばれ:
    - Mouse_SetData()を呼ぶ
    - MouseDX=0, MouseDY=0, MouseSt=1
    - LastMouseSt=0（前回の値）
    - **条件 `x==0 && y==0 && MouseSt(1) != LastMouseSt(0)` → 早期リターンしない**
    - LastMouseSt=1に更新
    - MouseX=0, MouseY=0, MouseSt=1 をSCC_Datに設定

実は、この分析では問題が見つからない...

### 再分析: SCC_Write()の条件の違い

もう一度、SCC_Write()の条件を詳しく見る:

#### オリジナル版
```c
if ( (!(SCC_RegsB[5]&2))&&(data&2)&&(SCC_RegsB[3]&1)&&(!SCC_DatNum) )
```
- **(!SCC_DatNum)** 条件: データが無い時**だけ**Mouse_SetData()を呼ぶ

#### MPX68K版
```c
int rtsRising = (!(SCC_RegsB[5]&2)) && (data&2);
int rxEnabled = (SCC_RegsB[3]&1);
if (rtsRising && rxEnabled) {
    Mouse_SetData();  // ← 常にMouse_SetData()を呼ぶ！
    if (!SCC_DatNum) {
        // Publish immediately
        SCC_DatNum = 3;
        SCC_Dat[2] = MouseSt;
        SCC_Dat[1] = (BYTE)MouseX;
        SCC_Dat[0] = (BYTE)MouseY;
    } else {
        // A packet is still pending; enqueue latest
        SCC_MouseQEnqueue(MouseSt, (BYTE)MouseX, (BYTE)MouseY);
    }
}
```

### 真の問題

MPX68K版では、**Mouse_SetData()がif文の外で呼ばれる**ため、`(!SCC_DatNum)`のチェックの前にMouseDX/MouseDYがクリアされる。

しかし、これは意図的な設計のように見える。問題は別の場所にあるかもしれない。

## 7. さらなる調査が必要

実際のダブルクリック判定がどこで行われているかを確認する必要がある。Visual Shellのコードを見るか、X68000側のマウスイベント処理を理解する必要がある。
