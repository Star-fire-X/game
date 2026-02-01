# Docker 部署验证清单

本文档提供完整的 Docker 部署验证步骤，确保所有服务正常运行。

## 目录

- [1. 构建验证](#1-构建验证)
- [2. 启动验证](#2-启动验证)
- [3. 功能验证](#3-功能验证)
- [4. 性能验证](#4-性能验证)
- [5. 安全验证](#5-安全验证)
- [6. 日志验证](#6-日志验证)
- [7. 故障排查](#7-故障排查)

---

## 1. 构建验证

### 1.1 镜像大小检查

**测试命令：**
```bash
# 查看所有镜像大小
docker images | grep mir2

# 详细查看镜像层
docker history mir2-cpp:latest

# 检查镜像总大小
docker images mir2-cpp:latest --format "table {{.Repository}}\t{{.Tag}}\t{{.Size}}"
```

**期望结果：**
- 最终镜像大小：≤ 200MB（使用多阶段构建）
- 构建阶段镜像：可以较大（gcc、vcpkg 依赖）
- 运行时镜像：仅包含必要的运行时库

**优化建议：**
```bash
# 如果镜像过大，检查是否包含不必要的文件
docker run --rm mir2-cpp:latest ls -lh /app
docker run --rm mir2-cpp:latest find /app -type f -size +10M
```

### 1.2 多阶段构建验证

**测试命令：**
```bash
# 验证构建阶段
docker build --target builder -t mir2-builder:test .

# 验证只复制了必要的文件
docker run --rm mir2-cpp:latest ls -la /app/

# 检查是否包含源代码（不应该包含）
docker run --rm mir2-cpp:latest find /app -name "*.cpp" -o -name "*.h"
```

**期望结果：**
- ✅ 只包含编译后的可执行文件
- ✅ 包含必要的配置文件
- ❌ 不包含源代码
- ❌ 不包含构建工具（cmake、gcc等）

### 1.3 层缓存验证

**测试命令：**
```bash
# 第一次构建
time docker build -t mir2-cpp:test1 .

# 无代码改动的第二次构建
time docker build -t mir2-cpp:test2 .

# 仅修改源代码后构建
echo "// test" >> src/server/main.cc
time docker build -t mir2-cpp:test3 .
```

**期望结果：**
- 第一次构建：完整构建时间
- 第二次构建：≈ 1-5秒（全部使用缓存）
- 修改代码后：仅重新编译修改的部分

---

## 2. 启动验证

### 2.1 容器状态检查

**测试命令：**
```bash
# 启动所有服务
docker-compose up -d

# 检查容器状态
docker-compose ps

# 检查容器健康状态
docker-compose ps --format "table {{.Name}}\t{{.Status}}\t{{.Health}}"

# 详细查看每个容器
docker inspect --format='{{.Name}}: {{.State.Status}} (Health: {{.State.Health.Status}})' \
  $(docker-compose ps -q)
```

**期望结果：**
```
NAME                 STATUS              HEALTH
mir2-db              Up 30 seconds       healthy
mir2-redis           Up 30 seconds       healthy
mir2-gateway         Up 25 seconds       healthy
mir2-game-1          Up 20 seconds       healthy
mir2-game-2          Up 20 seconds       healthy
```

### 2.2 依赖顺序验证

**测试命令：**
```bash
# 查看启动顺序和时间
docker-compose logs --timestamps | grep "started" | head -20

# 验证 depends_on 配置
docker-compose config | grep -A 5 "depends_on"

# 手动测试依赖顺序
docker-compose down
docker-compose up -d db redis
sleep 5
docker-compose up -d gateway game
```

**期望结果：**
1. ✅ PostgreSQL 首先启动
2. ✅ Redis 同时或稍后启动
3. ✅ Gateway 等待数据库健康后启动
4. ✅ Game 服务器最后启动

### 2.3 健康检查验证

**测试命令：**
```bash
# 检查健康检查配置
docker inspect mir2-db | jq '.[0].State.Health'

# 查看健康检查日志
docker inspect mir2-gateway | jq '.[0].State.Health.Log'

# 手动执行健康检查
docker exec mir2-db pg_isready -U mir2user
docker exec mir2-redis redis-cli ping
docker exec mir2-gateway nc -zv localhost 7000
```

**期望结果：**
```bash
# PostgreSQL
/var/run/postgresql:5432 - accepting connections

# Redis
PONG

# Gateway
Connection to localhost 7000 port [tcp/*] succeeded!
```

### 2.4 重启恢复测试

**测试命令：**
```bash
# 测试单个容器重启
docker restart mir2-gateway
docker logs -f mir2-gateway

# 测试崩溃自动重启
docker exec mir2-game-1 kill -9 1
sleep 5
docker-compose ps mir2-game-1

# 测试整体重启
docker-compose restart
docker-compose ps
```

**期望结果：**
- ✅ 容器在 10 秒内重启
- ✅ 自动重连数据库和 Redis
- ✅ 重新注册到 Gateway
- ✅ 日志中无错误信息

---

## 3. 功能验证

### 3.1 数据库连接验证

**测试命令：**
```bash
# 连接到数据库
docker exec -it mir2-db psql -U mir2user -d mir2db

# 测试查询
docker exec mir2-db psql -U mir2user -d mir2db -c "\dt"
docker exec mir2-db psql -U mir2user -d mir2db -c "SELECT version();"

# 检查应用日志中的数据库连接
docker logs mir2-gateway 2>&1 | grep -i "database\|postgres"
docker logs mir2-game-1 2>&1 | grep -i "database\|postgres"

# 验证连接池
docker exec mir2-db psql -U mir2user -d mir2db -c \
  "SELECT datname, count(*) FROM pg_stat_activity GROUP BY datname;"
```

**期望结果：**
```sql
-- 数据库版本
PostgreSQL 15.x on x86_64-pc-linux-gnu

-- 连接数
 datname | count
---------+-------
 mir2db  |     5

-- 应用日志
[INFO] Database connected successfully
[INFO] Connection pool initialized: min=2, max=10
```

### 3.2 Redis 连接验证

**测试命令：**
```bash
# 连接到 Redis
docker exec -it mir2-redis redis-cli

# 测试基本操作
docker exec mir2-redis redis-cli SET test_key "test_value"
docker exec mir2-redis redis-cli GET test_key
docker exec mir2-redis redis-cli DEL test_key

# 检查 Redis 信息
docker exec mir2-redis redis-cli INFO stats
docker exec mir2-redis redis-cli INFO clients

# 检查应用使用的 Redis 键
docker exec mir2-redis redis-cli KEYS "*"

# 验证发布/订阅
docker exec mir2-redis redis-cli PUBSUB CHANNELS
```

**期望结果：**
```bash
# 基本操作
OK
"test_value"
(integer) 1

# 客户端连接数
connected_clients:5

# 应用键（示例）
1) "session:*"
2) "player:online:*"
3) "server:status:*"
```

### 3.3 服务间通信验证

**测试命令：**
```bash
# 测试 Gateway -> Game Server 通信
docker exec mir2-gateway nc -zv mir2-game-1 7100
docker exec mir2-gateway nc -zv mir2-game-2 7100

# 测试服务发现
docker logs mir2-gateway | grep "game server registered"
docker logs mir2-game-1 | grep "registered to gateway"

# 检查网络连接
docker network inspect mir2-network | jq '.[0].Containers'

# 测试跨容器 DNS 解析
docker exec mir2-gateway ping -c 3 mir2-db
docker exec mir2-game-1 ping -c 3 mir2-redis

# 验证消息转发
docker logs mir2-gateway | grep "forwarding message"
```

**期望结果：**
```
# 服务注册日志
[INFO] Game server registered: id=game-1, address=mir2-game-1:7100
[INFO] Game server registered: id=game-2, address=mir2-game-2:7100

# 网络连接
✓ Gateway -> Game-1: Connected
✓ Gateway -> Game-2: Connected
✓ Game -> Database: Connected
✓ Game -> Redis: Connected
```

### 3.4 客户端连接验证

**测试命令：**
```bash
# 使用 telnet 测试连接
telnet localhost 7000

# 使用 netcat 测试
echo "test" | nc localhost 7000

# 检查端口监听
docker exec mir2-gateway netstat -tlnp | grep 7000

# 模拟客户端连接测试
docker run --rm --network=mir2-network nicolaka/netshoot \
  nc -zv mir2-gateway 7000

# 查看连接日志
docker logs mir2-gateway | tail -20

# 检查并发连接数
docker exec mir2-gateway netstat -an | grep :7000 | wc -l
```

**期望结果：**
```
# 端口监听
tcp        0      0 0.0.0.0:7000            0.0.0.0:*               LISTEN      1/gateway

# 连接日志
[INFO] Client connected from 172.18.0.5:54321
[INFO] Session created: session_id=xxx
```

### 3.5 端到端功能测试

**测试脚本：**
```bash
#!/bin/bash
# test-e2e.sh

echo "=== 端到端功能测试 ==="

# 1. 测试用户注册
echo "1. 测试用户注册..."
curl -X POST http://localhost:8080/api/register \
  -H "Content-Type: application/json" \
  -d '{"username":"testuser","password":"testpass"}'

# 2. 测试用户登录
echo "2. 测试用户登录..."
TOKEN=$(curl -X POST http://localhost:8080/api/login \
  -H "Content-Type: application/json" \
  -d '{"username":"testuser","password":"testpass"}' \
  | jq -r '.token')

# 3. 测试角色创建
echo "3. 测试角色创建..."
curl -X POST http://localhost:8080/api/character \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"name":"TestChar","class":"warrior"}'

# 4. 验证数据持久化
echo "4. 验证数据库数据..."
docker exec mir2-db psql -U mir2user -d mir2db \
  -c "SELECT username FROM users WHERE username='testuser';"

echo "=== 测试完成 ==="
```

---

## 4. 性能验证

### 4.1 资源使用监控

**测试命令：**
```bash
# 实时监控所有容器
docker stats

# 查看特定容器资源使用
docker stats mir2-gateway --no-stream

# 检查内存使用详情
docker exec mir2-game-1 ps aux --sort=-%mem | head -10

# 检查 CPU 使用详情
docker exec mir2-game-1 top -bn1 | head -20

# 查看磁盘使用
docker exec mir2-db df -h

# 导出资源使用报告
docker stats --no-stream --format \
  "table {{.Container}}\t{{.CPUPerc}}\t{{.MemUsage}}\t{{.NetIO}}\t{{.BlockIO}}"
```

**期望结果：**
```
CONTAINER        CPU %   MEM USAGE / LIMIT     MEM %   NET I/O         BLOCK I/O
mir2-gateway     5%      256MiB / 512MiB       50%     1.2MB / 850KB   0B / 0B
mir2-game-1      15%     512MiB / 1GiB         50%     2.5MB / 1.8MB   0B / 0B
mir2-game-2      15%     512MiB / 1GiB         50%     2.5MB / 1.8MB   0B / 0B
mir2-db          8%      384MiB / 2GiB         19%     800KB / 1.2MB   5MB / 12MB
mir2-redis       2%      128MiB / 512MiB       25%     500KB / 500KB   0B / 0B
```

### 4.2 响应时间测试

**测试命令：**
```bash
# 安装测试工具
# apt-get install apache2-utils

# HTTP 接口性能测试
ab -n 1000 -c 10 http://localhost:8080/api/health

# 使用 curl 测试响应时间
for i in {1..10}; do
  curl -w "@curl-format.txt" -o /dev/null -s http://localhost:8080/api/health
done

# curl-format.txt 内容：
cat > curl-format.txt << 'EOF'
    time_namelookup:  %{time_namelookup}s\n
       time_connect:  %{time_connect}s\n
    time_appconnect:  %{time_appconnect}s\n
   time_pretransfer:  %{time_pretransfer}s\n
      time_redirect:  %{time_redirect}s\n
 time_starttransfer:  %{time_starttransfer}s\n
                    ----------\n
         time_total:  %{time_total}s\n
EOF

# 数据库查询性能
docker exec mir2-db psql -U mir2user -d mir2db -c "\timing on" \
  -c "SELECT COUNT(*) FROM users;"

# Redis 性能基准测试
docker exec mir2-redis redis-cli --latency
docker exec mir2-redis redis-benchmark -q -n 10000
```

**期望结果：**
```bash
# HTTP 响应时间
Connection Times (ms)
              min  mean[+/-sd] median   max
Total:        5    12   3.4     11      45

# 数据库查询
Time: 1.234 ms

# Redis 基准测试
PING_INLINE: 50000.00 requests per second
PING_BULK: 52631.58 requests per second
SET: 51282.05 requests per second
GET: 52083.33 requests per second
```

### 4.3 并发连接测试

**测试脚本：**
```bash
#!/bin/bash
# concurrent-test.sh

echo "=== 并发连接测试 ==="

# 创建多个并发连接
for i in {1..100}; do
  (
    echo "Client $i connecting..."
    nc localhost 7000 << EOF
test message
EOF
  ) &
done

# 等待所有连接完成
wait

# 检查连接统计
docker logs mir2-gateway | grep "Client connected" | wc -l
docker exec mir2-gateway netstat -an | grep :7000 | grep ESTABLISHED | wc -l

echo "=== 测试完成 ==="
```

### 4.4 长时间稳定性测试

**测试命令：**
```bash
# 启动长时间监控（运行 1 小时）
docker stats --format \
  "table {{.Container}}\t{{.CPUPerc}}\t{{.MemUsage}}\t{{.MemPerc}}" \
  > stats_$(date +%Y%m%d_%H%M%S).log &

# 每 5 分钟记录一次
for i in {1..12}; do
  echo "=== Check at $(date) ===" | tee -a stability.log
  docker-compose ps >> stability.log
  docker stats --no-stream >> stability.log
  sleep 300
done

# 检查内存泄漏
docker exec mir2-game-1 ps aux | awk '{print $6}' | tail -n +2 | \
  awk '{sum+=$1} END {print "Total Memory: " sum/1024 " MB"}'
```

---

## 5. 安全验证

### 5.1 端口暴露检查

**测试命令：**
```bash
# 检查所有暴露的端口
docker-compose ps --format "table {{.Name}}\t{{.Ports}}"

# 检查宿主机监听端口
netstat -tlnp | grep docker

# 扫描开放端口
nmap localhost

# 验证内部端口不对外暴露
nmap localhost -p 5432,6379,7100

# 检查防火墙规则
iptables -L -n | grep docker
```

**期望结果：**
```
✅ 对外暴露：
- 7000 (Gateway - 客户端连接)

❌ 不应暴露：
- 5432 (PostgreSQL - 仅容器内部)
- 6379 (Redis - 仅容器内部)
- 7100 (Game Server - 仅容器内部)
```

### 5.2 文件权限检查

**测试命令：**
```bash
# 检查应用文件权限
docker exec mir2-gateway ls -la /app/

# 检查配置文件权限
docker exec mir2-gateway find /app/config -type f -ls

# 检查数据目录权限
docker exec mir2-db ls -la /var/lib/postgresql/data/

# 验证敏感文件不可写
docker exec mir2-gateway test -w /app/config/db.yaml && echo "WARN: Config is writable!"

# 检查 secrets 权限
ls -la secrets/
```

**期望结果：**
```bash
# 应用文件
-rwxr-xr-x 1 appuser appuser  gateway
-rw-r--r-- 1 appuser appuser  config.yaml

# 数据库文件
drwx------ 19 postgres postgres  data

# Secrets
-rw------- 1 root root  db_password.txt
-rw------- 1 root root  redis_password.txt
```

### 5.3 用户权限检查

**测试命令：**
```bash
# 检查运行用户
docker exec mir2-gateway whoami
docker exec mir2-game-1 id

# 验证非 root 用户运行
docker exec mir2-gateway ps aux | head -2

# 检查用户权限
docker exec mir2-gateway su - root && echo "WARN: Can switch to root!"

# 验证 sudo 不可用
docker exec mir2-gateway which sudo

# 检查文件系统只读
docker inspect mir2-gateway | jq '.[0].HostConfig.ReadonlyRootfs'
```

**期望结果：**
```bash
# 运行用户
appuser

# UID/GID
uid=1000(appuser) gid=1000(appuser) groups=1000(appuser)

# 进程用户
USER       PID  COMMAND
appuser      1  /app/gateway

# sudo 不存在
(no output)
```

### 5.4 网络隔离验证

**测试命令：**
```bash
# 检查网络配置
docker network ls
docker network inspect mir2-network

# 验证容器间网络隔离
docker run --rm nicolaka/netshoot nc -zv mir2-db 5432
# (应失败，因为不在同一网络)

# 验证内部网络通信
docker exec mir2-gateway nc -zv mir2-db 5432
# (应成功)

# 检查网络策略
docker inspect mir2-gateway | jq '.[0].NetworkSettings.Networks'
```

### 5.5 镜像安全扫描

**测试命令：**
```bash
# 使用 Trivy 扫描镜像漏洞
docker run --rm -v /var/run/docker.sock:/var/run/docker.sock \
  aquasec/trivy image mir2-cpp:latest

# 检查基础镜像版本
docker inspect mir2-cpp:latest | jq '.[0].Config.Labels'

# 验证镜像签名（如果使用）
docker trust inspect mir2-cpp:latest

# 扫描历史层
docker history mir2-cpp:latest --no-trunc
```

---

## 6. 日志验证

### 6.1 日志输出检查

**测试命令：**
```bash
# 查看所有服务日志
docker-compose logs

# 查看特定服务日志
docker-compose logs gateway
docker-compose logs -f game

# 查看最近的日志
docker-compose logs --tail=100

# 查看带时间戳的日志
docker-compose logs -t

# 过滤错误日志
docker-compose logs | grep -i error
docker-compose logs | grep -i warn

# 导出日志到文件
docker-compose logs > logs_$(date +%Y%m%d_%H%M%S).txt

# 检查日志格式
docker logs mir2-gateway 2>&1 | head -10
```

**期望日志格式：**
```
[2026-02-01 10:30:45.123] [INFO] [gateway] Server started on port 7000
[2026-02-01 10:30:46.456] [INFO] [gateway] Connected to database
[2026-02-01 10:30:46.789] [INFO] [gateway] Connected to Redis
[2026-02-01 10:30:50.123] [INFO] [game-1] Game server started
[2026-02-01 10:30:51.456] [INFO] [game-1] Registered to gateway
```

### 6.2 日志级别验证

**测试命令：**
```bash
# 检查不同日志级别
docker logs mir2-gateway | grep "\[DEBUG\]" | wc -l
docker logs mir2-gateway | grep "\[INFO\]" | wc -l
docker logs mir2-gateway | grep "\[WARN\]" | wc -l
docker logs mir2-gateway | grep "\[ERROR\]" | wc -l

# 动态修改日志级别（如果支持）
docker exec mir2-gateway kill -USR1 1  # 切换到 DEBUG 级别

# 验证日志级别配置
docker exec mir2-gateway cat /app/config/gateway.yaml | grep log_level
```

### 6.3 日志轮转检查

**测试命令：**
```bash
# 检查日志驱动配置
docker inspect mir2-gateway | jq '.[0].HostConfig.LogConfig'

# 查看日志文件大小
docker inspect mir2-gateway | jq '.[0].LogPath' | xargs ls -lh

# 验证日志轮转设置
docker-compose config | grep -A 5 "logging:"

# 手动触发日志轮转测试
for i in {1..1000}; do
  docker exec mir2-gateway echo "Test log message $i"
done

# 检查旧日志文件
ls -lh /var/lib/docker/containers/*/
```

**期望配置：**
```yaml
logging:
  driver: "json-file"
  options:
    max-size: "10m"
    max-file: "3"
```

### 6.4 日志聚合验证

**测试命令：**
```bash
# 查看集中日志
docker-compose logs | grep "ERROR" | sort | uniq -c

# 按服务统计日志
echo "=== Gateway Logs ==="
docker logs mir2-gateway 2>&1 | wc -l

echo "=== Game Server Logs ==="
docker logs mir2-game-1 2>&1 | wc -l

# 生成日志报告
cat > log-report.sh << 'EOF'
#!/bin/bash
echo "=== Log Summary Report ==="
echo "Time: $(date)"
echo ""

for service in gateway game-1 game-2 db redis; do
  echo "Service: mir2-$service"
  errors=$(docker logs mir2-$service 2>&1 | grep -i error | wc -l)
  warnings=$(docker logs mir2-$service 2>&1 | grep -i warn | wc -l)
  echo "  Errors: $errors"
  echo "  Warnings: $warnings"
  echo ""
done
EOF

chmod +x log-report.sh
./log-report.sh
```

### 6.5 日志内容验证

**测试检查清单：**
```bash
# 1. 启动日志
docker logs mir2-gateway 2>&1 | grep "Server started" || echo "❌ Missing startup log"

# 2. 数据库连接日志
docker logs mir2-gateway 2>&1 | grep -i "database.*connect" || echo "❌ Missing DB log"

# 3. Redis 连接日志
docker logs mir2-gateway 2>&1 | grep -i "redis.*connect" || echo "❌ Missing Redis log"

# 4. 客户端连接日志
docker logs mir2-gateway 2>&1 | grep "Client connected" || echo "ℹ️ No client connections yet"

# 5. 错误日志检查
errors=$(docker logs mir2-gateway 2>&1 | grep -i "error" | grep -v "0 errors")
if [ -n "$errors" ]; then
  echo "❌ Found errors:"
  echo "$errors"
fi

# 6. 内存/资源警告
docker logs mir2-gateway 2>&1 | grep -i "memory\|oom\|resource" | grep -i "warn\|error"
```

---

## 7. 故障排查

### 7.1 容器无法启动

**诊断步骤：**
```bash
# 1. 查看容器状态
docker-compose ps

# 2. 查看启动日志
docker-compose logs <service_name>

# 3. 检查依赖服务
docker-compose ps db redis

# 4. 检查端口占用
netstat -tlnp | grep -E "7000|5432|6379"

# 5. 检查磁盘空间
df -h

# 6. 检查内存
free -h

# 7. 尝试手动启动
docker-compose up <service_name>
```

### 7.2 数据库连接失败

**诊断步骤：**
```bash
# 1. 验证数据库运行
docker exec mir2-db pg_isready -U mir2user

# 2. 检查连接参数
docker exec mir2-gateway env | grep -i db

# 3. 测试网络连接
docker exec mir2-gateway nc -zv mir2-db 5432

# 4. 检查数据库日志
docker logs mir2-db | tail -50

# 5. 验证密码
docker exec -it mir2-db psql -U mir2user -d mir2db
```

### 7.3 性能问题

**诊断步骤：**
```bash
# 1. 检查资源使用
docker stats --no-stream

# 2. 查看慢查询
docker exec mir2-db psql -U mir2user -d mir2db -c \
  "SELECT * FROM pg_stat_statements ORDER BY total_time DESC LIMIT 10;"

# 3. 检查连接数
docker exec mir2-db psql -U mir2user -d mir2db -c \
  "SELECT count(*) FROM pg_stat_activity;"

# 4. Redis 性能
docker exec mir2-redis redis-cli --latency-history

# 5. 网络延迟
docker exec mir2-gateway ping -c 10 mir2-game-1
```

### 7.4 快速诊断命令

**一键诊断脚本：**
```bash
#!/bin/bash
# diagnose.sh - 快速诊断脚本

echo "=== Docker Compose 部署诊断 ==="
echo "Time: $(date)"
echo ""

# 1. 容器状态
echo "1. 容器状态:"
docker-compose ps
echo ""

# 2. 资源使用
echo "2. 资源使用:"
docker stats --no-stream
echo ""

# 3. 网络连接
echo "3. 网络连接测试:"
docker exec mir2-gateway nc -zv mir2-db 5432
docker exec mir2-gateway nc -zv mir2-redis 6379
docker exec mir2-gateway nc -zv mir2-game-1 7100
echo ""

# 4. 服务健康检查
echo "4. 服务健康:"
docker exec mir2-db pg_isready -U mir2user
docker exec mir2-redis redis-cli ping
echo ""

# 5. 最近错误
echo "5. 最近错误日志:"
docker-compose logs --tail=20 | grep -i error
echo ""

# 6. 磁盘空间
echo "6. 磁盘空间:"
df -h | grep -E "Filesystem|/var/lib/docker"
echo ""

echo "=== 诊断完成 ==="
```

---

## 附录：完整验证脚本

**完整自动化验证脚本：**
```bash
#!/bin/bash
# full-verification.sh - 完整部署验证脚本

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

function check() {
  echo -n "Checking $1... "
}

function pass() {
  echo -e "${GREEN}✓ PASS${NC}"
}

function fail() {
  echo -e "${RED}✗ FAIL${NC}: $1"
  exit 1
}

function warn() {
  echo -e "${YELLOW}⚠ WARN${NC}: $1"
}

echo "========================================="
echo "  MIR2 Docker Deployment Verification"
echo "========================================="
echo ""

# 1. 构建验证
echo "=== 1. Build Verification ==="
check "Docker images exist"
docker images mir2-cpp:latest | grep -q "mir2-cpp" && pass || fail "Image not found"

check "Image size"
size=$(docker images mir2-cpp:latest --format "{{.Size}}")
echo "Image size: $size"

# 2. 启动验证
echo ""
echo "=== 2. Startup Verification ==="
check "All containers running"
running=$(docker-compose ps --filter "status=running" | grep -c "Up" || true)
[ "$running" -ge 5 ] && pass || fail "Not all containers running ($running/5)"

check "Database healthy"
docker exec mir2-db pg_isready -U mir2user > /dev/null 2>&1 && pass || fail "Database not ready"

check "Redis healthy"
docker exec mir2-redis redis-cli ping | grep -q "PONG" && pass || fail "Redis not responding"

# 3. 功能验证
echo ""
echo "=== 3. Functionality Verification ==="
check "Database connection"
docker logs mir2-gateway 2>&1 | grep -qi "database.*connect" && pass || warn "No DB connection log"

check "Redis connection"
docker logs mir2-gateway 2>&1 | grep -qi "redis.*connect" && pass || warn "No Redis connection log"

check "Service registration"
docker logs mir2-game-1 2>&1 | grep -qi "registered" && pass || warn "Service not registered"

check "Port listening"
docker exec mir2-gateway netstat -tln | grep -q ":7000" && pass || fail "Gateway not listening"

# 4. 性能验证
echo ""
echo "=== 4. Performance Verification ==="
check "Resource usage"
cpu=$(docker stats mir2-gateway --no-stream --format "{{.CPUPerc}}" | tr -d '%')
if (( $(echo "$cpu < 50" | bc -l) )); then
  pass
else
  warn "High CPU usage: ${cpu}%"
fi

# 5. 安全验证
echo ""
echo "=== 5. Security Verification ==="
check "Non-root user"
user=$(docker exec mir2-gateway whoami)
[ "$user" != "root" ] && pass || warn "Running as root"

check "Database port not exposed"
! netstat -tln | grep -q "0.0.0.0:5432" && pass || fail "Database port exposed"

# 6. 日志验证
echo ""
echo "=== 6. Log Verification ==="
check "Startup logs"
docker logs mir2-gateway 2>&1 | grep -qi "started" && pass || warn "No startup log"

check "Error logs"
errors=$(docker-compose logs 2>&1 | grep -ci "error" || true)
if [ "$errors" -eq 0 ]; then
  pass
else
  warn "Found $errors error entries"
fi

echo ""
echo "========================================="
echo -e "${GREEN}  Verification Complete!${NC}"
echo "========================================="
```

**使用方法：**
```bash
chmod +x full-verification.sh
./full-verification.sh
```

---

## 总结

通过本验证清单，您可以确保：

✅ **构建质量**：镜像大小合理，多阶段构建正确
✅ **启动可靠**：依赖顺序正确，健康检查通过
✅ **功能完整**：所有服务正常通信，数据持久化
✅ **性能达标**：资源使用合理，响应时间正常
✅ **安全合规**：端口、权限、网络隔离正确
✅ **日志完善**：日志输出正常，轮转配置正确

**建议验证频率：**
- 每次部署后：运行完整验证
- 每日：运行快速健康检查
- 每周：运行性能和安全验证
- 生产上线前：运行完整验证 + 压力测试

有问题请参考故障排查章节或查看日志详细信息。
