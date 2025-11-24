# スーパーインポーズ機能実装計画

## 概要
X68000エミュレータにスーパーインポーズ機能を追加する。これにより、ユーザーが選択した動画ファイル（MP4）が背景でループ再生され、エミュレータ画面の黒い部分を透過してその動画が表示される。

## アーキテクチャ分析
現在のレンダリングパイプラインは以下の通り：
- **GameScene.swift**: SpriteKit ベースのメイン描画クラス
- **spr**: SKSpriteNode - メインエミュレータ画面スプライト（X68000画面）
- **CRT効果**: 複数のSKEffectNodeとSKSpriteNodeによる重畳エフェクト
- **zPosition**: 階層レンダリング（0.0-1000の範囲で使用中）

## 技術仕様

### ビデオ統合アプローチ
1. **AVFoundation** を使用してMP4ファイルを再生
2. **SKVideoNode** を使用してSpriteKitに統合
3. **ブレンドモード** で黒透過を実現

### zPosition 階層設計
```
1000: UI要素（通知、ボタン）
999: CRT設定パネル
10.0: 仮想パッド
3.0-4.0: ラベル/タイトルロゴ
1.0-2.0: CRTエフェクト
0.5: スーパーインポーズビデオ [新規]
0.0: メインエミュレータ画面（spr）
-1.0: 背景ビデオ [新規バックアップ位置]
```

## 実装計画

### フェーズ1: コア機能
1. **SuperimposeManager.swift** 新規作成
   - MP4ファイル管理
   - SKVideoNode制御
   - ループ再生制御
   - 透過設定管理

2. **GameScene.swift 拡張**
   - superimposeVideo: SKVideoNode プロパティ追加
   - setupSuperimposeVideo() メソッド
   - applySuperimposeBlending() メソッド
   - zPosition管理更新

3. **メニュー統合（macOS）**
   - AppDelegate.swift に「Set Background Video...」メニュー追加
   - ファイル選択ダイアログ（MP4フィルタ）
   - 「Remove Background Video」メニュー

### フェーズ2: iOS対応
4. **iOS UI拡張**
   - 設定画面にビデオ選択ボタン追加
   - ドキュメントピッカー統合

### フェーズ3: 設定・最適化
5. **UserDefaults統合**
   - 選択した動画ファイルパスの永続化
   - 透過度設定（0.0-1.0）
   - 有効/無効切り替え

6. **パフォーマンス最適化**
   - ビデオデコーダリソース管理
   - メモリ使用量監視
   - フレームレート調整

## ファイル構成

### 新規ファイル
```
X68000 Shared/
├── SuperimposeManager.swift [新規]
└── Superimpose/
    ├── SuperimposeSettings.swift [新規]
    └── SuperimposeVideoNode.swift [新規]
```

### 変更ファイル
```
X68000 Shared/GameScene.swift [変更]
X68000 macOS/AppDelegate.swift [変更]
X68000 iOS/ConfigViewController.swift [変更]
```

## 実装詳細

### 1. SuperimposeManager.swift
```swift
import AVFoundation
import SpriteKit

class SuperimposeManager {
    private var videoPlayer: AVPlayer?
    private var videoNode: SKVideoNode?
    private weak var scene: SKScene?

    func loadVideo(from url: URL) { }
    func startLoop() { }
    func stopLoop() { }
    func setTransparency(_ alpha: Float) { }
    func attachToScene(_ scene: SKScene, zPosition: CGFloat) { }
}
```

### 2. GameScene.swift 拡張
```swift
// 新規プロパティ
private var superimposeManager: SuperimposeManager?
private var superimposeEnabled: Bool = false

// 新規メソッド
func setupSuperimposeVideo() { }
func loadSuperimposeVideo(url: URL) { }
func enableSuperimpose(_ enabled: Bool) { }
private func applySuperimposeSettings() { }
```

### 3. ブレンドモード実装
- **SKBlendMode.multiply** でエミュレータ画面の黒を透過
- **SKBlendMode.screen** で明度ベース合成
- カスタムCIFilterでより精密な黒透過

### 4. メニュー構成（macOS）
```
Display
├── Set Background Video...
├── Remove Background Video
├── ──────────
└── 既存項目...
```

## ユーザー体験

1. **設定**: Display > Set Background Video でMP4ファイル選択
2. **再生**: 即座にループ再生開始、黒部分透過
3. **調整**: 透過度調整可能（設定で0-100%）
4. **解除**: Remove Background Video で無効化

## 技術的考慮事項

### パフォーマンス
- ビデオデコード負荷を考慮したフレームレート調整
- メモリリーク防止（AVPlayer適切な解放）
- バックグラウンド時の動画再生停止

### ファイル形式サポート
- 主要対象: MP4 (H.264/HEVC)
- 対応解像度: 1080p以下推奨
- ループ最適化: シームレスループ対応

### エラーハンドリング
- ファイル読み込み失敗時の適切なフォールバック
- ビデオ再生エラーの用意な復旧
- メモリ不足時の自動無効化

## 将来の拡張性

### 拡張機能案
1. **複数ビデオ対応**: プレイリスト機能
2. **エフェクト**: ビデオにもCRT効果適用
3. **同期機能**: エミュレータ画面変化とビデオ同期
4. **ライブカメラ**: Webカメラ入力対応

### コード品質
- **Protocol指向設計**: VideoSourceProtocol定義
- **依存注入**: テスタビリティ向上
- **SwiftUI統合**: 将来のUI移行準備

## 実装優先度

1. **最高優先**: macOS基本機能（フェーズ1）
2. **高優先**: iOS対応（フェーズ2）
3. **中優先**: 設定・最適化（フェーズ3）
4. **低優先**: 拡張機能

この計画により、既存のレンダリングパイプラインを最小限の変更で拡張し、パフォーマンスを維持しながらスーパーインポーズ機能を実現できる。