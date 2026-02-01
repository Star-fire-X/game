# Docker 部署文档中心

**MIR2-CPP 项目 Docker 容器化部署完整指南**

本目录包含了 MIR2-CPP 项目的所有 Docker 相关文件、配置和文档，提供从开发到生产的完整容器化解决方案。

---

## 📑 目录

- [快速导航](#-快速导航)
- [文件清单](#-文件清单)
- [快速开始](#-快速开始)
- [架构概览](#-架构概览)
- [最佳实践](#-最佳实践)
- [常见场景](#-常见场景)
- [故障排查](#-故障排查)
- [扩展阅读](#-扩展阅读)

---

## 🚀 快速导航

### 核心文档（按使用顺序）

| 文档 | 适用场景 | 阅读时间 |
|------|---------|---------|
| [快速入门指南](../DOCKER_QUICKSTART.md) | 🎯 新手入门，5分钟快速部署 | 10 分钟 |
| [完整部署指南](../DOCKER_DEPLOYMENT.md) | 📦 生产环境部署、高级配置 | 30 分钟 |
| [验证测试清单](../DOCKER_VERIFICATION.md) | ✅ 部署验证、健康检查 | 15 分钟 |
| [文件说明总结](../DOCKER_FILES_SUMMARY.md) | 📋 文件用途速查 | 5 分钟 |

### 快捷链接

- **我是新手** → 从 [快速入门指南](../DOCKER_QUICKSTART.md) 开始
- **生产部署** → 阅读 [完整部署指南](../DOCKER_DEPLOYMENT.md)
- **遇到问题** → 查看 [故障排查](#-故障排查) 章节
- **查看命令** → 运行 `make -f Makefile.docker help`

---

## 📦 文件清单

### 核心配置文件

#### 1. **Dockerfile**
- **位置**: `./Dockerfile`
- **用途**: 多阶段构建定义文件
- **说明**:
  - 使用 6 个构建阶段（vcpkg-deps, builder, gateway, world, game, db）
  - 基于 Debian Bookworm 精简镜像
  - 所有服务以非 root 用户运行（mir2:mir2）
  - 最终镜像大小优化至 ~150MB
- **相关命令**:
  ```bash
  # 查看构建阶段
  docker build --target builder -t mir2-builder .

  # 构建所有服务
  docker compose build
  ```

#### 2. **docker-compose.yml**
- **位置**: `./docker-compose.yml`
- **用途**: 主编排配置（适用于开发和测试）
- **包含服务**:
  - **postgres**: PostgreSQL 16 数据库
  - **redis**: Redis 7 缓存
  - **db**: MIR2 DB 数据库代理服务
  - **world**: MIR2 World 世界服务器
  - **game**: MIR2 Game 游戏逻辑服务器（支持横向扩展）
  - **gateway**: MIR2 Gateway 客户端接入网关
- **特性**:
  - 健康检查配置
  - 数据持久化卷
  - 服务启动依赖管理
  - 独立网络隔离
- **相关命令**:
  ```bash
  # 启动所有服务
  docker compose up -d

  # 查看服务状态
  docker compose ps
  ```

#### 3. **docker-compose.dev.yml**
- **位置**: `./docker-compose.dev.yml`
- **用途**: 开发环境覆盖配置
- **特性**:
  - 启用代码热重载（挂载源码目录）
  - 调试端口映射
  - 详细日志输出（debug 级别）
  - 开发工具集成
- **使用方式**:
  ```bash
  docker compose -f docker-compose.yml -f docker-compose.dev.yml up -d
  ```

#### 4. **docker-compose.prod.yml**
- **位置**: `./docker-compose.prod.yml`
- **用途**: 生产环境覆盖配置
- **特性**:
  - 资源限制配置
  - 生产级日志（info 级别）
  - 安全加固配置
  - 自动重启策略
- **使用方式**:
  ```bash
  docker compose -f docker-compose.yml -f docker-compose.prod.yml up -d
  ```

#### 5. **.dockerignore**
- **位置**: `./.dockerignore`
- **用途**: Docker 构建上下文排除文件
- **排除内容**:
  - 构建产物（build/, bin/, lib/）
  - IDE 文件（.vscode/, .idea/）
  - 文档和测试
  - 临时文件和日志
- **效果**: 减少构建上下文大小，加速构建过程

#### 6. **.env.example**
- **位置**: `./.env.example`
- **用途**: 环境变量配置模板
- **包含配置**:
  - 数据库连接参数
  - Redis 连接参数
  - 服务端口配置
  - 日志级别
  - 资源限制
- **使用方式**:
  ```bash
  cp .env.example .env
  vim .env  # 根据需要修改配置
  ```

### 自动化脚本

#### 7. **build-docker.sh**
- **位置**: `./build-docker.sh`
- **用途**: Docker 镜像构建自动化脚本
- **功能**:
  - 构建单个或所有服务
  - 自定义镜像标签
  - 推送到镜像仓库
  - 构建缓存管理
- **使用示例**:
  ```bash
  # 构建所有服务
  ./build-docker.sh all

  # 构建指定服务
  ./build-docker.sh gateway

  # 不使用缓存构建
  ./build-docker.sh -n all

  # 构建并推送到仓库
  ./build-docker.sh -r registry.example.com/mir2 -p all

  # 查看帮助
  ./build-docker.sh --help
  ```

#### 8. **Makefile.docker**
- **位置**: `./Makefile.docker`
- **用途**: Make 风格的便捷命令集合
- **命令分类**:
  - **构建命令**: build, build-gateway, build-world, build-game, build-db
  - **运行命令**: up, down, restart, restart-*
  - **监控命令**: logs, logs-*, ps, stats, top
  - **扩展命令**: scale-game, scale-game-3, scale-game-5
  - **调试命令**: shell-*, psql, redis-cli
  - **清理命令**: clean, clean-volumes, clean-images, clean-all
  - **备份命令**: backup-db, restore-db, backup-volumes
  - **验证命令**: health-check, version
  - **快捷命令**: rebuild, reset, quick-start
- **使用示例**:
  ```bash
  # 查看所有可用命令
  make -f Makefile.docker help

  # 一键快速启动
  make -f Makefile.docker quick-start

  # 扩展 Game 服务到 3 个实例
  make -f Makefile.docker scale-game-3

  # 备份数据库
  make -f Makefile.docker backup-db
  ```

### CI/CD 配置

#### 9. **.github/workflows/docker-build.yml**
- **位置**: `./.github/workflows/docker-build.yml`
- **用途**: GitHub Actions 自动化构建流程
- **触发条件**:
  - Push 到 master/main 分支
  - Pull Request
  - 版本标签创建
- **功能**:
  - 自动构建所有服务镜像
  - 推送到 GitHub Container Registry (ghcr.io)
  - 多标签策略（branch, tag, sha, latest）
  - 构建缓存优化
  - 测试验证集成

### 文档资料

#### 10. **DOCKER_QUICKSTART.md**
- **位置**: `./DOCKER_QUICKSTART.md`
- **内容概览**:
  - ⚡ 5分钟快速部署流程
  - ✅ 部署验证步骤
  - 📋 服务端口说明
  - 🔧 常用命令速查表
  - 🚀 快速验证脚本
  - 🔐 生产环境配置
  - ❓ 常见问题解答
  - 📊 性能优化建议
- **适用人群**: 新手、快速上手
- **阅读时间**: 10 分钟

#### 11. **DOCKER_DEPLOYMENT.md**
- **位置**: `./DOCKER_DEPLOYMENT.md`
- **内容概览**:
  - 📐 架构设计详解
  - 🎯 先决条件检查
  - 📦 首次部署完整流程
  - 🔄 滚动更新策略
  - 🧪 验证测试方法
  - 🔍 故障排查指南
  - 🛠️ 运维操作手册
  - 🔐 安全加固建议
  - 📈 性能调优方案
- **适用人群**: 运维工程师、生产部署
- **阅读时间**: 30 分钟

#### 12. **DOCKER_VERIFICATION.md**
- **位置**: `./DOCKER_VERIFICATION.md`
- **内容概览**:
  - 🏗️ 构建验证（镜像大小、多阶段构建、缓存）
  - 🚀 启动验证（容器状态、健康检查、端口）
  - ⚙️ 功能验证（服务通信、数据库、缓存）
  - 📊 性能验证（资源使用、并发测试、扩展）
  - 🔐 安全验证（用户权限、网络隔离、敏感数据）
  - 📝 日志验证（日志输出、持久化、轮转）
  - 🔍 故障排查（常见问题、诊断工具）
- **适用人群**: QA工程师、部署验证
- **阅读时间**: 15 分钟

#### 13. **DOCKER_FILES_SUMMARY.md**
- **位置**: `./DOCKER_FILES_SUMMARY.md`
- **内容概览**:
  - 📂 文件列表和用途
  - 🔗 文件关系图
  - 📖 快速使用指南
  - 🎬 常见使用场景
  - ⚙️ CMake 构建参数
  - 🔌 端口映射表
  - 💾 数据卷说明
  - 🔐 安全特性
  - 📈 性能优化
- **适用人群**: 快速查阅、速查手册
- **阅读时间**: 5 分钟

---

## ⚡ 快速开始

### 方法 1: 一键部署（最快）

```bash
# 1. 克隆项目
git clone https://github.com/your-org/mir2-cpp.git
cd mir2-cpp

# 2. 启动所有服务（使用默认配置）
docker compose up -d

# 3. 验证部署
docker compose ps
```

**就这么简单！🎉**

### 方法 2: 使用 Makefile（推荐）

```bash
# 1. 查看所有可用命令
make -f Makefile.docker help

# 2. 一键构建并启动
make -f Makefile.docker quick-start

# 3. 查看日志
make -f Makefile.docker logs
```

### 方法 3: 生产部署

```bash
# 1. 复制并配置环境变量
cp .env.example .env
vim .env  # 修改密码和生产配置

# 2. 使用生产配置启动
docker compose -f docker-compose.yml -f docker-compose.prod.yml up -d

# 3. 验证部署
make -f Makefile.docker health-check
```

### 验证部署成功

```bash
# 查看服务状态（所有服务应为 Up/healthy）
docker compose ps

# 测试端口连通性
nc -zv localhost 7000  # Gateway
nc -zv localhost 7100  # World
nc -zv localhost 7200  # Game

# 检查数据库
docker compose exec postgres pg_isready -U mir2
# 输出: accepting connections ✓

# 检查 Redis
docker compose exec redis redis-cli ping
# 输出: PONG ✓
```

---

## 🏗️ 架构概览

### 服务架构图

```
┌──────────────────────────────────────────────────────────────┐
│                        客户端层                               │
│                     (TCP/UDP 连接)                            │
└──────────────────────────────────────────────────────────────┘
                            ↓
┌──────────────────────────────────────────────────────────────┐
│  Gateway (7000)                                              │
│  - 客户端接入                                                 │
│  - 负载均衡                                                   │
│  - 路由转发                                                   │
└──────────────────────────────────────────────────────────────┘
                            ↓
        ┌───────────────────┴───────────────────┐
        ↓                                       ↓
┌──────────────────┐                  ┌──────────────────┐
│  World (7100)    │ ←──────────────→ │  Game (7200+)    │
│  - 全局状态      │    双向通信        │  - 地图实例      │
│  - 玩家分配      │                   │  - 战斗系统      │
│  - ECS管理       │                   │  - 怪物AI        │
│  - 事件总线      │                   │  - 技能系统      │
└──────────────────┘                  └──────────────────┘
        ↓                                       ↓
┌──────────────────────────────────────────────────────────────┐
│  DB Service (7300)                                           │
│  - 数据库代理                                                 │
│  - 连接池管理                                                 │
│  - 查询优化                                                   │
└──────────────────────────────────────────────────────────────┘
        ↓                                       ↓
┌──────────────────┐                  ┌──────────────────┐
│ PostgreSQL (5432)│                  │  Redis (6379)    │
│ - 玩家数据       │                  │  - 会话缓存      │
│ - 物品装备       │                  │  - 排行榜        │
│ - 游戏配置       │                  │  - 实时数据      │
└──────────────────┘                  └──────────────────┘
```

### 端口映射表

| 服务       | 容器端口 | 宿主机端口 | 协议  | 说明                  |
|-----------|---------|-----------|------|----------------------|
| Gateway   | 7000    | 7000      | TCP  | 🎮 客户端连接入口      |
| World     | 7100    | 7100      | TCP  | 🌍 世界服务器          |
| Game      | 7200    | 7200-7210 | TCP  | ⚔️ 游戏逻辑（可扩展）  |
| DB        | 7300    | 7300      | TCP  | 💾 数据库代理          |
| PostgreSQL| 5432    | 5432*     | TCP  | 🗄️ 数据库（开发环境）  |
| Redis     | 6379    | 6379*     | TCP  | ⚡ 缓存（开发环境）     |

**注**: 生产环境建议不暴露数据库端口

### 数据持久化

| 数据卷名          | 挂载点                      | 用途          |
|------------------|----------------------------|--------------|
| postgres_data    | /var/lib/postgresql/data   | PostgreSQL 数据 |
| redis_data       | /data                      | Redis 持久化 |
| gateway_logs     | /opt/mir2/logs             | Gateway 日志 |
| world_logs       | /opt/mir2/logs             | World 日志   |
| game_logs        | /opt/mir2/logs             | Game 日志    |
| db_logs          | /opt/mir2/logs             | DB 日志      |

---

## 💡 最佳实践

### 开发环境

#### 1. 使用开发配置启动
```bash
docker compose -f docker-compose.yml -f docker-compose.dev.yml up -d
```

#### 2. 启用热重载
```yaml
# docker-compose.dev.yml
volumes:
  - ./src:/opt/mir2/src:ro
```

#### 3. 查看实时日志
```bash
# 查看所有服务日志
docker compose logs -f

# 查看特定服务日志
docker compose logs -f gateway
```

#### 4. 快速调试
```bash
# 进入容器
docker compose exec gateway bash

# 连接数据库
make -f Makefile.docker psql

# 连接 Redis
make -f Makefile.docker redis-cli
```

### 生产环境

#### 1. 安全配置清单

- ✅ 修改所有默认密码
- ✅ 使用强密码（32字符以上）
- ✅ 不暴露数据库端口到公网
- ✅ 配置防火墙规则
- ✅ 启用 SSL/TLS 加密
- ✅ 定期安全审计

```bash
# 生成强密码示例
POSTGRES_PASS=$(openssl rand -base64 32)
REDIS_PASS=$(openssl rand -base64 24)
JWT_SECRET=$(openssl rand -base64 64)
```

#### 2. 资源限制配置

```yaml
# docker-compose.prod.yml
deploy:
  resources:
    limits:
      cpus: '2.0'
      memory: 2G
    reservations:
      cpus: '1.0'
      memory: 1G
```

#### 3. 健康检查配置

```yaml
healthcheck:
  test: ["CMD", "pg_isready", "-U", "mir2"]
  interval: 10s
  timeout: 5s
  retries: 5
  start_period: 30s
```

#### 4. 日志管理

```yaml
logging:
  driver: "json-file"
  options:
    max-size: "10m"
    max-file: "3"
```

#### 5. 自动重启策略

```yaml
restart: unless-stopped
```

#### 6. 备份策略

```bash
# 定时备份数据库（每天凌晨2点）
0 2 * * * cd /path/to/mir2-cpp && \
  make -f Makefile.docker backup-db

# 定时备份数据卷（每周日凌晨3点）
0 3 * * 0 cd /path/to/mir2-cpp && \
  make -f Makefile.docker backup-volumes
```

### 性能优化

#### 1. 横向扩展 Game 服务

```bash
# 扩展到 3 个实例
docker compose up -d --scale game=3

# 或使用 Makefile
make -f Makefile.docker scale-game-3
```

#### 2. 资源监控

```bash
# 实时资源使用情况
docker stats

# 或使用 Makefile
make -f Makefile.docker stats
```

#### 3. 构建优化

```bash
# 使用构建缓存
docker compose build

# 并行构建
docker compose build --parallel
```

#### 4. 网络优化

```yaml
# 使用 host 网络模式（仅Linux生产环境）
network_mode: "host"
```

---

## 🎬 常见场景

### 场景 1: 首次部署（新项目）

```bash
# 1. 克隆项目
git clone https://github.com/your-org/mir2-cpp.git
cd mir2-cpp

# 2. 配置环境变量
cp .env.example .env
vim .env

# 3. 一键启动
make -f Makefile.docker quick-start

# 4. 验证部署
make -f Makefile.docker health-check
```

### 场景 2: 开发调试

```bash
# 1. 启动开发环境（仅数据库）
make -f Makefile.docker dev-up

# 2. 本地编译运行
mkdir build && cd build
cmake .. && make -j$(nproc)

# 3. 查看数据库日志
make -f Makefile.docker logs-postgres

# 4. 进入数据库调试
make -f Makefile.docker psql
```

### 场景 3: 生产部署

```bash
# 1. 生成安全密码
POSTGRES_PASS=$(openssl rand -base64 32)
sed -i "s/^POSTGRES_PASSWORD=.*/POSTGRES_PASSWORD=$POSTGRES_PASS/" .env

# 2. 使用生产配置启动
docker compose -f docker-compose.yml -f docker-compose.prod.yml up -d

# 3. 配置防火墙（仅开放 Gateway 端口）
sudo ufw allow 7000/tcp
sudo ufw enable

# 4. 验证部署
make -f Makefile.docker health-check
```

### 场景 4: 性能压测

```bash
# 1. 扩展 Game 服务
make -f Makefile.docker scale-game-5

# 2. 查看资源使用
make -f Makefile.docker stats

# 3. 查看日志
make -f Makefile.docker logs-game
```

### 场景 5: 故障恢复

```bash
# 1. 停止所有服务
docker compose down

# 2. 恢复数据库备份
make -f Makefile.docker restore-db BACKUP_FILE=backup/mir2_backup_20260201.sql

# 3. 重新启动服务
docker compose up -d

# 4. 验证恢复
make -f Makefile.docker health-check
```

### 场景 6: 版本更新

```bash
# 1. 备份当前数据
make -f Makefile.docker backup-db
make -f Makefile.docker backup-volumes

# 2. 拉取最新代码
git pull origin master

# 3. 重新构建镜像
docker compose build --no-cache

# 4. 滚动更新（逐个重启）
docker compose up -d --no-deps gateway
sleep 10
docker compose up -d --no-deps world
sleep 10
docker compose up -d --no-deps game

# 5. 验证更新
make -f Makefile.docker health-check
```

### 场景 7: 数据迁移

```bash
# 1. 在旧服务器备份
make -f Makefile.docker backup-db
make -f Makefile.docker backup-volumes

# 2. 复制备份文件到新服务器
scp -r backup/ user@new-server:/path/to/mir2-cpp/

# 3. 在新服务器恢复
make -f Makefile.docker restore-db BACKUP_FILE=backup/mir2_backup_latest.sql

# 4. 启动服务
docker compose up -d
```

---

## 🔍 故障排查

### 常见问题速查表

| 问题现象 | 可能原因 | 解决方案 |
|---------|---------|---------|
| 容器无法启动 | 端口被占用 | `sudo lsof -i :7000` 查找并释放端口 |
| 服务状态 Restarting | 配置错误或资源不足 | `docker compose logs <service>` 查看日志 |
| 数据库连接失败 | 数据库未就绪 | `docker compose exec postgres pg_isready -U mir2` |
| Redis 连接失败 | Redis 未启动 | `docker compose exec redis redis-cli ping` |
| 服务间无法通信 | 网络配置问题 | `docker network inspect mir2-cpp_mir2-network` |
| 镜像构建失败 | 缓存损坏 | `docker compose build --no-cache` |
| 磁盘空间不足 | 日志或数据过多 | `docker system prune -a` 清理未使用资源 |

### 诊断命令速查

```bash
# 查看容器状态
docker compose ps

# 查看详细日志
docker compose logs -f <service>

# 查看资源使用
docker stats

# 进入容器调试
docker compose exec <service> bash

# 检查网络
docker network ls
docker network inspect mir2-cpp_mir2-network

# 检查数据卷
docker volume ls
docker volume inspect mir2-cpp_postgres_data

# 健康检查
make -f Makefile.docker health-check

# 查看端口监听
sudo netstat -tlnp | grep -E '7000|7100|7200|7300|5432|6379'
```

### 日志分析

```bash
# 查看最近 100 行日志
docker compose logs --tail=100 gateway

# 查看错误日志
docker compose logs | grep -i error

# 查看最近 1 小时的日志
docker compose logs --since 1h

# 导出日志到文件
docker compose logs > mir2-logs-$(date +%Y%m%d_%H%M%S).log
```

### 性能分析

```bash
# 查看容器资源使用
docker stats --no-stream

# 查看进程列表
docker compose top

# 查看网络流量
docker stats --format "table {{.Container}}\t{{.NetIO}}"
```

---

## 📚 扩展阅读

### 官方文档

- [Docker 官方文档](https://docs.docker.com/)
- [Docker Compose 文档](https://docs.docker.com/compose/)
- [PostgreSQL Docker 镜像](https://hub.docker.com/_/postgres)
- [Redis Docker 镜像](https://hub.docker.com/_/redis)

### 进阶主题

#### Kubernetes 部署

- 将 Docker Compose 配置转换为 Kubernetes YAML
- 使用 Kompose 工具自动转换
- 配置 Ingress 和 LoadBalancer
- 设置 HPA（水平Pod自动扩展）

#### 监控和日志

- 接入 Prometheus + Grafana 监控
- 配置 ELK（Elasticsearch + Logstash + Kibana）日志聚合
- 使用 Jaeger 进行分布式追踪
- 配置告警系统（AlertManager）

#### 安全加固

- 使用 Secrets 管理敏感数据
- 配置 TLS/SSL 证书
- 实施网络策略（Network Policies）
- 镜像安全扫描（Trivy, Clair）
- 运行时安全（Falco）

#### CI/CD 集成

- GitHub Actions 自动化部署
- GitLab CI/CD 流水线
- Jenkins Pipeline 集成
- ArgoCD GitOps 部署

### 相关项目文档

- [项目主文档](../README.md)
- [架构设计文档](../docs/P0_architecture_plan.md)
- [NPC 系统设计](../docs/npc_system_design.md)

---

## 🆘 获取帮助

### 命令行帮助

```bash
# Makefile 命令帮助
make -f Makefile.docker help

# 构建脚本帮助
./build-docker.sh --help

# Docker Compose 帮助
docker compose --help
```

### 社区支持

- **报告问题**: [GitHub Issues](https://github.com/your-org/mir2-cpp/issues)
- **功能请求**: [GitHub Discussions](https://github.com/your-org/mir2-cpp/discussions)
- **安全问题**: security@example.com

### 快速链接

- [快速入门指南](../DOCKER_QUICKSTART.md) - 5分钟快速上手
- [完整部署指南](../DOCKER_DEPLOYMENT.md) - 生产环境部署
- [验证测试清单](../DOCKER_VERIFICATION.md) - 部署验证
- [文件说明总结](../DOCKER_FILES_SUMMARY.md) - 文件用途速查

---

## 📝 版本信息

- **文档版本**: 1.0.0
- **最后更新**: 2026-02-01
- **适用 Docker 版本**: 20.10+
- **适用 Docker Compose 版本**: 2.0+

---

## 📄 许可证

本文档遵循项目主许可证。

---

**提示**:
- ✅ 新手从 [快速入门指南](../DOCKER_QUICKSTART.md) 开始
- ✅ 生产部署前务必修改所有默认密码
- ✅ 定期备份数据库和重要数据
- ✅ 使用 `make -f Makefile.docker help` 查看所有可用命令

**祝您部署顺利！** 🚀
