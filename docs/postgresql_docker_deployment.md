# PostgreSQL Docker 部署方案（适配双进程架构）

## 1. 架构前提

当前是双进程：

- `Gateway` 负责客户端接入与转发
- `Logic` 负责游戏逻辑与数据库访问

数据库连接应由 `Logic` 使用，`Gateway` 不应依赖 PostgreSQL 启动顺序。

## 2. 方案说明

本方案使用 `docker-compose.postgres.yml` 提供两个服务：

- `postgres`: 数据库容器（持久化 + 健康检查）
- `db-init`: 一次性迁移引导容器（只在未初始化库时执行 `migrations/*.sql`）

修正点：

- 端口默认仅绑定 `127.0.0.1`，避免对外暴露。
- 迁移由 `db-init` 显式执行，双进程启动流程更可控。
- 明确只要求 `Logic` 配置数据库；`Gateway` 不参与 DB 依赖链。

## 3. 启动步骤

```bash
cp .env.example .env
```

确认 `.env` 至少包含：

```bash
POSTGRES_BIND_IP=127.0.0.1
POSTGRES_PORT=5432
POSTGRES_DB=mir2_game
POSTGRES_USER=mir2
POSTGRES_PASSWORD=mir2_password
MIR2_DB_HOST=127.0.0.1
MIR2_DB_PORT=5432
MIR2_DB_NAME=mir2_game
MIR2_DB_USER=mir2
MIR2_DB_PASSWORD=mir2_password
```

启动数据库与初始化迁移：

```bash
docker compose -f docker-compose.postgres.yml --env-file .env up -d postgres
docker compose -f docker-compose.postgres.yml --env-file .env up db-init
```

验证：

```bash
docker compose -f docker-compose.postgres.yml ps
docker compose -f docker-compose.postgres.yml logs db-init
docker exec -it legend2-postgres psql -U mir2 -d mir2_game -c '\dt'
```

## 4. 双进程服务启动顺序

1. 启动 `postgres`
2. 跑完 `db-init`
3. 启动 `Logic`
4. 启动 `Gateway`

如果 `Logic` 先于数据库启动，当前代码会在初始化阶段直接失败，不会自动等待数据库可用。

## 5. 配置对接（Logic）

`config/logic.yaml` 示例：

```yaml
database:
  host: "127.0.0.1"
  port: 5432
  user: "mir2"
  password: "mir2_password"
  database: "mir2_game"
```

说明：

- 这是“宿主机跑 Gateway/Logic，Docker 跑 PostgreSQL”的推荐模式。
- 若未来把 `Logic` 也容器化，`host` 应改为 `postgres`（容器服务名）。

## 6. 常用运维命令

停止：

```bash
docker compose -f docker-compose.postgres.yml down
```

删除数据卷重建（危险）：

```bash
docker compose -f docker-compose.postgres.yml down -v
```

备份：

```bash
docker exec legend2-postgres pg_dump -U mir2 mir2_game > backup.sql
```

恢复：

```bash
cat backup.sql | docker exec -i legend2-postgres psql -U mir2 -d mir2_game
```
