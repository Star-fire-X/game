# 快速修复指南：移除 5GB 的大型二进制文件

## 🎯 当前状态

- **问题来源**：commit `c546691`（Initial commit）中的 Data 目录
- **受影响文件**：240 个 .wil 文件 + 其他游戏资源
- **总大小**：5.0 GB
- **已推送到 GitHub**：是（远程为 `https://github.com/Star-fire-X/game.git`）

---

## ⚡ 快速修复（3 步）

### 步骤 1：从 git 索引中移除 Data 目录

```bash
# 删除 Data 目录的所有文件从 git 索引（保留本地文件）
git rm -r --cached Data/

# 验证删除成功
git status | grep deleted

# 应该看到：
# deleted:    Data/...
# deleted:    Data/...
# ... (共 238 行)
```

### 步骤 2：提交更改

```bash
git commit -m "chore: remove large binary files from git tracking

Data directory (5GB) contains game asset files that should not be
version controlled. These files are preserved locally but excluded
from git history.

See FIX_LARGE_FILES.md for long-term solutions (Git LFS, etc.)

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

### 步骤 3：强制推送到 GitHub

```bash
# 警告：这会改变远程历史！确保没有其他协作者

git push origin master --force-with-lease
```

---

## ✅ 验证修复

```bash
# 1. 检查本地索引
git ls-files | grep -i "Data/" | wc -l
# 应显示：0

# 2. 检查本地文件仍然存在
ls -la Data/ | head -5
# 应显示 Data 目录和文件仍在

# 3. 检查远程仓库大小（可能需要几分钟更新）
# 访问：https://github.com/Star-fire-X/game/settings
# 查看 "Storage" 或 "Repository size"
# 应该从 ~5GB 降至 < 200MB
```

---

## 📋 检查清单

- [ ] 已执行 `git rm -r --cached Data/`
- [ ] 已验证 `git status` 显示要删除的文件
- [ ] 已执行 `git commit`
- [ ] 已执行 `git push --force-with-lease`
- [ ] 已验证本地 Data 目录文件仍然存在
- [ ] 已等待 GitHub 更新仓库大小（可能 5-10 分钟）

---

## ⚠️ 注意事项

1. **--force-with-lease 是安全的**
   - 只会拒绝推送如果远程有其他人的提交
   - 比纯 `--force` 更安全

2. **GitHub 仓库存储统计更新延迟**
   - 推送后可能需要 5-10 分钟才能更新仓库大小
   - 强制刷新页面可能看不到立即更新

3. **协作者需要更新**
   - 如果有其他协作者，他们需要执行：
   ```bash
   git fetch origin
   git reset --hard origin/master
   ```

4. **保留本地 Data 目录**
   - Data 文件**仍然在**你的本地工作目录中
   - 可以用 `.gitignore` 继续排除它们
   - 考虑使用 Git LFS 或云存储来管理这些文件

---

## 🔍 故障排查

### 如果 push 失败了

```bash
# 错误：Updates were rejected because the tip of your current branch is behind

# 解决：使用更安全的 --force-with-lease
git push origin master --force-with-lease

# 如果仍然失败，检查是否有其他提交
git log origin/master..HEAD --oneline
```

### 如果想保留历史（不推荐）

```bash
# 只在本地删除，不推送
git rm -r --cached Data/
git commit -m "..."
# 不执行 push --force

# 但这样的话 GitHub 上仍然有大文件，会浪费存储空间
```

---

## 🎯 执行命令

```bash
# 完整的一键修复脚本
set -e  # 任何命令失败都停止

echo "开始移除 Data 目录..."

# 1. 删除
git rm -r --cached Data/

# 2. 验证
echo "验证删除..."
DELETED_COUNT=$(git status | grep "deleted:" | wc -l)
echo "已删除 $DELETED_COUNT 个文件"

# 3. 提交
git commit -m "chore: remove large binary files from tracking

Data directory (5GB) removed from git index but preserved locally.
See FIX_LARGE_FILES.md for long-term asset management strategies.

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"

# 4. 推送
echo "推送到 GitHub..."
git push origin master --force-with-lease

echo "✅ 修复完成！"
echo ""
echo "验证步骤："
echo "  git ls-files | grep -i Data/ | wc -l  # 应为 0"
echo "  ls -la Data/ | head -5  # 文件应该仍然存在"
```

---

## 📊 预期结果

| 指标 | 修复前 | 修复后 |
|------|--------|--------|
| git 追踪文件数 | 3000+ | ~500 |
| 仓库大小 | ~5.5 GB | < 100 MB |
| Data 文件追踪状态 | 已追踪 | 未追踪 |
| 本地文件 | 存在 | 存在 ✅ |

---

## 💡 下一步建议

修复后，建议采用以下方案之一管理游戏资源：

### 方案 1：Git LFS（推荐）
```bash
# 安装：https://git-lfs.com/
git lfs install
git lfs track "Data/*.wil"
git add .gitattributes
git commit -m "chore: enable git-lfs for game assets"
git push origin master
```

### 方案 2：分离资源仓库
```bash
# 创建 mir2-assets 仓库，使用 git submodule 引入
# 保持主仓库轻量级
```

### 方案 3：云存储 + 下载脚本
```bash
# 上传 Data 目录到 AWS S3/Google Drive
# 提供 scripts/download-assets.sh 给团队使用
```

---

## 🆘 需要回滚怎么办？

```bash
# 如果修复有问题，可以恢复到修复前的状态
git reset --hard origin/master^

# 但一般不需要，因为我们只是从索引中移除，本地文件仍保留
```

---

## 📞 遇到问题？

如果：
- push 失败：检查是否有其他协作者的新提交
- 仓库大小没变：GitHub 需要 5-10 分钟更新统计
- Data 文件消失：恢复 `git checkout HEAD -- Data/`
