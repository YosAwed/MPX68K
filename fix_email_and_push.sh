#!/bin/bash

# GitHubメール設定エラー修正スクリプト
# 使用方法: ./fix_email_and_push.sh YOUR_GITHUB_USERNAME

set -e  # エラーが発生したら停止

# 引数チェック
if [ $# -eq 0 ]; then
    echo "使用方法: $0 YOUR_GITHUB_USERNAME"
    echo "例: $0 myusername"
    exit 1
fi

USERNAME=$1
NOREPLY_EMAIL="${USERNAME}@users.noreply.github.com"

echo "=== GitHubメール設定エラー修正スクリプト ==="
echo "GitHubユーザー名: $USERNAME"
echo "使用するメールアドレス: $NOREPLY_EMAIL"
echo ""

# 現在のディレクトリがGitリポジトリかチェック
if [ ! -d ".git" ]; then
    echo "エラー: 現在のディレクトリはGitリポジトリではありません"
    echo "X68Macプロジェクトのディレクトリで実行してください"
    exit 1
fi

# 現在のブランチを確認
CURRENT_BRANCH=$(git branch --show-current)
echo "現在のブランチ: $CURRENT_BRANCH"

if [ "$CURRENT_BRANCH" != "improve-code-documentation" ]; then
    echo "警告: 現在のブランチが 'improve-code-documentation' ではありません"
    read -p "続行しますか？ (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "処理を中止しました"
        exit 1
    fi
fi

echo ""
echo "=== ステップ1: Gitメール設定の更新 ==="
echo "現在のメール設定:"
git config user.email || echo "メール設定なし"

echo "新しいメール設定に更新中..."
git config user.email "$NOREPLY_EMAIL"

echo "更新後のメール設定:"
git config user.email

echo ""
echo "=== ステップ2: コミットの修正 ==="
echo "最新のコミット情報:"
git log --format="%h %an <%ae> %s" -1

echo ""
echo "コミットを修正中..."
git commit --amend --reset-author --no-edit

echo "修正後のコミット情報:"
git log --format="%h %an <%ae> %s" -1

echo ""
echo "=== ステップ3: プッシュの実行 ==="
echo "リモートリポジトリにプッシュ中..."

# プッシュを試行
if git push origin "$CURRENT_BRANCH"; then
    echo "✅ プッシュが成功しました！"
else
    echo "⚠️ 通常のプッシュが失敗しました。強制プッシュを試行します..."
    if git push origin "$CURRENT_BRANCH" --force; then
        echo "✅ 強制プッシュが成功しました！"
    else
        echo "❌ プッシュが失敗しました。手動で確認してください。"
        exit 1
    fi
fi

echo ""
echo "=== 完了 ==="
echo "✅ メール設定の修正とプッシュが完了しました"
echo ""
echo "次のステップ:"
echo "1. GitHubでフォークしたリポジトリにアクセス"
echo "2. 'improve-code-documentation' ブランチが作成されていることを確認"
echo "3. 'Compare & pull request' ボタンをクリック"
echo "4. プルリクエストを作成"
echo ""
echo "プルリクエストの作成方法は patch_application_guide.md を参照してください"

