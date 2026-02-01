# Gateway Metrics Integration Guide

本文档说明如何在Legend2 Gateway代码中集成Prometheus metrics上报。

## 概述

所有metrics使用 `prometheus-cpp` 库实现，通过HTTP Exporter在指定端口（默认9101）暴露metrics端点。

## Metrics定义

### Counter类型（单调递增）

```cpp
// gateway_forward_total - 总转发量
metrics->IncrementCounter("gateway_forward_total");

// gateway_forward_service_* - 按服务分类的转发量
metrics->IncrementCounter("gateway_forward_service_world");
metrics->IncrementCounter("gateway_forward_service_game");
metrics->IncrementCounter("gateway_forward_service_db");

// gateway_user_register - 用户注册
metrics->IncrementCounter("gateway_user_register");

// gateway_session_unregister - 会话注销
metrics->IncrementCounter("gateway_session_unregister");

// gateway_service_connected_* - 服务连接成功
metrics->IncrementCounter("gateway_service_connected_world");
metrics->IncrementCounter("gateway_service_connected_game");
metrics->IncrementCounter("gateway_service_connected_db");

// gateway_service_disconnected_* - 服务断连
metrics->IncrementCounter("gateway_service_disconnected_world");
metrics->IncrementCounter("gateway_service_disconnected_game");
metrics->IncrementCounter("gateway_service_disconnected_db");
```

### Gauge类型（可增可减）

```cpp
// gateway_route_table_connection_count - 连接路由表大小
metrics->SetGauge("gateway_route_table_connection_count", connection_map.size());

// gateway_route_table_user_count - 用户路由表大小
metrics->SetGauge("gateway_route_table_user_count", user_map.size());
```

## 集成示例

### 1. 初始化Metrics系统

```cpp
// 在Gateway启动时初始化
#include "monitor/metrics.h"

void Gateway::Initialize() {
    // 在9101端口暴露metrics
    mir2::monitor::Metrics::GetInstance().Init(9101);

    // ... 其他初始化代码
}
```

### 2. 消息转发处理

```cpp
void Gateway::ForwardMessage(const Message& msg, const std::string& target_service) {
    auto* metrics = &mir2::monitor::Metrics::GetInstance();

    // 记录总转发量
    metrics->IncrementCounter("gateway_forward_total");

    // 按目标服务分类记录
    if (target_service == "world") {
        metrics->IncrementCounter("gateway_forward_service_world");
    } else if (target_service == "game") {
        metrics->IncrementCounter("gateway_forward_service_game");
    } else if (target_service == "db") {
        metrics->IncrementCounter("gateway_forward_service_db");
    }

    // 实际转发逻辑
    DoForward(msg, target_service);
}
```

### 3. 用户注册处理

```cpp
void Gateway::OnUserRegister(uint64_t user_id, uint64_t connection_id) {
    auto* metrics = &mir2::monitor::Metrics::GetInstance();

    // 记录用户注册
    metrics->IncrementCounter("gateway_user_register");

    // 更新路由表
    user_route_table_[user_id] = connection_id;

    // 更新路由表大小metrics
    metrics->SetGauge("gateway_route_table_user_count", user_route_table_.size());
}
```

### 4. 会话注销处理

```cpp
void Gateway::OnSessionUnregister(uint64_t connection_id) {
    auto* metrics = &mir2::monitor::Metrics::GetInstance();

    // 记录会话注销
    metrics->IncrementCounter("gateway_session_unregister");

    // 清理路由表
    connection_route_table_.erase(connection_id);

    // 更新路由表大小
    metrics->SetGauge("gateway_route_table_connection_count",
                      connection_route_table_.size());
}
```

### 5. 服务连接管理

```cpp
void Gateway::OnServiceConnected(const std::string& service_type) {
    auto* metrics = &mir2::monitor::Metrics::GetInstance();

    std::string counter_name = "gateway_service_connected_" + service_type;
    metrics->IncrementCounter(counter_name);

    LOG_INFO("Service {} connected", service_type);
}

void Gateway::OnServiceDisconnected(const std::string& service_type) {
    auto* metrics = &mir2::monitor::Metrics::GetInstance();

    std::string counter_name = "gateway_service_disconnected_" + service_type;
    metrics->IncrementCounter(counter_name);

    LOG_WARN("Service {} disconnected", service_type);
}
```

