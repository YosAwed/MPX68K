# スーパーインポーズ機能 実装計画 v3（修正版）

本案は plan2.md を踏まえ、黒透過の方式を「ブレンドモード」から「ルマキー（輝度キー）」へ変更し、確実に背景動画が覗く構成に修正したものです。既存のCRTパイプラインを最小限の変更で拡張します。

## 概要
- 背景にユーザー選択の動画（MP4）を `AVPlayer` + `SKVideoNode` でループ再生。
- エミュレータ画面スプライト（`spr`）のフラグメントシェーダで黒近傍をルマキー処理し、アルファを落として背景動画を透過表示。
- しきい値（threshold）、ソフトネス（softness）、全体強度（alpha）を調整可能。初期状態は無効（OFF）。

## 現行パイプライン（要点）
- `GameScene.swift` に `spr: SKSpriteNode`（エミュ画面）
- 各種CRTエフェクト（Bloom/Vignette/Scanlines/Chromatic/Persistence）は spr の上に重畳
- UI（設定パネル/通知）は最前面（z≈999）

## 新パイプライン（追加）
```
Scene  root
├─ SKVideoNode (z: -10)                 # 背景動画（全面）
├─ spr (z: 0, ルマキーで黒を抜く)         # エミュ画面
├─ CRT overlays/effects (z: 1..n)       # 既存の各種エフェクト
└─ UI (z: 999)
```
- 背景動画は spr より背面に固定。spr のアルファが落ちた領域に動画が覗く。

## 黒透過（ルマキー）設計
- 方式: spr のフラグメントでルマ（輝度）を算出し、しきい値以下を透明化
- 既存の `CRTFilterManager` のフラグメントに下記ユニフォームと処理を追加
  - `u_superEnabled` (bool/int)
  - `u_superThreshold` (float, 0.0..0.2 推奨デフォルト 0.03)
  - `u_superSoftness` (float, 0.0..0.2 推奨デフォルト 0.04)
  - `u_superAlpha` (float, 0.0..1.0 推奨デフォルト 1.0)
- 擬似コード:
```glsl
vec3 rgb = color.rgb;                       // spr の元色
float luma = dot(rgb, vec3(0.2126,0.7152,0.0722));
float k = smoothstep(u_superThreshold, u_superThreshold + u_superSoftness, luma);
float outA = baseAlpha * mix(0.0, 1.0, k) * u_superAlpha;
// gl_FragColor = vec4(rgb, outA);
```
- これにより、黒（低輝度）はアルファ0、暗灰はソフトに透け、明部は不透明。

## SuperimposeManager 設計
```swift
final class SuperimposeManager {
    private var player: AVPlayer?
    private(set) var videoNode: SKVideoNode?
    private weak var scene: SKScene?

    struct Settings: Codable, Equatable {
        var enabled: Bool = false
        var threshold: Float = 0.03
        var softness: Float = 0.04
        var alpha: Float = 1.0
        var bookmarkedURLData: Data? // セキュリティスコープブックマーク
    }
    var settings = Settings()

    func attach(to scene: SKScene) { /* add SKVideoNode(z:-10) */ }
    func loadVideo(url: URL) throws { /* AVPlayer作成, ループ, ミュート */ }
    func removeVideo() { /* 解放＆ノード除去 */ }
    func play() { player?.play() }
    func pause() { player?.pause() }
    func apply(to sprite: SKSpriteNode) { /* シェーダUniform更新 */ }
    func persist() { /* UserDefaults保存（ブックマーク含む）*/ }
    func restoreIfPossible() { /* ブックマーク復元 */ }
}
```
- ループ再生: `AVPlayerItemDidPlayToEndTime` 通知で `seek(to:.zero); play()`
- 音声は常時ミュート（競合回避）
- 背景リサイズは Scene サイズにフィット（アスペクト維持）

## GameScene 変更点
- 追加プロパティ
```swift
private let superManager = SuperimposeManager()
```
- 追加処理
  - `didMove(to:)` または 初期化完了時に `superManager.attach(to:self)`
  - メニューや設定から `loadSuperimposeVideo(url:)` を呼ぶ
  - `update(_:)` もしくは `didFinishUpdate()` で `superManager.apply(to: spr)` を呼び、Uniform更新
- z順は前記構成に固定

## macOS メニュー追加
```
Display
├─ Set Background Video…   (mp4 選択 + ブックマーク保存)
├─ Remove Background Video
├─ Enable Superimpose      (ON/OFF)
├─ Key Threshold…          (0.00–0.20)
├─ Key Softness…           (0.00–0.20)
└─ Intensity…              (0–100%)
```
- しきい値/ソフトネス/強度は簡易入力 or 小ダイアログ（将来 ConfigScene へスライダ実装）
- OFFの既定値で起動。UserDefaults 復元時にビデオがあれば attach+play するが、`enabled=false` ならsprは透過しない（動画はpause可）

