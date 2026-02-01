# Git 大型二进制文件问题修复方案

## 🚨 问题诊断

### 发现的大型文件
- **Data 目录大小**：5.0 GB
- **被追踪的文件数**：238 个
- **git 历史中的对象**：238 个

### 文件示例（按大小排序）
1. Data/HumEffect.wil - 610 MB
2. Data/Weapon1.wil - 442 MB
3. Data/Hum1.wil - 232 MB
4. Data/Hum.wil - 180 MB
5. Data/Tiles.wil - 146 MB
... 还有 233 个类似的大文件

### 根本原因
`.gitignore` 中有 `Data/` 规则，但这些文件在规则添加**前**就已经被 git 追踪了。
.gitignore 只能忽略未追踪的文件，无法自动删除已追踪的文件。

---

## 🔧 修复方案（选择一种）

### 方案 A：轻量级修复（推荐，如果还未推送到 GitHub）

如果你**还未推送到 GitHub**，这是最简单的方法：

```bash
# 1. 从 git 索引中删除 Data 目录（但保留本地文件）
git rm -r --cached Data/

# 2. 验证删除效果
git status | grep "deleted"

# 3. 提交更改
git commit -m "chore: remove large binary files from git tracking

These files were added before .gitignore rules were created.
Data directory is excluded via .gitignore and should not be tracked."

# 4. 现在才推送到 GitHub
git push origin master
```

**验证修复**：
```bash
# 确认 Data 文件已从索引中移除
git ls-files | grep "Data/" | wc -l  # 应显示 0

# 确认本地文件仍然存在（未被删除）
ls -la Data/ | head -5
```

---

### 方案 B：硬核修复（如果已推送到 GitHub）

如果已经推送到 GitHub，需要从 git 历史中完全移除这些文件：

#### 步骤 1：使用 git filter-branch 清除历史

```bash
# 警告：这会重写提交历史！

# 1. 从所有提交历史中删除 Data 目录
git filter-branch --force --tree-filter 'rm -rf Data' --prune-empty HEAD

# 2. 强制推送（会覆盖 GitHub 上的历史）
git push origin --force master

# 3. 清理本地引用
git reflog expire --expire=now --all
git gc --prune=now
```

#### 步骤 2：使用 BFG Repo-Cleaner（推荐，更快更安全）

```bash
# 1. 克隆一个镜像仓库
git clone --mirror https://github.com/YOUR_USERNAME/mir2-cpp.git mir2-cpp.git

# 2. 下载 BFG: https://rtyley.github.io/bfg-repo-cleaner/

# 3. 删除 Data 目录（保留最后一次提交）
bfg --delete-folders Data mir2-cpp.git

# 4. 清理重新打包
cd mir2-cpp.git
git reflog expire --expire=now --all
git gc --prune=now --aggressive
cd ..

# 5. 强制推送到 GitHub
git push --mirror https://github.com/YOUR_USERNAME/mir2-cpp.git
```

---

## ⚠️ 推送前的最终检查清单

### 1. 验证没有大型文件
```bash
# 列出所有被追踪的文件，按大小排序
git ls-files --size | sort -rn | head -20

# 结果应该显示源代码文件和配置文件，而不是二进制数据
```

### 2. 验证 Data 目录未被追踪
```bash
# 应显示 0
git ls-files | grep -i "Data/" | wc -l
```

### 3. 验证 .gitignore 规则完整
```bash
cat .gitignore | grep -E "^Data|wil|\.db|\.sqlite"
```

### 4. 检查仓库大小
```bash
# 应该 < 200 MB（取决于源代码大小）
du -sh .git/
```

---

## 📋 推荐的 .gitignore 更新

确保你的 `.gitignore` 包含这些规则：

```gitignore
# Game assets (WIL files, maps, etc.)
Data/
Wav/
MUSIC/
Map/

# Database and runtime files
*.db
*.sqlite
*.sqlite3

# Compiled binaries
*.exe
*.dll
*.so
*.a

# 等等...
```

---

## 🚀 完整修复流程（如果还未推送）

```bash
# 1. 检查当前状态
git status

# 2. 从索引中删除 Data 目录
git rm -r --cached Data/

# 3. 验证
git ls-files | grep -i "Data/" | wc -l  # 应为 0

# 4. 提交
git commit -m "chore: remove large binary files from tracking

Data directory contains 5GB of game asset files that should not be
version controlled. Added to .gitignore for future exclusion.

Files removed:
- Data/*.wil (240 game asset files)

Local files are preserved and can be managed separately using:
- Git LFS (Large File Storage)
- Cloud storage (S3, Google Drive)
- Assets repository (separate repo)"

# 5. 推送
git push origin master
```

---

## 💡 长期解决方案

对于大型游戏资源文件，建议采用以下方案之一：

### 选项 1：Git LFS（Large File Storage）
```bash
# 安装：https://git-lfs.com/

# 配置
git lfs install

# 追踪大文件
git lfs track "Data/*.wil"
git lfs track "*.db" "*.exe"

# 提交
git add .gitattributes
git commit -m "chore: configure git-lfs for large files"
git push origin master
```

### 选项 2：分离资源仓库
```bash
# 创建单独的仓库存储大文件
# 在主仓库中以 submodule 方式引入
git submodule add https://github.com/YOUR_USERNAME/mir2-assets.git Data
git commit -m "chore: add assets as submodule"
git push origin master
```

### 选项 3：云存储 + 下载脚本
```bash
# 上传 Data 目录到 AWS S3 或 Google Drive
# 在仓库中提供下载脚本：scripts/download-assets.sh
# 用户首次克隆后运行脚本下载资源
```

---

## ✅ 推送后的验证

```bash
# 访问 GitHub 检查：
# 1. 仓库大小（应 < 200 MB）
# 2. 不应包含任何 .wil 文件
# 3. 仓库 Insights → Storage 中不应有 Data 目录

# 或使用 GitHub CLI 检查：
gh repo view --json diskUsage
```

---

## 🆘 如果意外推送了怎么办？

1. **GitHub 会拒绝大推送**（> 100 MB 文件会失败）
2. **如果成功推送了**：
   - 使用 GitHub 删除仓库 + 重新创建（最简单）
   - 或使用上面的 BFG 方案清除历史后强制推送
   - GitHub 管理员可以帮助清理仓库存储

---

## 📞 需要帮助？

如果你：
- ✅ **还未推送**：使用方案 A（git rm）
- ✅ **已推送到 GitHub**：使用方案 B（BFG 或 filter-branch）
- ✅ **想长期管理大文件**：使用 Git LFS 或分离仓库
