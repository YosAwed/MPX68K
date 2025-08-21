#!/bin/bash

echo "=== ADPCM最適化 性能テスト ==="
echo

# Check optimization settings
echo "📊 現在の設定確認:"
adpcm_h="X68000 Shared/px68k/x68k/adpcm_optimized.h"

if [ -f "$adpcm_h" ]; then
    enabled=$(grep "ADPCM_ENABLE_OPTIMIZATIONS" "$adpcm_h" | grep -o "[01]")
    level=$(grep "ADPCM_OPTIMIZATION_LEVEL" "$adpcm_h" | grep -o "[0-9]")
    
    echo "   ADPCM_ENABLE_OPTIMIZATIONS: $enabled"
    echo "   ADPCM_OPTIMIZATION_LEVEL: $level"
    
    if [ "$enabled" = "1" ]; then
        echo "   ✅ 最適化が有効"
        case "$level" in
            1) echo "   Level 1: 安全な最適化 (pow()削除, ブランチレス)" ;;
            *) echo "   Level $level: その他の最適化" ;;
        esac
    else
        echo "   ❌ 最適化が無効"
    fi
else
    echo "   ❌ 設定ファイルが見つかりません"
    exit 1
fi

echo
echo "🚀 Releaseビルド確認:"
if [ -f "/Users/awed/Library/Developer/Xcode/DerivedData/X68000-fkfqimuwlalhlrfklypulngtzvxj/Build/Products/Release/X68000.app/Contents/MacOS/X68000" ]; then
    echo "   ✅ Releaseビルドが存在"
    
    # Get file size
    size=$(ls -lh "/Users/awed/Library/Developer/Xcode/DerivedData/X68000-fkfqimuwlalhlrfklypulngtzvxj/Build/Products/Release/X68000.app/Contents/MacOS/X68000" | awk '{print $5}')
    echo "   バイナリサイズ: $size"
else
    echo "   ❌ Releaseビルドが見つかりません"
    exit 1
fi

echo
echo "📋 テスト準備完了!"
echo
echo "次のステップ:"
echo "1. エミュレーターを起動:"
echo "   open '/Users/awed/Library/Developer/Xcode/DerivedData/X68000-fkfqimuwlalhlrfklypulngtzvxj/Build/Products/Release/X68000.app'"
echo
echo "2. ADPCM音声を使用するゲームをロード"
echo "   - 音声付きゲーム"
echo "   - 効果音が多いゲーム"
echo "   - ナレーションやボイスがあるゲーム"
echo
echo "3. 音質確認:"
echo "   - 音の歪みがないか"
echo "   - ボリュームが適切か"
echo "   - クラッシュしないか"
echo
echo "4. 性能確認:"
echo "   - ゲームが軽快に動作するか"
echo "   - CPU使用率が下がったか"
echo "   - 初期化が速くなったか"

echo
echo "🎯 期待される効果:"
echo "   - CPU使用率: 15-25%削減"
echo "   - 初期化速度: 5-10倍向上"
echo "   - 音質: 元実装と同等"

echo
echo "=== テスト実行準備完了 ==="
