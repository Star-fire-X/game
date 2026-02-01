# MIR2-CPP Docker 部署完整指南

本文档提供 MIR2-CPP 项目的完整 Docker 部署指南，涵盖从准备工作到生产运维的全流程。

## 目录

- [1. 项目概览](#1-项目概览)
- [2. 先决条件](#2-先决条件)
- [3. 首次部署流程](#3-首次部署流程)
- [4. 生产/开发环境部署差异](#4-生产开发环境部署差异)
- [5. 更新部署和滚动更新](#5-更新部署和滚动更新)
- [6. 验证测试](#6-验证测试)
- [7. 常见问题排查](#7-常见问题排查)
- [8. 运维操作](#8-运维操作)

---

## 1. 项目概览

### 1.1 架构概述

MIR2-CPP 是一个分布式游戏服务器架构，采用微服务设计，由以下核心组件组成：

```
┌────────────────────────────────────────────────────────────────┐
│                        客户端连接层                              │
└────────────────────────────────────────────────────────────────┘
                              ↓
┌────────────────────────────────────────────────────────────────┐
│  Gateway (7000)  - 客户端接入网关，负载均衡和路由                │
└────────────────────────────────────────────────────────────────┘
                              ↓
        ┌─────────────────────┴─────────────────────┐
        ↓                                           ↓
┌──────────────────┐                    ┌──────────────────┐
│  World (7100)    │ ←──────────────→  │  Game (7200+)    │
│  世界服务器       │                    │  游戏逻辑服务器   │
│  - 全局状态      │                    │  - 地图实例      │
│  - 玩家分配      │                    │  - 战斗系统      │
│  - ECS管理       │                    │  - 怪物AI        │
└──────────────────┘                    └──────────────────┘
        ↓                                           ↓
┌──────────────────────────────────────────────────────────────┐
│  DB Service (7300)  - 数据库代理服务                           │
└──────────────────────────────────────────────────────────────┘
        ↓                                           ↓
┌──────────────────┐                    ┌──────────────────┐
│ PostgreSQL (5432)│                    │  Redis (6379)    │
│ 持久化数据库      │                    │  缓存和会话      │
└──────────────────┘                    └──────────────────┘
```

### 1.2 服务端口映射

| 服务名      | 容器端口 | 宿主机端口   | 用途说明                     |
|------------|---------|-------------|------------------------------|
| Gateway    | 7000    | 7000        | 客户端连接入口                |
| World      | 7100    | 7100        | 世界服务内部通信              |
| Game       | 7200    | 7200-7210   | 游戏逻辑服务（支持多实例）     |
| DB         | 7300    | 7300        | 数据库代理服务                |
| PostgreSQL | 5432    | 5432        | 数据库（生产环境不暴露）       |
| Redis      | 6379    | 6379        | 缓存（生产环境不暴露）         |
| Prometheus | 9090    | 9090        | 监控数据采集（可选）          |
| Grafana    | 3000    | 3000        | 监控可视化（可选）            |

### 1.3 技术栈

- **容器化**: Docker 24.x, Docker Compose v2.x
- **编译构建**: CMake 3.20+, Ninja, vcpkg
- **运行时**: Debian Bookworm (Slim)
- **数据库**: PostgreSQL 16, Redis 7
- **监控**: Prometheus, Grafana (可选)

---

## 2. 先决条件

### 2.1 系统要求

#### 最低配置（开发环境）
- CPU: 4 核心
- 内存: 8 GB RAM
- 磁盘: 20 GB 可用空间
- 操作系统: Linux (推荐 Ubuntu 22.04+), macOS 12+, Windows 10/11 (WSL2)

#### 推荐配置（生产环境）
- CPU: 8+ 核心
- 内存: 16+ GB RAM
- 磁盘: 100+ GB SSD
- 网络: 100 Mbps+ 带宽
- 操作系统: Linux (推荐 Ubuntu 22.04 LTS Server)

### 2.2 软件依赖

#### Docker 环境
```bash
# 检查 Docker 版本（需要 20.10+）
docker --version
# 输出示例: Docker version 24.0.5, build ced0996

# 检查 Docker Compose 版本（需要 2.0+）
docker compose version
# 输出示例: Docker Compose version v2.20.2
```

#### 安装 Docker（Ubuntu/Debian）
```bash
# 卸载旧版本
sudo apt-get remove docker docker-engine docker.io containerd runc

# 安装依赖
sudo apt-get update
sudo apt-get install -y ca-certificates curl gnupg lsb-release

# 添加 Docker 官方 GPG 密钥
sudo mkdir -p /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | \
  sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg

# 设置 Docker 仓库
echo \
  "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] \
  https://download.docker.com/linux/ubuntu \
  $(lsb_release -cs) stable" | \
  sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

# 安装 Docker Engine
sudo apt-get update
sudo apt-get install -y docker-ce docker-ce-cli containerd.io \
  docker-buildx-plugin docker-compose-plugin

# 将当前用户添加到 docker 组（避免每次使用 sudo）
sudo usermod -aG docker $USER
newgrp docker

# 验证安装
docker run hello-world
```

### 2.3 网络要求

- 开放入站端口: 7000 (Gateway 客户端连接)
- 内部服务端口: 7100, 7200-7210, 7300, 5432, 6379
- 监控端口（可选）: 3000, 9090, 9093
- 防火墙配置:
  ```bash
  # Ubuntu/Debian (ufw)
  sudo ufw allow 7000/tcp
  sudo ufw allow 3000/tcp  # Grafana (可选)
  ```

### 2.4 磁盘空间规划

```bash
# 推荐的目录结构和空间分配
/opt/mir2/
├── backups/          # 备份数据 (预留 10-50 GB)
├── logs/             # 日志文件 (预留 5-20 GB)
└── volumes/          # Docker 卷数据 (预留 10-50 GB)
    ├── postgres_data/
    ├── redis_data/
    └── monitoring/
```

---

## 3. 首次部署流程

### 3.1 准备部署环境

#### 步骤 1: 克隆代码仓库
```bash
# 克隆项目到本地
git clone https://github.com/your-org/mir2-cpp.git
cd mir2-cpp

# 切换到稳定分支（生产环境）
git checkout master  # 或指定的稳定版本标签，如 v1.0.0
```

#### 步骤 2: 创建环境配置文件
```bash
# 复制环境变量模板
cp .env.example .env

# 编辑配置文件
nano .env  # 或使用 vim/vi
```

#### 步骤 3: 配置关键参数（生产环境必改）

**重要: 以下配置项在生产环境必须修改！**

```bash
# .env 文件关键配置
# ==========================================

# 1. 环境类型（生产环境设置为 production）
ENVIRONMENT=production

# 2. 数据库密码（使用强密码）
POSTGRES_PASSWORD=$(openssl rand -base64 32)
MIR2_DB_PASSWORD=$(openssl rand -base64 32)

# 3. Redis 密码（生产环境建议启用）
REDIS_PASSWORD=$(openssl rand -base64 24)

# 4. JWT 密钥（用于用户认证）
JWT_SECRET=$(openssl rand -base64 64)

# 5. 日志级别（生产环境使用 info 或 warn）
LOG_LEVEL=info

# 6. 资源限制（根据服务器配置调整）
GAME_CPU_LIMIT=4
GAME_MEM_LIMIT=2G
POSTGRES_MEM_LIMIT=2G

# 7. 服务器显示名称
SERVER_DISPLAY_NAME=传奇世界
MAX_ONLINE_PLAYERS=2000
```

**快速生成安全密钥脚本**:
```bash
#!/bin/bash
# generate-secrets.sh - 自动生成安全密钥并更新 .env

echo "生成安全密钥..."

# 生成密钥
POSTGRES_PASS=$(openssl rand -base64 32)
REDIS_PASS=$(openssl rand -base64 24)
JWT_SECRET=$(openssl rand -base64 64)

# 更新 .env 文件
sed -i "s/^POSTGRES_PASSWORD=.*/POSTGRES_PASSWORD=$POSTGRES_PASS/" .env
sed -i "s/^MIR2_DB_PASSWORD=.*/MIR2_DB_PASSWORD=$POSTGRES_PASS/" .env
sed -i "s/^REDIS_PASSWORD=.*/REDIS_PASSWORD=$REDIS_PASS/" .env
sed -i "s/^JWT_SECRET=.*/JWT_SECRET=$JWT_SECRET/" .env

echo "✓ 密钥已生成并保存到 .env 文件"
echo "请妥善保管 .env 文件，不要提交到版本控制系统！"
```

### 3.2 构建镜像

#### 方式 1: 使用构建脚本（推荐）
```bash
# 赋予执行权限
chmod +x build-docker.sh

# 构建所有服务镜像
./build-docker.sh all

# 或单独构建某个服务
./build-docker.sh gateway
./build-docker.sh world
./build-docker.sh game
./build-docker.sh db
```

#### 方式 2: 使用 Docker Compose 构建
```bash
# 构建所有服务
docker compose build

# 不使用缓存重新构建
docker compose build --no-cache

# 并行构建（加快速度）
docker compose build --parallel
```

#### 构建选项说明
```bash
# 查看帮助信息
./build-docker.sh --help

# 常用选项
-n, --no-cache          # 不使用构建缓存（确保最新版本）
-t, --tag TAG           # 指定镜像标签（如 v1.0.0）
-r, --registry REGISTRY # 指定私有仓库前缀
-p, --push              # 构建后推送到仓库

# 示例：构建并标记版本
./build-docker.sh -t v1.0.0 all

# 示例：构建并推送到私有仓库
./build-docker.sh -r registry.example.com/mir2 -p all
```

**预期构建输出**:
```
[INFO] MIR2 Docker 构建脚本
[INFO] ====================
[INFO] 构建 gateway 服务...
[INFO] 镜像名称: mir2-gateway:latest
...
[INFO] ✓ gateway 构建完成
[INFO] 构建 world 服务...
...
[INFO] ====================
[INFO] 构建完成！
[INFO] 已构建的镜像:
REPOSITORY          TAG       IMAGE ID       CREATED          SIZE
mir2-gateway        latest    abc123def456   10 seconds ago   120MB
mir2-world          latest    def456ghi789   15 seconds ago   150MB
mir2-game           latest    ghi789jkl012   20 seconds ago   150MB
mir2-db             latest    jkl012mno345   25 seconds ago   110MB
```

### 3.3 初始化数据库

#### 步骤 1: 检查数据库迁移文件
```bash
# 查看迁移脚本
ls -lh migrations/

# 输出示例:
# 001_create_accounts.sql
# 002_create_characters.sql
# 003_create_equipment.sql
# ...
```

#### 步骤 2: 启动数据库服务（仅启动 PostgreSQL）
```bash
# 仅启动 PostgreSQL 和 Redis
docker compose up -d postgres redis

# 等待数据库就绪
docker compose exec postgres pg_isready -U mir2
# 输出: /var/run/postgresql:5432 - accepting connections
```

#### 步骤 3: 数据库初始化（自动执行）
数据库初始化会在 PostgreSQL 容器首次启动时自动执行 `migrations/` 目录下的 SQL 脚本。

**验证数据库初始化**:
```bash
# 连接数据库检查表结构
docker compose exec postgres psql -U mir2 -d mir2 -c "\dt"

# 输出示例:
#              List of relations
#  Schema |       Name        | Type  | Owner
# --------+-------------------+-------+-------
#  public | accounts          | table | mir2
#  public | characters        | table | mir2
#  public | equipment         | table | mir2
#  ...
```

### 3.4 启动服务

#### 步骤 1: 启动所有服务（开发环境）
```bash
# 前台启动（查看实时日志）
docker compose up

# 后台启动
docker compose up -d

# 查看启动日志
docker compose logs -f
```

#### 步骤 2: 启动所有服务（生产环境）
```bash
# 使用生产配置覆盖
docker compose -f docker-compose.yml -f docker-compose.prod.yml up -d

# 查看服务状态
docker compose ps

# 输出示例:
# NAME               COMMAND              SERVICE    STATUS      PORTS
# mir2-postgres      "docker-entry..."    postgres   Up 2 min    0.0.0.0:5432->5432/tcp
# mir2-redis         "docker-entry..."    redis      Up 2 min    0.0.0.0:6379->6379/tcp
# mir2-db            "/opt/mir2/bin..."   db         Up 1 min    0.0.0.0:7300->7300/tcp
# mir2-world         "/opt/mir2/bin..."   world      Up 1 min    0.0.0.0:7100->7100/tcp
# mir2-game-1        "/opt/mir2/bin..."   game       Up 1 min    0.0.0.0:7200->7200/tcp
# mir2-gateway       "/opt/mir2/bin..."   gateway    Up 30 sec   0.0.0.0:7000->7000/tcp
```

#### 步骤 3: 检查服务健康状态
```bash
# 使用 docker compose ps 查看健康状态
docker compose ps

# 检查 PostgreSQL 健康状态
docker compose exec postgres pg_isready -U mir2

# 检查 Redis 健康状态
docker compose exec redis redis-cli ping
# 输出: PONG

# 查看各服务日志
docker compose logs gateway
docker compose logs world
docker compose logs game
docker compose logs db
```

### 3.5 部署监控系统（可选但推荐）

#### 启动监控栈
```bash
# 进入监控目录
cd monitoring

# 启动 Prometheus + Grafana + Alertmanager
docker compose up -d

# 返回主目录
cd ..

# 验证监控服务
curl http://localhost:9090/-/healthy  # Prometheus
curl http://localhost:3000/api/health # Grafana
```

#### 访问监控界面
- **Grafana**: http://localhost:3000
  - 用户名: `admin`
  - 密码: `admin` (首次登录后修改)
  - 仪表盘: "Legend2 Gateway Dashboard"

- **Prometheus**: http://localhost:9090
  - 查看采集的指标: `gateway_forward_total`

- **Alertmanager**: http://localhost:9093
  - 查看活跃告警

### 3.6 首次部署验证清单

完成以下检查确保部署成功：

- [ ] 所有容器状态为 `Up` (使用 `docker compose ps` 检查)
- [ ] PostgreSQL 健康检查通过 (`pg_isready`)
- [ ] Redis 健康检查通过 (`redis-cli ping`)
- [ ] Gateway 服务监听 7000 端口 (`netstat -tlnp | grep 7000`)
- [ ] 数据库表结构正确 (执行 `\dt` 查看)
- [ ] 日志文件正常生成 (检查 `logs/` 目录)
- [ ] 监控指标正常采集（如已启用）

---

## 4. 生产/开发环境部署差异

### 4.1 配置对比表

| 配置项               | 开发环境                      | 生产环境                      |
|---------------------|------------------------------|-------------------------------|
| Compose 配置文件     | `docker-compose.yml`         | `docker-compose.yml` + `docker-compose.prod.yml` |
| 环境变量 ENVIRONMENT | `development`                | `production`                  |
| 日志级别 LOG_LEVEL   | `debug`                      | `info` 或 `warn`              |
| 健康检查             | 禁用（快速启动）               | 启用（确保可用性）             |
| 端口暴露             | 全部暴露（便于调试）           | 仅暴露必要端口                 |
| 自动重启策略         | `restart: "no"`              | `restart: on-failure:3`       |
| 资源限制             | 无限制                        | 严格限制（CPU/Memory）         |
| 日志大小限制         | 无限制                        | `max-size: 10m`, `max-file: 3` |
| SSL/TLS             | 禁用                          | 启用（推荐）                   |
| 密码强度             | 简单密码（如 `mir2_password`）| 强随机密码                     |

### 4.2 开发环境部署

#### 启动命令
```bash
# 使用开发配置
docker compose -f docker-compose.yml -f docker-compose.dev.yml up

# 或使用默认配置（已包含开发友好设置）
docker compose up
```

#### 开发环境特点
```yaml
# docker-compose.dev.yml 关键配置

# 1. 所有端口暴露
ports:
  - "5432:5432"  # PostgreSQL
  - "6379:6379"  # Redis
  - "7000:7000"  # Gateway
  - "7100:7100"  # World
  - "7200-7210:7200"  # Game

# 2. 调试级别日志
environment:
  - LOG_LEVEL=debug

# 3. 禁用健康检查（加快启动）
healthcheck:
  disable: true

# 4. 不自动重启
restart: "no"
```

#### 开发环境调试技巧
```bash
# 进入容器调试
docker compose exec gateway bash

# 实时查看日志
docker compose logs -f --tail=100 gateway

# 重启单个服务
docker compose restart world

# 查看资源使用
docker stats
```

### 4.3 生产环境部署

#### 启动命令
```bash
# 使用生产配置
docker compose -f docker-compose.yml -f docker-compose.prod.yml up -d
```

#### 生产环境特点
```yaml
# docker-compose.prod.yml 关键配置

# 1. 资源限制
deploy:
  resources:
    limits:
      cpus: '4'
      memory: 2G
    reservations:
      cpus: '2'
      memory: 1G

# 2. 重启策略
restart: on-failure:3

# 3. 日志限制
logging:
  driver: json-file
  options:
    max-size: "10m"
    max-file: "3"

# 4. 移除不必要的端口暴露
postgres:
  ports: []  # 不对外暴露
redis:
  ports: []  # 不对外暴露
```

#### 生产环境安全加固
```bash
# 1. 设置防火墙规则
sudo ufw enable
sudo ufw allow 7000/tcp  # 仅允许 Gateway 端口
sudo ufw allow 22/tcp    # SSH 管理端口

# 2. 限制 Docker 网络访问
# 在 docker-compose.yml 中配置内部网络
networks:
  mir2-network:
    driver: bridge
    internal: false  # 生产环境设置为 true 以隔离外部网络

# 3. 使用密钥文件管理敏感信息（推荐）
# 创建 Docker Secrets
echo "strong_password_here" | docker secret create postgres_password -
```

### 4.4 环境切换操作

#### 从开发切换到生产
```bash
# 1. 停止开发环境
docker compose down

# 2. 备份开发数据（如需保留）
docker run --rm -v mir2-cpp_postgres_data:/data \
  -v $(pwd)/backups:/backup \
  debian:bookworm-slim \
  tar czf /backup/dev-postgres-$(date +%Y%m%d).tar.gz -C /data .

# 3. 更新 .env 配置为生产环境
sed -i 's/ENVIRONMENT=development/ENVIRONMENT=production/' .env
sed -i 's/LOG_LEVEL=debug/LOG_LEVEL=info/' .env

# 4. 启动生产环境
docker compose -f docker-compose.yml -f docker-compose.prod.yml up -d

# 5. 验证部署
docker compose ps
docker compose logs -f
```

---

## 5. 更新部署和滚动更新

### 5.1 版本更新流程

#### 步骤 1: 准备更新
```bash
# 1. 拉取最新代码
git fetch origin
git checkout v1.1.0  # 切换到新版本标签

# 2. 检查变更日志
cat CHANGELOG.md

# 3. 检查配置文件是否有新增项
diff .env.example .env

# 4. 备份当前数据（重要！）
./scripts/backup.sh  # 或手动备份
```

#### 步骤 2: 构建新版本镜像
```bash
# 使用版本标签构建
./build-docker.sh -t v1.1.0 all

# 验证新镜像构建成功
docker images | grep mir2
# 输出:
# mir2-gateway   v1.1.0   ...
# mir2-gateway   latest   ...
```

#### 步骤 3: 执行更新（停机更新）
```bash
# 停止旧版本服务
docker compose down

# 启动新版本服务
docker compose up -d

# 查看启动日志
docker compose logs -f
```

#### 步骤 4: 验证更新
```bash
# 检查服务状态
docker compose ps

# 检查版本号（如果服务提供版本接口）
curl http://localhost:7000/version

# 查看日志确认无错误
docker compose logs --tail=50 gateway
```

### 5.2 零停机滚动更新（Game 服务）

Game 服务支持多实例部署，可实现零停机滚动更新。

#### 方式 1: 使用 Docker Compose Scale
```bash
# 1. 扩容到 2 个实例
docker compose up -d --scale game=2

# 等待新实例就绪
docker compose ps | grep game

# 2. 构建新版本镜像
./build-docker.sh -t v1.1.0 game

# 3. 更新 docker-compose.yml 使用新镜像
# image: mir2-game:v1.1.0

# 4. 滚动重启（逐个替换实例）
# 重启第一个实例
docker compose up -d --no-deps --scale game=2 game

# 等待第一个实例就绪后，重启第二个
docker compose restart game-2

# 5. 验证所有实例运行新版本
docker compose ps game
```

### 5.3 数据库迁移更新

#### 场景 1: 添加新表或字段（向后兼容）
```bash
# 1. 准备迁移脚本
# migrations/011_add_new_feature.sql

# 2. 将迁移脚本复制到容器
docker cp migrations/011_add_new_feature.sql mir2-postgres:/tmp/

# 3. 执行迁移
docker compose exec postgres psql -U mir2 -d mir2 -f /tmp/011_add_new_feature.sql

# 4. 验证迁移成功
docker compose exec postgres psql -U mir2 -d mir2 -c "\d new_table_name"
```

#### 场景 2: 修改表结构（需要停机）
```bash
# 1. 停止应用服务（保留数据库运行）
docker compose stop gateway world game db

# 2. 备份数据库
docker compose exec postgres pg_dump -U mir2 mir2 > backup_before_migration.sql

# 3. 执行迁移脚本
docker compose exec postgres psql -U mir2 -d mir2 -f /tmp/schema_change.sql

# 4. 验证迁移结果
docker compose exec postgres psql -U mir2 -d mir2 -c "\d modified_table"

# 5. 启动应用服务
docker compose start db world game gateway
```

### 5.4 回滚操作

#### 快速回滚（使用旧镜像）
```bash
# 1. 停止当前服务
docker compose down

# 2. 修改 docker-compose.yml 使用旧版本镜像
# image: mir2-gateway:v1.0.0  # 从 v1.1.0 回滚到 v1.0.0

# 3. 启动旧版本服务
docker compose up -d

# 4. 验证回滚成功
docker compose ps
docker compose logs -f
```

---

## 6. 验证测试

### 6.1 服务健康检查

#### 检查所有服务状态
```bash
# 使用 Docker Compose 查看
docker compose ps

# 预期输出（所有服务状态为 Up）
# NAME           STATUS          PORTS
# mir2-postgres  Up (healthy)    0.0.0.0:5432->5432/tcp
# mir2-redis     Up (healthy)    0.0.0.0:6379->6379/tcp
# mir2-db        Up 2 minutes    0.0.0.0:7300->7300/tcp
# mir2-world     Up 2 minutes    0.0.0.0:7100->7100/tcp
# mir2-game-1    Up 1 minute     0.0.0.0:7200->7200/tcp
# mir2-gateway   Up 1 minute     0.0.0.0:7000->7000/tcp
```

#### 检查端口监听
```bash
# 检查 Gateway 端口
netstat -tlnp | grep 7000
# 或使用 ss
ss -tlnp | grep 7000

# 输出示例:
# tcp   0   0 0.0.0.0:7000   0.0.0.0:*   LISTEN   12345/mir2_gateway

# 批量检查所有端口
for port in 7000 7100 7200 7300 5432 6379; do
  echo -n "Port $port: "
  nc -zv localhost $port 2>&1 | grep -q succeeded && echo "OK" || echo "FAIL"
done
```

### 6.2 数据库连接测试

#### PostgreSQL 连接测试
```bash
# 方式 1: 使用 pg_isready
docker compose exec postgres pg_isready -U mir2 -d mir2
# 输出: /var/run/postgresql:5432 - accepting connections

# 方式 2: 直接连接测试
docker compose exec postgres psql -U mir2 -d mir2 -c "SELECT version();"

# 方式 3: 从宿主机连接
psql -h localhost -p 5432 -U mir2 -d mir2 -c "SELECT current_database();"

# 检查数据库表
docker compose exec postgres psql -U mir2 -d mir2 -c "\dt"

# 检查数据库大小
docker compose exec postgres psql -U mir2 -d mir2 -c \
  "SELECT pg_size_pretty(pg_database_size('mir2'));"
```

#### Redis 连接测试
```bash
# 方式 1: 使用 redis-cli ping
docker compose exec redis redis-cli ping
# 输出: PONG

# 方式 2: 测试读写
docker compose exec redis redis-cli SET test_key "hello"
docker compose exec redis redis-cli GET test_key
# 输出: "hello"

# 方式 3: 检查 Redis 信息
docker compose exec redis redis-cli INFO server

# 从宿主机连接
redis-cli -h localhost -p 6379 ping
```

### 6.3 服务间通信测试

#### 测试 Gateway → World 通信
```bash
# 查看 Gateway 日志确认连接到 World
docker compose logs gateway | grep -i "world.*connected"

# 输出示例:
# gateway | [INFO] Connected to World service at world:7100
```

#### 测试 World → DB 通信
```bash
# 查看 World 日志确认连接到 DB
docker compose logs world | grep -i "db.*connected"

# 输出示例:
# world | [INFO] Connected to DB service at db:7300
```

#### 网络连通性测试
```bash
# 从 Gateway 容器 ping World
docker compose exec gateway ping -c 3 world

# 从 World 容器测试 DB 服务端口
docker compose exec world nc -zv db 7300

# 从 Game 容器测试 Redis 连接
docker compose exec game nc -zv redis 6379
```

### 6.4 客户端连接测试

#### 使用 telnet 测试 Gateway 端口
```bash
# 测试 TCP 连接
telnet localhost 7000

# 或使用 nc (netcat)
nc -zv localhost 7000
# 输出: Connection to localhost 7000 port [tcp/*] succeeded!
```

### 6.5 日志验证

#### 检查日志文件生成
```bash
# 查看日志目录结构
docker compose exec gateway ls -lh /opt/mir2/logs/
docker compose exec world ls -lh /opt/mir2/logs/
docker compose exec game ls -lh /opt/mir2/logs/
docker compose exec db ls -lh /opt/mir2/logs/

# 从宿主机访问日志（通过卷挂载）
ls -lh ./logs/gateway/
ls -lh ./logs/world/
ls -lh ./logs/game/
ls -lh ./logs/db/
```

#### 检查日志内容
```bash
# 查看 Gateway 启动日志
docker compose logs gateway | head -20

# 查看最近的错误日志
docker compose logs gateway | grep -i error

# 实时跟踪日志
docker compose logs -f --tail=50 gateway

# 查看所有服务的日志
docker compose logs --tail=10
```

### 6.6 完整验证脚本

创建自动化验证脚本 `scripts/validate-deployment.sh`:

```bash
#!/bin/bash
# validate-deployment.sh - 自动化部署验证脚本

set -e

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "========================================="
echo "MIR2-CPP 部署验证脚本"
echo "========================================="

# 1. 检查 Docker 服务
echo -e "\n${YELLOW}[1/7] 检查 Docker 服务状态...${NC}"
docker compose ps
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ Docker 服务正常${NC}"
else
    echo -e "${RED}✗ Docker 服务异常${NC}"
    exit 1
fi

# 2. 检查端口监听
echo -e "\n${YELLOW}[2/7] 检查端口监听...${NC}"
for port in 7000 7100 7200 7300 5432 6379; do
    if nc -zv localhost $port 2>&1 | grep -q succeeded; then
        echo -e "${GREEN}✓ Port $port 正常${NC}"
    else
        echo -e "${RED}✗ Port $port 未监听${NC}"
    fi
done

# 3. PostgreSQL 连接测试
echo -e "\n${YELLOW}[3/7] 测试 PostgreSQL 连接...${NC}"
if docker compose exec -T postgres pg_isready -U mir2 | grep -q "accepting connections"; then
    echo -e "${GREEN}✓ PostgreSQL 连接正常${NC}"
else
    echo -e "${RED}✗ PostgreSQL 连接失败${NC}"
fi

# 4. Redis 连接测试
echo -e "\n${YELLOW}[4/7] 测试 Redis 连接...${NC}"
if docker compose exec -T redis redis-cli ping | grep -q "PONG"; then
    echo -e "${GREEN}✓ Redis 连接正常${NC}"
else
    echo -e "${RED}✗ Redis 连接失败${NC}"
fi

# 5. 检查日志错误
echo -e "\n${YELLOW}[5/7] 检查日志错误...${NC}"
ERROR_COUNT=$(docker compose logs --tail=100 | grep -ci "error\|fatal" || true)
if [ "$ERROR_COUNT" -eq 0 ]; then
    echo -e "${GREEN}✓ 无错误日志${NC}"
else
    echo -e "${YELLOW}⚠ 发现 $ERROR_COUNT 条错误日志${NC}"
fi

# 6. 检查服务间连接
echo -e "\n${YELLOW}[6/7] 检查服务间连接...${NC}"
if docker compose logs gateway | grep -qi "connected to world"; then
    echo -e "${GREEN}✓ Gateway → World 连接正常${NC}"
else
    echo -e "${YELLOW}⚠ Gateway → World 连接未确认${NC}"
fi

# 7. 检查数据库表
echo -e "\n${YELLOW}[7/7] 检查数据库表结构...${NC}"
TABLE_COUNT=$(docker compose exec -T postgres psql -U mir2 -d mir2 -t -c "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='public';" | tr -d ' ')
if [ "$TABLE_COUNT" -gt 0 ]; then
    echo -e "${GREEN}✓ 数据库表初始化成功（$TABLE_COUNT 张表）${NC}"
else
    echo -e "${RED}✗ 数据库表初始化失败${NC}"
fi

echo -e "\n========================================="
echo -e "${GREEN}验证完成！${NC}"
echo "========================================="
```

---

## 7. 常见问题排查

### 7.1 服务无法启动

#### 问题 1: 端口被占用
**症状**:
```
Error starting userland proxy: listen tcp4 0.0.0.0:7000: bind: address already in use
```

**排查步骤**:
```bash
# 1. 查找占用端口的进程
sudo lsof -i :7000
# 或
sudo netstat -tlnp | grep 7000

# 2. 停止占用端口的进程
sudo kill 12345

# 3. 或修改 docker-compose.yml 使用不同端口
# ports:
#   - "17000:7000"  # 宿主机使用 17000 端口
```

#### 问题 2: 健康检查失败
**症状**:
```
mir2-postgres | [WARN] Health check failed
```

**排查步骤**:
```bash
# 1. 查看详细日志
docker compose logs postgres

# 2. 手动执行健康检查命令
docker compose exec postgres pg_isready -U mir2

# 3. 检查数据库是否正在启动（需要时间）
docker compose exec postgres psql -U mir2 -c "SELECT 1;"

# 4. 如果持续失败，检查资源是否充足
docker stats
```

### 7.2 数据库连接问题

#### 问题 1: 连接被拒绝
**症状**:
```
psql: error: connection to server at "localhost" (127.0.0.1), port 5432 failed: Connection refused
```

**排查步骤**:
```bash
# 1. 检查 PostgreSQL 容器是否运行
docker compose ps postgres

# 2. 检查端口映射
docker compose port postgres 5432

# 3. 检查 PostgreSQL 日志
docker compose logs postgres | tail -50

# 4. 尝试从容器内部连接
docker compose exec postgres psql -U mir2 -d mir2

# 5. 检查防火墙规则
sudo ufw status
```

### 7.3 网络连接问题

#### 问题 1: 容器间无法通信
**症状**:
```
game | [ERROR] Failed to connect to world:7100: Name or service not known
```

**排查步骤**:
```bash
# 1. 检查 Docker 网络
docker network ls
docker network inspect mir2-cpp_mir2-network

# 2. 检查容器是否在同一网络
docker compose ps --format json | jq '.[].Networks'

# 3. 测试容器间 DNS 解析
docker compose exec game ping -c 3 world

# 4. 测试端口连通性
docker compose exec game nc -zv world 7100

# 5. 重建网络
docker compose down
docker network prune
docker compose up -d
```

### 7.4 性能问题

#### 问题 1: 高 CPU 使用率
**排查步骤**:
```bash
# 1. 查看资源使用详情
docker stats --no-stream

# 2. 查看容器内进程
docker compose exec game top

# 3. 检查日志是否有异常循环
docker compose logs --tail=100 game | grep -i "loop\|infinite"

# 4. 临时限制 CPU 使用
# 在 docker-compose.yml 中添加:
# deploy:
#   resources:
#     limits:
#       cpus: '2'
```

---

## 8. 运维操作

### 8.1 备份恢复

#### 8.1.1 数据库备份

**自动备份脚本** (`scripts/backup.sh`):
```bash
#!/bin/bash
# backup.sh - 自动备份脚本

set -e

BACKUP_DIR="/opt/mir2/backups"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

echo "开始备份: $TIMESTAMP"

# 创建备份目录
mkdir -p "$BACKUP_DIR"

# 1. 备份 PostgreSQL
echo "备份 PostgreSQL..."
docker compose exec -T postgres pg_dump -U mir2 mir2 | \
  gzip > "$BACKUP_DIR/postgres_$TIMESTAMP.sql.gz"

# 2. 备份 Redis（RDB 快照）
echo "备份 Redis..."
docker compose exec redis redis-cli BGSAVE
sleep 2
docker cp mir2-redis:/data/dump.rdb "$BACKUP_DIR/redis_$TIMESTAMP.rdb"

# 3. 备份配置文件
echo "备份配置文件..."
tar czf "$BACKUP_DIR/config_$TIMESTAMP.tar.gz" config/ .env

# 4. 清理旧备份（保留最近 7 天）
echo "清理旧备份..."
find "$BACKUP_DIR" -name "*.gz" -mtime +7 -delete
find "$BACKUP_DIR" -name "*.rdb" -mtime +7 -delete

echo "备份完成: $BACKUP_DIR"
ls -lh "$BACKUP_DIR"
```

**定时备份（crontab）**:
```bash
# 编辑 crontab
crontab -e

# 添加每日凌晨 2 点执行备份
0 2 * * * /opt/mir2-cpp/scripts/backup.sh >> /var/log/mir2-backup.log 2>&1
```

#### 8.1.2 数据恢复

**恢复 PostgreSQL**:
```bash
# 1. 停止应用服务（保留数据库运行）
docker compose stop gateway world game db

# 2. 恢复数据库
gunzip < /opt/mir2/backups/postgres_20240115_020000.sql.gz | \
  docker compose exec -T postgres psql -U mir2 -d mir2

# 3. 验证数据
docker compose exec postgres psql -U mir2 -d mir2 -c "SELECT COUNT(*) FROM accounts;"

# 4. 重启应用服务
docker compose start db world game gateway
```

### 8.2 日志管理

#### 8.2.1 查看日志

**实时日志**:
```bash
# 查看所有服务日志
docker compose logs -f

# 查看特定服务日志
docker compose logs -f gateway

# 查看最近 100 行日志
docker compose logs --tail=100 game

# 查看指定时间范围日志
docker compose logs --since 2024-01-15T10:00:00 --until 2024-01-15T11:00:00 gateway
```

#### 8.2.2 日志轮转配置

**Docker 日志驱动配置** (已在 `docker-compose.prod.yml`):
```yaml
logging:
  driver: json-file
  options:
    max-size: "10m"    # 单个日志文件最大 10MB
    max-file: "3"      # 保留最近 3 个日志文件
```

### 8.3 资源监控

#### 8.3.1 实时监控

**使用 docker stats**:
```bash
# 实时查看所有容器资源使用
docker stats

# 查看特定容器
docker stats mir2-gateway mir2-world mir2-game-1

# 格式化输出
docker stats --no-stream --format \
  "table {{.Container}}\t{{.CPUPerc}}\t{{.MemUsage}}\t{{.NetIO}}\t{{.BlockIO}}"
```

#### 8.3.2 Prometheus 监控（推荐）

**启动监控栈**:
```bash
cd monitoring
docker compose up -d
```

**关键监控指标**:
- **Gateway 指标**:
  - `gateway_forward_total`: 总消息转发量
  - `gateway_route_table_connection_count`: 连接数
  - `gateway_user_register`: 用户注册数

### 8.4 安全维护

#### 8.4.1 定期安全更新

**更新基础镜像**:
```bash
# 1. 拉取最新基础镜像
docker pull debian:bookworm-slim
docker pull postgres:16-bookworm
docker pull redis:7-bookworm

# 2. 重新构建应用镜像
./build-docker.sh --no-cache all

# 3. 重启服务
docker compose down
docker compose up -d
```

#### 8.4.2 密钥轮转

**轮转数据库密码**:
```bash
# 1. 生成新密码
NEW_PASSWORD=$(openssl rand -base64 32)

# 2. 更新 PostgreSQL 密码
docker compose exec postgres psql -U mir2 -c \
  "ALTER USER mir2 WITH PASSWORD '$NEW_PASSWORD';"

# 3. 更新 .env 文件
sed -i "s/^POSTGRES_PASSWORD=.*/POSTGRES_PASSWORD=$NEW_PASSWORD/" .env
sed -i "s/^MIR2_DB_PASSWORD=.*/MIR2_DB_PASSWORD=$NEW_PASSWORD/" .env

# 4. 重启依赖服务
docker compose restart db world game
```

---

## 附录

### A. 完整命令速查表

| 操作                 | 命令                                                |
|---------------------|-----------------------------------------------------|
| 构建所有镜像         | `./build-docker.sh all`                            |
| 启动所有服务（开发）  | `docker compose up -d`                             |
| 启动所有服务（生产）  | `docker compose -f docker-compose.yml -f docker-compose.prod.yml up -d` |
| 查看服务状态         | `docker compose ps`                                |
| 查看实时日志         | `docker compose logs -f`                           |
| 停止所有服务         | `docker compose down`                              |
| 重启单个服务         | `docker compose restart gateway`                   |
| 进入容器调试         | `docker compose exec gateway bash`                 |
| 备份数据库           | `./scripts/backup.sh`                              |
| 查看资源使用         | `docker stats`                                     |
| 清理未使用资源       | `docker system prune -a`                           |
| 扩容服务实例         | `docker compose up -d --scale game=3`              |

### B. 相关文档

- [Docker Compose 官方文档](https://docs.docker.com/compose/)
- [PostgreSQL 官方文档](https://www.postgresql.org/docs/)
- [Redis 官方文档](https://redis.io/documentation)
- [Prometheus 监控文档](monitoring/README.md)

---

**文档版本**: 1.0
**最后更新**: 2024-01-15
**维护者**: MIR2-CPP 运维团队