## iOS 対応（Phase 2）
- 設定画面（既存/新規）に「Background Video」項目
  - ピッカー（UIDocumentPicker）で mp4 選択
  - ON/OFF と 3パラメータ（threshold/softness/alpha）のスライダ
- バックグラウンド遷移で自動一時停止/再開

## 実装フェーズ
1. コア（macOS）
   - SuperimposeManager 追加・Sceneへ添付
   - CRTFilterManager: ルマキー Uniform と処理の追加
   - GameScene: 毎フレーム Uniform 更新
   - AppDelegate: メニュー4項目追加（Set/Remove/Enable/Advanced…）
2. iOS
   - ピッカー/設定UI追加
3. 永続化
   - ブックマーク保存/復元、ON/OFF/値の保存
4. 最適化/保守
   - AVPlayer リソース監視、停止タイミング、メモリ解放
   - 例外/エラー復旧

## しきい値の推奨初期値
- threshold: 0.03（黒のみ確実に抜ける）
- softness: 0.04（境界をスムーズに）
- alpha: 1.0（全体強度。0.5で半透過効果に）

## テストチェックリスト
- mp4 を設定 → ループで再生される（音声ミュート）
- Superimpose OFF で完全に無効（spr不透明）
- ON で黒領域のみ透過、エミュ画面の暗部は僅かに透ける（softness調整で最適化）
- しきい値/ソフトネス/強度の変更が即時反映
- 起動再現性（保存済み動画の復元、OFF既定）
- バックグラウンドで一時停止、復帰で再開

## リスク/回避
- 極端に暗いコンテンツで誤抜け → threshold を下げる、alpha を抑える
- 4K動画でデコード負荷 → 1080p推奨/自動一時停止
- 色空間差によるキーずれ → 実機で微調整可能なUI提供

## まとめ
- 黒透過は「sprシェーダでルマキー」によって確実かつ軽量に実現。
- 背景動画は `SKVideoNode` を背面に設置し、spr のアルファで“覗かせる”。
- 既存のCRT効果やUIはそのまま。OFF既定・堅牢な復元とエラー処理で安全に運用可能。

---

## 追補: 改善提案の反映

### A. パフォーマンス最適化（シェーダ早期リターン）
ルマキー無効時はフラグメントで即リターンし、余計な計算を省く。

```glsl
// inside CRT fragment shader
if (u_superEnabled < 0.5) { // bool 代用
    gl_FragColor = vec4(color.rgb, baseAlpha);
    return;
}
```

### B. エラーハンドリング強化
`SuperimposeManager` の失敗ケースを網羅し、UI/ログに反映。

```swift
enum SuperimposeError: Error {
    case unsupportedFormat
    case fileAccessDenied
    case playerInitFailed
    case memoryPressure
}
```

- `loadVideo(url:)` は上記を throw。呼び出し側でユーザ通知とフォールバック（無効化）。
- リカバリ不能な場合は自動で `settings.enabled = false; pause(); removeVideo()`。

### C. 解像度/アスペクト対応
動画とシーンの比率に応じて `aspectFit/Fill` を切り替え可能に。

```swift
func adjustVideoScale() {
    guard let node = videoNode, let scene = scene else { return }
    let v = node.size.width / max(1, node.size.height)
    let s = scene.size.width / max(1, scene.size.height)
    // デフォルト: Fit（全体表示）
    let fit = (/*UserDefaults*/ false)
    if fit {
        if v > s { node.xScale = scene.size.width / node.size.width; node.yScale = node.xScale }
        else      { node.yScale = scene.size.height / node.size.height; node.xScale = node.yScale }
    } else {
        // Fill（はみ出し許容）
        if v < s { node.xScale = scene.size.width / node.size.width; node.yScale = node.xScale }
        else      { node.yScale = scene.size.height / node.size.height; node.xScale = node.yScale }
    }
    node.position = .zero
}
```

### D. メニュー構造（macOS）
```
Display
└─ Background Video ▶
   ├─ Set Video File…
   ├─ Remove Video
   ├─ ─────────────
   ├─ Enable
   ├─ Threshold (0–20%)
   ├─ Softness  (0–20%)
   └─ Intensity (0–100%)
```

### E. 技術的懸念と対策
- フレームレート影響: SKVideoNode デコードでメインに負荷が来る場合は、
  - 1080p以下を推奨案内、バックグラウンドキューでの初期プレイヤー準備、再生開始はメインで。
  - `preferredFramesPerSecond` を動画有効時に 60→30 へ可変（オプション）。
- メモリ使用量: 長時間再生でのリーク監視、`autoreleasepool {}` で定期解放、停止時に確実に `player = nil`。
- 色域変換: sRGB/P3 差によるキー閾値のズレは、しきい値/ソフトネスUIで調整可能にし、実機でキャリブレーション。

