# Docker 部署文件总结

本文档列出了为 MIR2 项目创建的所有 Docker 相关文件及其用途。

## 创建的文件列表

### 核心文件

1. **Dockerfile** - 多阶段构建文件
   - 位置: 项目根目录
   - 用途: 定义 6 个构建阶段（vcpkg-deps, builder, gateway, world, game, db）
   - 特点:
     - Stage 1: 基于 debian:bookworm 预编译 vcpkg 依赖
     - Stage 2: 编译所有服务可执行文件
     - Stage 3-6: 创建最小运行时镜像
     - 所有服务以 mir2:mir2 非 root 用户运行
     - 仅安装必要的运行时依赖（libpq5, libhiredis0.14, libstdc++6）

2. **.dockerignore** - Docker 构建忽略文件
   - 位置: 项目根目录
   - 用途: 减少构建上下文大小，排除不必要的文件
   - 包含: build 产物、IDE 文件、文档、测试等

3. **docker-compose.yml** - Docker Compose 编排文件
   - 位置: 项目根目录
   - 用途: 定义并管理所有服务
   - 包含服务:
     - postgres: PostgreSQL 16 数据库
     - redis: Redis 7 缓存
     - db: MIR2 DB 服务
     - world: MIR2 World 服务
     - game: MIR2 Game 服务（可扩展）
     - gateway: MIR2 Gateway 服务
   - 特点:
     - 健康检查配置
     - 数据持久化卷
     - 服务依赖管理
     - 独立网络隔离

4. **.env.example** - 环境变量模板
   - 位置: 项目根目录
   - 用途: 提供配置模板
   - 包含: 数据库配置、Redis 配置、服务端口等

### 脚本和工具

5. **build-docker.sh** - Docker 构建脚本
   - 位置: 项目根目录
   - 用途: 简化 Docker 镜像构建
   - 功能:
     - 支持构建单个或所有服务
     - 支持自定义标签和仓库
     - 支持推送到镜像仓库
     - 提供清理和缓存管理
   - 使用: `./build-docker.sh [选项] [服务名]`

6. **Makefile.docker** - Make 命令文件
   - 位置: 项目根目录
   - 用途: 提供便捷的管理命令
   - 功能分类:
     - 构建命令: build, build-gateway, build-world, etc.
     - 运行命令: up, down, restart, etc.
     - 监控命令: logs, ps, stats, top
     - 扩展命令: scale-game, scale-game-3, etc.
     - 调试命令: shell-*, psql, redis-cli
     - 清理命令: clean, clean-volumes, clean-all
     - 备份命令: backup-db, restore-db, backup-volumes
     - 验证命令: health-check, version
   - 使用: `make -f Makefile.docker <命令>`

### CI/CD

7. **.github/workflows/docker-build.yml** - GitHub Actions 工作流
   - 位置: .github/workflows/
   - 用途: 自动化 Docker 构建和推送
   - 功能:
     - 自动构建所有服务镜像
     - 推送到 GitHub Container Registry (ghcr.io)
     - 支持多种标签策略（branch, tag, sha, latest）
     - PR 自动集成测试
     - 构建缓存优化

### 文档

8. **DOCKER_DEPLOYMENT.md** - 详细部署文档
   - 位置: 项目根目录
   - 内容:
     - 架构概述和特点
     - 快速开始指南
     - 单独构建和运行说明
     - 扩展和配置方法
     - 日志和数据管理
     - 故障排查
     - 生产环境建议
     - 性能优化
     - 安全建议

9. **DOCKER_QUICKSTART.md** - 快速入门指南
   - 位置: 项目根目录
   - 内容:
     - 前置要求
     - 3 种快速开始方法
     - 验证部署
     - 常用操作
     - 配置修改
     - 故障排查
     - 数据备份
     - 性能优化
     - 进阶使用
     - 常见问题

10. **DOCKER_FILES_SUMMARY.md** - 本文档
    - 位置: 项目根目录
    - 内容: 所有 Docker 文件的总结和使用指南

## 文件关系图

