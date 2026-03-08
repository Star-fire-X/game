# StorageEngine Phase4 审计字段字典

## 1. 审计结构

`StorageEngine::AuditEntry` 字段：

1. `timestamp_ms`：毫秒时间戳（UTC epoch ms）。
2. `principal`：调用主体，空值记录为 `anonymous`。
3. `operation`：操作类型（如 `get/set/delete/batch_write/runtime_config_update`）。
4. `key`：目标 key；敏感 key 会被脱敏为 `"<redacted>"`。
5. `success`：是否成功。
6. `reason`：失败原因或状态摘要（如 `invalid_token`、`ok`）。

## 2. 脱敏规则

当前默认规则：

1. `account:username:*` 视为敏感 key。
2. 审计写入与日志输出统一使用脱敏值 `"<redacted>"`。

## 3. 强审计规则

即使 `enable_audit_log=false`，以下场景仍会强制落审计：

1. 敏感 key 访问（匹配敏感 key 规则）。
2. 鉴权拒绝类原因：
   - `invalid_token`
   - `access_denied`
   - `token_not_configured`
3. 运行时安全配置变更：
   - `operation=runtime_config_update`
   - 包括 `enable_access_control`、`require_auth_for_reads`、`access_control_token`
   - 以及加密相关运行时项（如 `encryption_active_key_id`）的变更/失败

## 4. Runtime Config 审计键与原因

常见 `key`：

1. `config.enable_access_control`
2. `config.require_auth_for_reads`
3. `config.access_control_token`
4. `config.enable_data_encryption`
5. `config.encryption_active_key_id`
6. `config.encryption_key_env`

常见 `reason`：

1. `updated`：运行时配置变更已生效。
2. `l2_codec_apply_failed`：L2 编解码运行时应用失败（已回滚旧配置）。

## 5. 推荐告警

1. `reason in {invalid_token, access_denied}` 在 5 分钟窗口内突增。
2. `operation=delete` 且失败率连续超阈值。
3. 强审计触发次数突增（用于发现异常访问/攻击）。

## 6. 取证建议

1. 优先按 `operation + reason + principal + timestamp_ms` 聚合定位。
2. 对敏感 key 仅保留最小必要信息，不回填明文 key。
3. 与应用访问日志、网关来源 IP、请求链路 ID 交叉核验。

## 7. 运行时指标（新增）

可观测计数（Prometheus counter 名）：

1. `storage.audit.runtime_config.total`
2. `storage.audit.runtime_config.failure_total`
3. `storage.audit.runtime_config.reason.updated_total`
4. `storage.audit.runtime_config.reason.l2_codec_apply_failed_total`
5. `storage.audit.runtime_config.key.enable_access_control_total`
6. `storage.audit.runtime_config.key.require_auth_for_reads_total`
7. `storage.audit.runtime_config.key.access_control_token_total`
8. `storage.audit.runtime_config.key.encryption_active_key_id_total`
9. `storage.audit.runtime_config.key.enable_data_encryption_total`
10. `storage.audit.runtime_config.key.encryption_key_env_total`
