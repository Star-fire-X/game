# MIR2-CPP Docker 快速入门

**5 分钟快速部署传奇世界游戏服务器**

## ⚡ 快速开始

### 前置要求

- Docker 20.10+ / Docker Compose 2.0+
- 8GB+ 内存 / 20GB+ 磁盘空间

```bash
# 验证环境
docker --version && docker compose version
```

### 三步部署

```bash
# 1. 克隆并进入项目
git clone https://github.com/your-org/mir2-cpp.git
cd mir2-cpp

# 2. 配置环境（开发环境可跳过）
cp .env.example .env

# 3. 一键启动
docker compose up -d
```

**就这么简单！** 🎉

## ✅ 验证部署

```bash
# 查看服务状态（所有服务应为 Up）
docker compose ps

# 测试数据库连接
docker compose exec postgres pg_isready -U mir2
# 输出: accepting connections ✓

# 测试缓存连接
docker compose exec redis redis-cli ping
# 输出: PONG ✓

# 测试游戏端口
nc -zv localhost 7000
# 输出: Connection succeeded ✓
```

### 期望输出

```
NAME            STATUS         PORTS
mir2-postgres   Up (healthy)   0.0.0.0:5432->5432/tcp
mir2-redis      Up (healthy)   0.0.0.0:6379->6379/tcp
mir2-db         Up 2 minutes   0.0.0.0:7300->7300/tcp
mir2-world      Up 2 minutes   0.0.0.0:7100->7100/tcp
mir2-game-1     Up 1 minute    0.0.0.0:7200->7200/tcp
mir2-gateway    Up 1 minute    0.0.0.0:7000->7000/tcp
```

## 📋 服务端口说明

| 服务       | 端口  | 说明                  |
|-----------|------|----------------------|
| Gateway   | 7000 | 🎮 客户端连接入口       |
| World     | 7100 | 🌍 世界服务器          |
| Game      | 7200 | ⚔️  游戏逻辑服务器      |
| DB        | 7300 | 💾 数据库代理          |
| PostgreSQL| 5432 | 🗄️  数据库             |
| Redis     | 6379 | ⚡ 缓存服务            |

## 🔧 常用命令速查表

### 服务管理

```bash
# 启动所有服务
docker compose up -d

# 停止所有服务
docker compose down

# 重启服务
docker compose restart gateway

# 查看服务状态
docker compose ps

# 扩容 Game 服务
docker compose up -d --scale game=3
```

### 日志查看

```bash
# 实时查看所有日志
docker compose logs -f

# 查看单个服务日志
docker compose logs -f gateway

# 查看最近 100 行
docker compose logs --tail=100 game

# 查看错误日志
docker compose logs | grep -i error
```

### 数据库操作

```bash
# 连接 PostgreSQL
docker compose exec postgres psql -U mir2 -d mir2

# 查看数据库表
docker compose exec postgres psql -U mir2 -d mir2 -c "\dt"

# 备份数据库
docker compose exec postgres pg_dump -U mir2 mir2 > backup.sql

# 恢复数据库
cat backup.sql | docker compose exec -T postgres psql -U mir2 -d mir2

# 连接 Redis
docker compose exec redis redis-cli
```

### 调试诊断

```bash
# 进入容器
docker compose exec gateway bash

# 查看资源使用
docker stats

# 查看容器进程
docker compose top

# 测试网络连通
docker compose exec game ping world
```

### 清理操作

```bash
# 停止并删除容器（保留数据）
docker compose down

# 停止并删除所有数据（⚠️ 危险）
docker compose down -v

# 清理未使用资源
docker system prune -a
```

## 🚀 快速验证脚本

创建 `quick-check.sh` 文件：

```bash
#!/bin/bash
echo "🔍 检查 MIR2 服务状态..."

# 服务状态
echo -e "\n📦 容器状态:"
docker compose ps

# 端口检查
echo -e "\n🔌 端口检查:"
for port in 7000 7100 7200 7300; do
  if nc -zv localhost $port 2>&1 | grep -q succeeded; then
    echo "✓ Port $port 正常"
  else
    echo "✗ Port $port 异常"
  fi
done

# 数据库检查
echo -e "\n💾 数据库检查:"
docker compose exec -T postgres pg_isready -U mir2 | grep -q "accepting" && \
  echo "✓ PostgreSQL 正常" || echo "✗ PostgreSQL 异常"

# Redis 检查
docker compose exec -T redis redis-cli ping | grep -q "PONG" && \
  echo "✓ Redis 正常" || echo "✗ Redis 异常"

echo -e "\n✅ 检查完成！"
```

使用方式：

```bash
chmod +x quick-check.sh
./quick-check.sh
```

## 🔐 生产环境部署

### 1. 安全配置

