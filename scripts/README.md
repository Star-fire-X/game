# Scripts 目录

这个目录包含项目中使用的各种脚本工具。

## wait-for-it.sh

等待 TCP 主机:端口变为可用的脚本，主要用于 Docker 容器启动时等待依赖服务就绪。

### 功能特性

- ✅ **多服务等待**: 同时等待多个主机:端口
- ✅ **超时控制**: 支持自定义超时时间或无限等待
- ✅ **重试机制**: 按指定间隔持续检查服务状态
- ✅ **严格模式**: 超时时强制退出并返回错误码
- ✅ **详细日志**: 彩色日志输出，包含时间戳
- ✅ **延迟控制**: 支持等待前后的延迟时间
- ✅ **兼容性好**: 支持 nc、telnet、/dev/tcp 多种连接方式
- ✅ **命令链式**: 等待完成后可执行指定命令

### 使用示例

#### 1. 基本用法

```bash
# 等待单个服务（默认15秒超时）
./scripts/wait-for-it.sh db:3306

# 等待多个服务
./scripts/wait-for-it.sh db:3306 redis:6379 gateway:50051

# 指定超时时间（30秒）
./scripts/wait-for-it.sh db:3306 -t 30

# 无限等待（超时时间为0）
./scripts/wait-for-it.sh db:3306 -t 0
```

#### 2. 在 Docker Compose 中使用

```yaml
version: '3.8'

services:
  db:
    image: mysql:8.0
    ports:
      - "3306:3306"
    environment:
      MYSQL_ROOT_PASSWORD: example

  redis:
    image: redis:7-alpine
    ports:
      - "6379:6379"

  game-server:
    build: .
    depends_on:
      - db
      - redis
    # 等待依赖服务就绪后再启动
    command: >
      sh -c "./scripts/wait-for-it.sh db:3306 redis:6379 -t 60 -s --
              ./game-server --config /etc/game/config.yaml"
```

#### 3. 在 Dockerfile 中使用

```dockerfile
FROM ubuntu:22.04

# 复制等待脚本
COPY scripts/wait-for-it.sh /usr/local/bin/wait-for-it.sh
RUN chmod +x /usr/local/bin/wait-for-it.sh

# 复制应用程序
COPY build/game-server /usr/local/bin/game-server

# 使用等待脚本作为入口点
ENTRYPOINT ["wait-for-it.sh"]
CMD ["db:3306", "redis:6379", "-t", "60", "-s", "--", "game-server"]
```

#### 4. 严格模式（超时退出）

```bash
# 严格模式：如果30秒内未就绪则退出并返回错误码124
./scripts/wait-for-it.sh db:3306 -t 30 -s -- ./start-server.sh

# 检查退出码
if ./scripts/wait-for-it.sh db:3306 -t 30 -s; then
    echo "数据库已就绪，启动服务器"
    ./start-server.sh
else
    echo "数据库未就绪，启动失败"
    exit 1
fi
```

#### 5. 使用延迟控制

```bash
# 等待前延迟5秒（给服务启动时间）
./scripts/wait-for-it.sh gateway:50051 -w 5 -t 30

# 等待后延迟2秒（确保服务完全就绪）
./scripts/wait-for-it.sh db:3306 -a 2 -- ./migrate-database.sh

# 组合使用
./scripts/wait-for-it.sh db:3306 redis:6379 \
    -w 5 \      # 等待前延迟5秒
    -a 2 \      # 等待后延迟2秒
    -t 60 \     # 超时60秒
    -i 2 \      # 每2秒检查一次
    -s \        # 严格模式
    -- ./start-app.sh
```

#### 6. 静默模式

```bash
# 安静模式：仅在失败时输出错误
./scripts/wait-for-it.sh db:3306 redis:6379 -q -t 60
```

#### 7. 使用环境变量

```bash
# 通过环境变量配置
export WAIT_HOSTS="db:3306 redis:6379 gateway:50051"
export WAIT_HOSTS_TIMEOUT=60
export WAIT_BEFORE_HOSTS=5
export WAIT_AFTER_HOSTS=2
export WAIT_SLEEP_INTERVAL=2

./scripts/wait-for-it.sh -- ./start-server.sh
```

### 命令行选项

| 选项 | 说明 | 默认值 |
|------|------|--------|
| `-h, --help` | 显示帮助信息 | - |
| `-t, --timeout=秒` | 超时时间（0表示无限等待） | 15 |
| `-s, --strict` | 严格模式：超时时退出并返回错误码124 | 关闭 |
| `-q, --quiet` | 安静模式：不输出日志信息 | 关闭 |
| `-w, --wait-before=秒` | 开始等待前的延迟时间 | 0 |
| `-a, --wait-after=秒` | 所有服务就绪后的额外等待时间 | 0 |
| `-i, --interval=秒` | 检查间隔时间 | 1 |
| `--` | 分隔符，后面跟要执行的命令 | - |