```
mir2-cpp/
├── Dockerfile                          # 多阶段构建文件
├── .dockerignore                       # 构建忽略文件
├── docker-compose.yml                  # 服务编排文件
├── .env.example                        # 环境变量模板
├── build-docker.sh                     # 构建脚本
├── Makefile.docker                     # Make 命令
├── DOCKER_DEPLOYMENT.md                # 详细部署文档
├── DOCKER_QUICKSTART.md                # 快速入门指南
├── DOCKER_FILES_SUMMARY.md             # 本文档
├── .github/
│   └── workflows/
│       └── docker-build.yml            # CI/CD 工作流
└── config/                             # 配置文件目录（挂载到容器）
    ├── gateway.yaml
    ├── world.yaml
    ├── game.yaml
    ├── db.yaml
    └── server.yaml
```

## 快速使用指南

### 方法 1: 使用 Makefile（最推荐）

```bash
# 查看所有命令
make -f Makefile.docker help

# 一键启动
make -f Makefile.docker quick-start

# 查看日志
make -f Makefile.docker logs

# 扩展 Game 服务
make -f Makefile.docker scale-game-3

# 备份数据库
make -f Makefile.docker backup-db

# 完全重置
make -f Makefile.docker reset
```

### 方法 2: 使用 Docker Compose

```bash
# 构建
docker compose build

# 启动
docker compose up -d

# 查看状态
docker compose ps

# 查看日志
docker compose logs -f

# 停止
docker compose down
```

### 方法 3: 使用构建脚本

```bash
# 构建所有服务
./build-docker.sh all

# 构建单个服务
./build-docker.sh gateway

# 不使用缓存构建
./build-docker.sh -n all

# 构建并推送
./build-docker.sh -r myregistry.com/mir2 -p all
```

## 常见使用场景

### 场景 1: 首次部署

```bash
# 1. 复制环境变量
cp .env.example .env

# 2. 编辑配置（可选）
vim .env

# 3. 一键启动
make -f Makefile.docker quick-start

# 4. 验证部署
make -f Makefile.docker health-check
```

### 场景 2: 开发环境

```bash
# 只启动数据库服务
make -f Makefile.docker dev-up

# 本地编译运行服务端
# ...

# 关闭数据库
make -f Makefile.docker dev-down
```

### 场景 3: 生产部署

```bash
# 1. 构建镜像
./build-docker.sh -t v1.0.0 all

# 2. 推送到私有仓库
./build-docker.sh -r registry.company.com/mir2 -t v1.0.0 -p all

# 3. 在生产服务器拉取镜像
make -f Makefile.docker pull REGISTRY=registry.company.com/mir2 TAG=v1.0.0

# 4. 启动服务
docker compose up -d
```

### 场景 4: 性能测试

```bash
# 扩展 Game 服务到 5 个实例
make -f Makefile.docker scale-game-5

# 查看资源使用
make -f Makefile.docker stats

# 查看日志
make -f Makefile.docker logs-game
```

### 场景 5: 故障排查

```bash
# 查看服务状态
make -f Makefile.docker ps

# 查看特定服务日志
make -f Makefile.docker logs-gateway

# 进入容器调试
make -f Makefile.docker shell-gateway

# 检查数据库连接
make -f Makefile.docker psql

# 检查 Redis
make -f Makefile.docker redis-cli

# 健康检查
make -f Makefile.docker health-check
```

### 场景 6: 数据备份和恢复

```bash
# 备份数据库
make -f Makefile.docker backup-db

# 备份数据卷
make -f Makefile.docker backup-volumes

# 恢复数据库
make -f Makefile.docker restore-db BACKUP_FILE=backup/mir2_backup_20260201_120000.sql
```

### 场景 7: 清理和重置

```bash
# 停止服务
make -f Makefile.docker down

# 清理日志
make -f Makefile.docker clean-logs

# 删除数据卷（危险！）
make -f Makefile.docker clean-volumes

# 完全清理
make -f Makefile.docker clean-all

# 完全重置（删除数据并重新构建）
make -f Makefile.docker reset
```

## CMake 构建参数说明

Dockerfile 中使用的 CMake 参数：

```cmake
-DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake
    # 使用 vcpkg 工具链

-DBUILD_SERVER=ON
    # 构建服务端

-DBUILD_DB=ON
    # 构建 DB 服务

-DBUILD_CLIENT=OFF
    # 不构建客户端（减少依赖）

-DBUILD_TESTS=OFF
    # 不构建测试（减少构建时间）

-DBUILD_BENCHMARKS=OFF
    # 不构建基准测试

-DBUILD_TOOLS=OFF
    # 不构建工具

-DCMAKE_BUILD_TYPE=Release
    # 发布版本构建（优化性能）

-DCMAKE_INSTALL_PREFIX=/opt/mir2
    # 安装路径
```

