# 回滚指南（LogicServer -> World/Game/DB）

> **⚠️ 历史文档警告**:
> 本文档描述的是从 LogicServer 回滚到旧四进程架构（World/Game/DB）的方案。
> **当前代码库已移除 World/Game/DB 服务器源码，此回滚方案不再适用。**
> 保留此文档仅供历史参考。

## 适用范围

- ~~当前部署为二进程架构：Gateway + LogicServer~~
- ~~目标回滚为旧版四进程架构：Gateway + World/Game/DB~~
- 回滚脚本仅支持 systemd 服务管理

## 前置条件

1. 旧版二进程/四进程二进制与配置已准备完毕（尤其是 Gateway 的旧配置）。
2. 对应 systemd unit 已存在且名称确认无误。
3. 数据库 Schema 符合“向后兼容”原则（仅新增字段/表，详见下文）。
4. 已通知维护窗口并完成必要的业务冻结（如需）。

## 快速开始

```bash
# 推荐：在有 sudo 权限的环境执行
./scripts/rollback.sh --sudo
```

## 回滚步骤

1. 通知维护/冻结写入（如需）。
2. 确认旧版 Gateway 配置已恢复为 World/Game/DB 路由模式。
3. 执行回滚脚本：
   ```bash
   ./scripts/rollback.sh --sudo
   ```
4. 如 Gateway 需要重新加载旧配置，重启 Gateway（建议手动执行以避免误配置）。
5. 按“回滚验证”完成验证。

## 脚本参数与环境变量

| 作用 | 默认值 | 参数 | 环境变量 |
|------|--------|------|----------|
| LogicServer 单元 | `mir2-logic.service` | `--logic-unit` | `LOGIC_UNIT` |
| WorldServer 单元 | `mir2-world.service` | `--world-unit` | `WORLD_UNIT` |
| GameServer 单元 | `mir2-game.service` | `--game-unit` | `GAME_UNIT` |
| DbServer 单元 | `mir2-db.service` | `--db-unit` | `DB_UNIT` |
| 单元等待超时 | `60` 秒 | `--timeout` | `TIMEOUT_SEC` |
| 轮询间隔 | `2` 秒 | `--interval` | `INTERVAL_SEC` |
| 只打印不执行 | - | `--dry-run` | - |
| 使用 sudo | - | `--sudo` | - |

示例：

```bash
./scripts/rollback.sh --sudo \
  --logic-unit mir2-logic \
  --world-unit mir2-world \
  --game-unit mir2-game \
  --db-unit mir2-db
```

## 数据兼容性说明（Epic 5）

> 依据架构文档 Section 5: 兼容性与回滚

回滚基于“数据库 Schema 向后兼容”原则：

- 迁移脚本只允许 `ADD COLUMN`（nullable）或 `CREATE TABLE`。
- 禁止 `DROP COLUMN`、`ALTER COLUMN TYPE` 或重命名。
- 旧版 DbServer 应能在新 Schema 上正常启动并读写旧字段。
- 迁移脚本需包含回滚说明（即使回滚是 no-op）。

**注意**：
- 如果出现任何破坏性迁移（删除/修改字段类型），需先恢复数据库备份再回滚旧版本。
- 新版本写入的新字段会保留，但旧版本可能不识别这些字段。

## 回滚验证

建议按以下清单验证：

1. **服务状态**
   ```bash
   systemctl is-active mir2-world.service
   systemctl is-active mir2-game.service
   systemctl is-active mir2-db.service
   systemctl is-active mir2-logic.service  # 应为 inactive/failed
   ```
2. **网关连通性**
   - Gateway 日志中确认已连接 World/Game/DB。
3. **功能冒烟**
   - 登录/选角/进图/移动/战斗/掉线重连。
4. **数据读写**
   - 创建角色、装备变更、位置保存、退出重登一致。
5. **监控指标**
   - Gateway/World/Game/DB 指标恢复正常。

## 测试回滚流程

1. **Dry-run 验证脚本动作**
   ```bash
   ./scripts/rollback.sh --dry-run
   ```
2. **Staging 预演**
   - 在预发布环境执行完整回滚。
   - 记录脚本日志（`logs/rollback-*.log`）与 systemd 状态。
3. **复盘确认**
   - 对照“回滚验证”清单逐项确认。

## 故障排除

- **Unit 不存在**：
  - `systemctl list-unit-files | grep mir2-` 检查 unit 名称。
- **启动超时**：
  - `journalctl -u <unit>` 查看错误原因。
- **DB 连接失败**：
  - 检查 `config/db.yaml` 与数据库可达性。
- **Gateway 路由异常**：
  - 确认 `gateway.yaml` 已恢复旧版路由并重启 Gateway。