```bash
# 生成强密码并更新 .env
POSTGRES_PASS=$(openssl rand -base64 32)
REDIS_PASS=$(openssl rand -base64 24)
JWT_SECRET=$(openssl rand -base64 64)

# 自动更新配置文件
sed -i "s/^POSTGRES_PASSWORD=.*/POSTGRES_PASSWORD=$POSTGRES_PASS/" .env
sed -i "s/^MIR2_DB_PASSWORD=.*/MIR2_DB_PASSWORD=$POSTGRES_PASS/" .env
sed -i "s/^REDIS_PASSWORD=.*/REDIS_PASSWORD=$REDIS_PASS/" .env
sed -i "s/^JWT_SECRET=.*/JWT_SECRET=$JWT_SECRET/" .env

# 设置生产环境
sed -i 's/ENVIRONMENT=development/ENVIRONMENT=production/' .env
sed -i 's/LOG_LEVEL=debug/LOG_LEVEL=info/' .env
```

### 2. 启动生产服务

```bash
# 使用生产配置启动
docker compose -f docker-compose.yml -f docker-compose.prod.yml up -d

# 验证部署
docker compose ps
```

### 3. 配置防火墙

```bash
# 仅开放必要端口
sudo ufw allow 7000/tcp    # Gateway 客户端端口
sudo ufw allow 22/tcp      # SSH 管理端口
sudo ufw enable
```

### 4. 定时备份

```bash
# 添加定时任务
crontab -e

# 每日凌晨 2 点备份
0 2 * * * cd /path/to/mir2-cpp && \
  docker compose exec -T postgres pg_dump -U mir2 mir2 | \
  gzip > /backups/mir2_$(date +\%Y\%m\%d).sql.gz
```

## ❓ 常见问题

### 端口被占用

**问题：** `bind: address already in use`

**解决：**

```bash
# 查找占用端口的进程
sudo lsof -i :7000

# 停止进程
sudo kill <PID>

# 或修改端口映射
# 编辑 docker-compose.yml
# ports: - "17000:7000"
```

### 容器无法启动

**问题：** 容器状态为 `Exit` 或 `Restarting`

**解决：**

```bash
# 查看详细日志
docker compose logs gateway

# 检查磁盘空间
df -h

# 检查资源使用
docker stats

# 重新构建
docker compose build --no-cache gateway
docker compose up -d
```

### 数据库连接失败

**问题：** `Connection refused`

**解决：**

```bash
# 检查容器状态
docker compose ps postgres

# 查看日志
docker compose logs postgres

# 手动测试
docker compose exec postgres psql -U mir2 -d mir2

# 重启数据库
docker compose restart postgres
```

### 服务间无法通信

**问题：** `Name or service not known`

**解决：**

```bash
# 检查 Docker 网络
docker network inspect mir2-cpp_mir2-network

# 测试 DNS 解析
docker compose exec game ping world

# 重建网络
docker compose down
docker network prune
docker compose up -d
```

## 📊 性能优化

### 资源调整

编辑 `.env` 文件调整资源限制：

```bash
# 4 核 8GB 服务器示例
GATEWAY_CPU_LIMIT=1
GATEWAY_MEM_LIMIT=1G

GAME_CPU_LIMIT=2
GAME_MEM_LIMIT=2G

POSTGRES_CPU_LIMIT=1
POSTGRES_MEM_LIMIT=2G
```

### 横向扩展

```bash
# 扩容 Game 服务到 3 个实例
docker compose up -d --scale game=3

# 查看扩容结果
docker compose ps game
```

## 📚 下一步阅读

### 深入学习

- **[完整部署指南](DOCKER_DEPLOYMENT.md)** - 生产环境部署详解、故障排查、运维操作
- **[Docker 文件说明](DOCKER_FILES_SUMMARY.md)** - Docker 配置文件详细说明
- **[验证测试指南](DOCKER_VERIFICATION.md)** - 完整的测试验证流程

### 进阶配置

- 配置 SSL/TLS 加密连接
- 设置 Kubernetes 集群部署
- 配置 Redis 主从复制
- 接入 Prometheus + Grafana 监控

### 运维实践

- 制定备份恢复策略
- 配置日志聚合系统（ELK）
- 设置性能监控告警
- 建立故障应急预案

## 🆘 获取帮助

- **报告问题**: [GitHub Issues](https://github.com/your-org/mir2-cpp/issues)
- **项目文档**: [README.md](README.md)
- **架构设计**: [docs/P0_architecture_plan.md](docs/P0_architecture_plan.md)

---

**快速提示：**

- ✅ 开发环境默认配置即可启动
- ⚠️ 生产环境务必修改所有密码
- 🔍 使用 `docker compose logs -f` 查看实时日志
- 🚀 使用 `--scale game=N` 快速扩容

**当前版本**: 1.0.0
**最后更新**: 2024-01-15