### 6. 定期更新Gauge指标

```cpp
// 建议使用定时器定期更新Gauge类型指标
void Gateway::UpdateMetrics() {
    auto* metrics = &mir2::monitor::Metrics::GetInstance();

    // 更新连接路由表大小
    metrics->SetGauge("gateway_route_table_connection_count",
                      connection_route_table_.size());

    // 更新用户路由表大小
    metrics->SetGauge("gateway_route_table_user_count",
                      user_route_table_.size());
}

// 在事件循环中每秒调用一次
void Gateway::Run() {
    asio::steady_timer timer(io_context_, std::chrono::seconds(1));

    auto update_handler = [this, &timer](const asio::error_code& ec) {
        if (!ec) {
            UpdateMetrics();
            timer.expires_after(std::chrono::seconds(1));
            timer.async_wait(update_handler);
        }
    };

    timer.async_wait(update_handler);
    io_context_.run();
}
```

## 配置文件示例

```yaml
# config/gateway.yaml
gateway:
  listen_port: 7000

  # Prometheus Metrics配置
  metrics:
    enabled: true
    port: 9101
    path: /metrics
```

## 验证Metrics导出

### 方法1：使用curl
```bash
curl http://localhost:9101/metrics
```

预期输出：
```
# HELP gateway_forward_total Total messages forwarded
# TYPE gateway_forward_total counter
gateway_forward_total 12345

# HELP gateway_route_table_connection_count Connection route table size
# TYPE gateway_route_table_connection_count gauge
gateway_route_table_connection_count 42
```

### 方法2：使用Prometheus查询
访问 http://localhost:9090/graph，执行查询：
```promql
rate(gateway_forward_total[1m])
```

### 方法3：检查Prometheus Targets
访问 http://localhost:9090/targets，确认Gateway目标状态为"UP"。

## 注意事项

1. **线程安全**：prometheus-cpp库是线程安全的，可以在多线程环境中直接调用。

2. **性能考虑**：
   - Counter增量操作非常快（原子操作），对性能影响极小
   - Gauge设置操作也很快，但建议避免高频调用（如每次消息都更新）
   - 建议使用定时器定期更新Gauge类型指标（如每秒一次）

3. **命名规范**：
   - 使用小写字母和下划线
   - 前缀使用 `gateway_` 标识来源
   - Counter类型建议使用 `_total` 后缀
   - 保持与Grafana Dashboard中的名称一致

4. **错误处理**：
   - Metrics上报失败不应影响业务逻辑
   - 建议将metrics调用包装在try-catch中（如果需要）

5. **资源清理**：
   - Metrics系统在进程退出时自动清理
   - HTTP Exporter会自动停止

## 扩展Metrics

如需添加新的metrics，需要：

1. 在代码中添加相应的计数/设置调用
2. 更新 `monitoring/grafana/dashboards/gateway-dashboard.json`
3. （可选）在 `monitoring/prometheus/rules/gateway_alerts.yml` 中添加告警规则
4. 更新本文档和README

## 示例：添加新Metric

假设要添加"消息队列深度"指标：

```cpp
// 在合适位置添加
metrics->SetGauge("gateway_message_queue_depth", message_queue_.size());
```

在Grafana中添加Panel：
```json
{
  "targets": [
    {
      "expr": "gateway_message_queue_depth",
      "legendFormat": "Queue Depth"
    }
  ],
  "title": "Message Queue Depth"
}
```

## 参考资料

- [Prometheus Best Practices](https://prometheus.io/docs/practices/naming/)
- [prometheus-cpp Documentation](https://github.com/jupp0r/prometheus-cpp)
- [Metric Types](https://prometheus.io/docs/concepts/metric_types/)
