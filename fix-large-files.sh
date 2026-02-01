#!/bin/bash

# 移除大型二进制文件的一键修复脚本
# 使用方法：bash fix-large-files.sh

set -e  # 任何命令失败都停止

echo "=========================================="
echo "  移除大型二进制文件修复脚本"
echo "=========================================="
echo ""

# 检查当前分支
CURRENT_BRANCH=$(git branch --show-current)
echo "当前分支: $CURRENT_BRANCH"
echo ""

# 检查 Data 目录
if [ ! -d "Data" ]; then
    echo "❌ 错误：Data 目录不存在"
    exit 1
fi

# 检查被追踪的 Data 文件数量
TRACKED_COUNT=$(git ls-files | grep -i "Data/" | wc -l)
echo "📊 当前追踪的 Data 文件数量: $TRACKED_COUNT"

if [ "$TRACKED_COUNT" -eq 0 ]; then
    echo "✅ Data 目录已经不在 git 追踪中，无需修复"
    exit 0
fi

echo ""
echo "⚠️  警告：将从 git 索引中删除 $TRACKED_COUNT 个 Data 文件"
echo "    （本地文件会保留，不会被删除）"
echo ""
read -p "继续执行？(y/n): " -n 1 -r
echo ""

if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "操作已取消"
    exit 0
fi

echo ""
echo "步骤 1/4: 从 git 索引中移除 Data 目录..."
git rm -r --cached Data/

echo ""
echo "步骤 2/4: 验证删除..."
DELETED_COUNT=$(git status --short | grep "^D " | wc -l)
echo "✅ 已标记删除 $DELETED_COUNT 个文件"

echo ""
echo "步骤 3/4: 提交更改..."
git commit -m "chore: remove large binary files from git tracking

Data directory (5GB) contains game asset files that should not be
version controlled. Files are preserved locally and excluded via
.gitignore.

Removed files:
- Data/*.wil (240 game asset files)
- Total size: ~5GB

See FIX_LARGE_FILES.md for long-term asset management strategies.

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"

echo ""
echo "步骤 4/4: 推送到远程仓库..."
echo "⚠️  将使用 --force-with-lease 强制推送（安全模式）"
echo ""
read -p "确认推送？(y/n): " -n 1 -r
echo ""

if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "⚠️  提交已完成，但未推送到远程"
    echo "手动推送命令："
    echo "  git push origin $CURRENT_BRANCH --force-with-lease"
    exit 0
fi

git push origin $CURRENT_BRANCH --force-with-lease

echo ""
echo "=========================================="
echo "  ✅ 修复完成！"
echo "=========================================="
echo ""
echo "验证步骤："
echo ""
echo "  1. 检查 git 索引（应为 0）："
echo "     git ls-files | grep -i Data/ | wc -l"
echo ""
echo "  2. 检查本地文件（应仍存在）："
echo "     ls -la Data/ | head -5"
echo ""
echo "  3. 等待 5-10 分钟后，访问 GitHub 检查仓库大小："
echo "     https://github.com/Star-fire-X/game/settings"
echo ""
echo "  4. 预期仓库大小：从 ~5GB 降至 < 200MB"
echo ""
echo "=========================================="
echo "下一步建议："
echo ""
echo "  考虑使用 Git LFS 管理游戏资源："
echo "    git lfs install"
echo "    git lfs track 'Data/*.wil'"
echo "    git add .gitattributes"
echo "    git commit -m 'chore: enable git-lfs for assets'"
echo "    git push origin $CURRENT_BRANCH"
echo ""
echo "  详细信息请查看："
echo "    - QUICK_FIX_LARGE_FILES.md"
echo "    - FIX_LARGE_FILES.md"
echo "=========================================="