## 端口映射

| 服务 | 容器端口 | 宿主机端口 | 说明 |
|------|---------|-----------|------|
| Gateway | 7000 | 7000 | 客户端连接入口 |
| World | 7100 | 7100 | 世界服务 |
| Game | 7200 | 7200-7210 | 游戏逻辑（可扩展） |
| DB | 7300 | 7300 | 数据库服务 |
| PostgreSQL | 5432 | 5432 | PostgreSQL 数据库 |
| Redis | 6379 | 6379 | Redis 缓存 |

## 数据卷

| 卷名 | 挂载点 | 说明 |
|------|--------|------|
| postgres_data | /var/lib/postgresql/data | PostgreSQL 数据 |
| redis_data | /data | Redis 数据（AOF 持久化） |
| gateway_logs | /opt/mir2/logs | Gateway 日志 |
| world_logs | /opt/mir2/logs | World 日志 |
| game_logs | /opt/mir2/logs | Game 日志 |
| db_logs | /opt/mir2/logs | DB 日志 |

## 环境变量

主要环境变量（详见 `.env.example`）：

```bash
# PostgreSQL
POSTGRES_DB=mir2
POSTGRES_USER=mir2
POSTGRES_PASSWORD=mir2_password

# 服务连接
MIR2_DB_HOST=postgres
MIR2_DB_PORT=5432
MIR2_REDIS_HOST=redis
MIR2_REDIS_PORT=6379

# 端口配置
GATEWAY_PORT=7000
WORLD_PORT=7100
GAME_PORT=7200
DB_PORT=7300

# 日志级别
LOG_LEVEL=info

# Game 实例数
GAME_REPLICAS=1
```

## 安全特性

1. **非 root 用户**: 所有服务以 `mir2:mir2` 用户运行
2. **最小镜像**: 使用 debian:bookworm-slim 减少攻击面
3. **只读挂载**: 配置文件以只读方式挂载
4. **网络隔离**: 服务运行在独立的 Docker 网络
5. **健康检查**: 数据库服务配置健康检查
6. **依赖最小化**: 仅安装必要的运行时依赖

## 性能优化

1. **构建缓存**: vcpkg 依赖单独缓存层
2. **并行编译**: 使用 `-j$(nproc)` 并行构建
3. **多阶段构建**: 最终镜像只包含运行时文件
4. **水平扩展**: Game 服务支持多实例部署
5. **构建缓存**: GitHub Actions 使用 cache-from/cache-to

## 故障排查工具

### 查看所有 Makefile 命令
```bash
make -f Makefile.docker help
```

### 查看构建脚本帮助
```bash
./build-docker.sh --help
```

### 查看服务日志
```bash
make -f Makefile.docker logs
make -f Makefile.docker logs-gateway
make -f Makefile.docker logs-world
make -f Makefile.docker logs-game
make -f Makefile.docker logs-db
```

### 健康检查
```bash
make -f Makefile.docker health-check
make -f Makefile.docker ps
make -f Makefile.docker stats
```

### 进入容器调试
```bash
make -f Makefile.docker shell-gateway
make -f Makefile.docker shell-world
make -f Makefile.docker shell-game
make -f Makefile.docker shell-db
make -f Makefile.docker shell-postgres
make -f Makefile.docker shell-redis
```

### 数据库调试
```bash
make -f Makefile.docker psql
make -f Makefile.docker redis-cli
```

## 下一步

1. **阅读快速入门**: [DOCKER_QUICKSTART.md](DOCKER_QUICKSTART.md)
2. **阅读详细文档**: [DOCKER_DEPLOYMENT.md](DOCKER_DEPLOYMENT.md)
3. **查看 Makefile**: 运行 `make -f Makefile.docker help`
4. **查看构建脚本**: 运行 `./build-docker.sh --help`
5. **配置环境变量**: 复制并编辑 `.env.example`

## 获取帮助

- Makefile 命令: `make -f Makefile.docker help`
- 构建脚本: `./build-docker.sh --help`
- Docker Compose: `docker compose --help`
- 快速入门: [DOCKER_QUICKSTART.md](DOCKER_QUICKSTART.md)
- 详细文档: [DOCKER_DEPLOYMENT.md](DOCKER_DEPLOYMENT.md)

## 许可证

遵循项目主许可证。