### 环境变量

| 变量名 | 说明 |
|--------|------|
| `WAIT_HOSTS` | 要等待的主机列表（空格分隔） |
| `WAIT_HOSTS_TIMEOUT` | 超时时间（秒） |
| `WAIT_BEFORE_HOSTS` | 开始等待前的延迟时间（秒） |
| `WAIT_AFTER_HOSTS` | 所有服务就绪后的额外等待时间（秒） |
| `WAIT_SLEEP_INTERVAL` | 检查间隔时间（秒） |

### 退出码

| 退出码 | 说明 |
|--------|------|
| 0 | 所有服务就绪（或命令执行成功） |
| 1 | 参数错误或命令执行失败 |
| 124 | 超时（仅在严格模式下） |
| 130 | 收到中断信号（Ctrl+C） |

### 实际应用场景

#### 场景1: 游戏服务器启动流程

```bash
#!/bin/bash
# start-game-servers.sh

# 1. 等待数据库和缓存就绪
./scripts/wait-for-it.sh db:3306 redis:6379 -t 60 -s || exit 1

# 2. 运行数据库迁移
./scripts/migrate-database.sh

# 3. 等待网关服务启动
./scripts/wait-for-it.sh gateway:50051 -t 30 -s || exit 1

# 4. 启动游戏服务器
./game-server --config /etc/game/config.yaml
```

#### 场景2: Docker Compose 多服务编排

```yaml
version: '3.8'

services:
  # 数据库服务
  db:
    image: mysql:8.0
    healthcheck:
      test: ["CMD", "mysqladmin", "ping", "-h", "localhost"]
      interval: 5s
      timeout: 3s
      retries: 5

  # Redis 缓存
  redis:
    image: redis:7-alpine
    healthcheck:
      test: ["CMD", "redis-cli", "ping"]
      interval: 5s
      timeout: 3s
      retries: 5

  # 网关服务
  gateway:
    build:
      context: .
      dockerfile: Dockerfile.gateway
    depends_on:
      - db
      - redis
    command: >
      sh -c "wait-for-it.sh db:3306 redis:6379 -t 60 -s --
             gateway-server --port 50051"

  # 游戏服务器
  game-server:
    build:
      context: .
      dockerfile: Dockerfile.game
    depends_on:
      - gateway
      - db
      - redis
    command: >
      sh -c "wait-for-it.sh gateway:50051 db:3306 redis:6379
             -t 90 -w 5 -a 2 -s --
             game-server --gateway gateway:50051"
```

#### 场景3: CI/CD 集成测试

```bash
#!/bin/bash
# run-integration-tests.sh

set -e

# 启动测试环境
docker-compose -f docker-compose.test.yml up -d

# 等待所有服务就绪
./scripts/wait-for-it.sh \
    localhost:3306 \
    localhost:6379 \
    localhost:50051 \
    localhost:8080 \
    -t 120 -s || {
    echo "服务启动失败"
    docker-compose -f docker-compose.test.yml logs
    docker-compose -f docker-compose.test.yml down
    exit 1
}

# 运行集成测试
./run-tests.sh

# 清理环境
docker-compose -f docker-compose.test.yml down
```

### 故障排除

#### 问题1: 脚本没有执行权限

```bash
# 解决方法：添加执行权限
chmod +x scripts/wait-for-it.sh
```

#### 问题2: 缺少 nc (netcat) 工具

脚本会自动尝试多种连接方式：
1. `nc` (netcat) - 推荐
2. `/dev/tcp` + timeout - 备选
3. `telnet` - 兼容性方案

如果都不可用，请安装 netcat：

```bash
# Debian/Ubuntu
apt-get install -y netcat

# Alpine Linux
apk add --no-cache netcat-openbsd

# CentOS/RHEL
yum install -y nc
```

#### 问题3: 在 Windows Git Bash 中运行

确保脚本使用 Unix 行尾（LF）：

```bash
# 转换行尾
dos2unix scripts/wait-for-it.sh

# 或使用 sed
sed -i 's/\r$//' scripts/wait-for-it.sh
```

### 最佳实践

1. **合理设置超时时间**: 根据服务启动时间设置，避免过短或过长
2. **使用严格模式**: 在生产环境中使用 `-s` 确保依赖服务必须就绪
3. **适当的检查间隔**: 对于快速启动的服务使用较短间隔（1秒），慢速服务使用较长间隔（2-5秒）
4. **等待后延迟**: 某些服务端口开放后还需初始化，使用 `-a` 参数等待额外时间
5. **日志记录**: 在生产环境保留日志输出以便问题排查
6. **健康检查配合**: 结合 Docker 健康检查使用效果更佳

### 许可证

MIT License
