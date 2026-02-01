# Legend2 Gateway Monitoring

完整的Prometheus + Grafana监控解决方案，用于监控Legend2网关系统的运行状态。

## 目录结构

```
monitoring/
├── docker-compose.yml              # Docker Compose编排文件
├── README.md                       # 本文档
├── grafana/
│   ├── datasources/
│   │   └── prometheus.yaml        # Prometheus数据源配置
│   ├── dashboards/
│   │   └── gateway-dashboard.json # 网关监控仪表盘
│   └── dashboard-config.yaml      # 仪表盘自动加载配置
├── prometheus/
│   ├── prometheus.yml             # Prometheus主配置
│   └── rules/
│       └── gateway_alerts.yml     # 告警规则定义
└── alertmanager/
    └── alertmanager.yml           # Alertmanager配置
```

## 监控指标说明

### Gateway Metrics（网关指标）

| 指标名称 | 类型 | 说明 |
|---------|------|------|
| `gateway_forward_total` | Counter | 总消息转发量 |
| `gateway_forward_service_world` | Counter | 转发到World服务的消息数 |
| `gateway_forward_service_game` | Counter | 转发到Game服务的消息数 |
| `gateway_forward_service_db` | Counter | 转发到DB服务的消息数 |
| `gateway_user_register` | Counter | 用户注册数 |
| `gateway_session_unregister` | Counter | 会话注销数 |
| `gateway_service_connected_world` | Counter | World服务连接成功数 |
| `gateway_service_connected_game` | Counter | Game服务连接成功数 |
| `gateway_service_connected_db` | Counter | DB服务连接成功数 |
| `gateway_service_disconnected_world` | Counter | World服务断连数 |
| `gateway_service_disconnected_game` | Counter | Game服务断连数 |
| `gateway_service_disconnected_db` | Counter | DB服务断连数 |
| `gateway_route_table_connection_count` | Gauge | 连接路由表大小 |
| `gateway_route_table_user_count` | Gauge | 用户路由表大小 |

## 快速开始

### 前置条件

- Docker 20.10+
- Docker Compose 2.0+
- 网关服务已启用Prometheus metrics导出（默认端口9101）

### 1. 启动监控栈

```bash
cd monitoring
docker-compose up -d
```

### 2. 验证服务状态

```bash
# 检查所有服务是否正常运行
docker-compose ps

# 查看Prometheus日志
docker-compose logs prometheus

# 查看Grafana日志
docker-compose logs grafana
```

### 3. 访问监控界面

#### Grafana（可视化仪表盘）
- URL: http://localhost:3000
- 默认用户名: `admin`
- 默认密码: `admin`
- 首次登录后会要求修改密码

#### Prometheus（时序数据库）
- URL: http://localhost:9090
- 可直接查询metrics和验证数据采集

#### Alertmanager（告警管理）
- URL: http://localhost:9093
- 查看和管理告警状态

## 仪表盘导入

### 方法1：自动加载（推荐）

仪表盘配置文件已通过Volume挂载，启动后自动加载：
- Dashboard会出现在 **"Legend2"** 文件夹下
- 名称：**Legend2 Gateway Dashboard**

### 方法2：手动导入

1. 登录Grafana
2. 点击左侧菜单 **"+"** → **"Import"**
3. 上传文件：`monitoring/grafana/dashboards/gateway-dashboard.json`
4. 选择数据源：**Prometheus**
5. 点击 **"Import"**

## 仪表盘说明

### Overview（概览区域）
- **Current Connections**: 当前连接数
- **Current Users**: 当前用户数
- **Message Forward Rate**: 5分钟平均转发速率
- **Session Unregister Rate**: 1分钟会话注销速率

### Traffic（流量监控区域）
- **Message Forward Rate**: 实时消息转发量趋势
- **Forward Rate by Service**: 按服务分类的转发量（World/Game/DB）
- **User Register/Unregister Trends**: 用户注册/注销趋势对比

### Services（服务连接状态区域）
- **World/Game/DB Service Connection Rate**: 各服务连接成功率（Gauge图表）
- **Service Disconnection Trends**: 服务断连趋势（堆叠面积图）

### Routes（路由表监控区域）
- **Connection Route Table**: 连接路由表大小变化
- **User Route Table**: 用户路由表大小变化

### 仪表盘特性
- **自动刷新**: 每5秒自动刷新数据
- **时间范围**: 默认显示最近1小时，可通过变量调整
- **支持变量**: 可切换时间范围（5m/15m/30m/1h/3h/6h/12h/24h）

## 告警规则

### 已配置的告警规则

| 告警名称 | 触发条件 | 严重级别 | 持续时间 |
|---------|---------|---------|---------|
| HighServiceDisconnectionRate | 服务断连速率 > 0.05/s | Warning | 2分钟 |
| WorldServiceConnectionFailure | World服务连接成功率 < 95% | Critical | 3分钟 |
| GameServiceConnectionFailure | Game服务连接成功率 < 95% | Critical | 3分钟 |
| DBServiceConnectionFailure | DB服务连接成功率 < 95% | Critical | 3分钟 |
| HighSessionUnregisterRate | 会话注销速率 > 10/s | Warning | 2分钟 |
| LargeRouteTableSize | 路由表条目 > 5000 | Warning | 5分钟 |
| NoForwardTraffic | 5分钟内无消息转发 | Warning | 5分钟 |
| LowUserRegistrationRate | 用户注册速率 < 0.01/s | Info | 10分钟 |

### 告警查看

在Grafana中查看告警：
1. 左侧菜单 → **Alerting** → **Alert rules**
2. 查看触发的告警历史
3. 配置通知渠道（邮件/Webhook/Slack等）

在Alertmanager中管理告警：
- 访问 http://localhost:9093
- 查看活跃告警、静默规则等

## 配置定制

### 修改数据采集间隔

编辑 `prometheus/prometheus.yml`:
```yaml
global:
  scrape_interval: 5s  # 改为10s、15s等
```

### 添加新的监控目标

在 `prometheus/prometheus.yml` 的 `scrape_configs` 中添加：
```yaml
scrape_configs:
  - job_name: 'my-service'
    static_configs:
      - targets:
          - 'my-service:9100'
```

### 修改Grafana管理员密码

编辑 `docker-compose.yml`:
```yaml
environment:
  - GF_SECURITY_ADMIN_PASSWORD=your-secure-password
```

### 配置告警通知

编辑 `alertmanager/alertmanager.yml`，配置邮件、Slack、Webhook等接收器：

```yaml
receivers:
  - name: 'email-receiver'
    email_configs:
      - to: 'your-email@example.com'
        from: 'alertmanager@legend2.local'
        smarthost: 'smtp.gmail.com:587'
        auth_username: 'your-email@gmail.com'
        auth_password: 'your-app-password'
```

### 数据保留时间

默认保留30天，修改 `docker-compose.yml` 中的Prometheus启动参数：
```yaml
command:
  - '--storage.tsdb.retention.time=30d'  # 改为60d、90d等
```

## 与网关服务集成

### 1. 确保网关启用Prometheus Exporter

在网关配置文件中启用metrics导出：
```yaml
# config/gateway.yaml
monitor:
  enabled: true
  port: 9101
  path: /metrics
```

### 2. 修改Prometheus目标地址

如果网关不在Docker网络中，需修改 `prometheus/prometheus.yml`:
```yaml
scrape_configs:
  - job_name: 'gateway'
    static_configs:
      - targets:
          - 'host.docker.internal:9101'  # 使用宿主机地址
```

### 3. 在代码中上报Metrics

参考示例（C++伪代码）：
```cpp
// 注册用户时
metrics->IncrementCounter("gateway_user_register");

// 转发消息时
metrics->IncrementCounter("gateway_forward_total");
metrics->IncrementCounter("gateway_forward_service_" + service_name);

// 更新路由表大小
metrics->SetGauge("gateway_route_table_connection_count", connection_map.size());
metrics->SetGauge("gateway_route_table_user_count", user_map.size());
```

## 常用操作

### 重启服务
```bash
docker-compose restart
```

### 查看日志
```bash
# 所有服务日志
docker-compose logs -f

# 特定服务日志
docker-compose logs -f grafana
docker-compose logs -f prometheus
```

### 停止服务
```bash
docker-compose down
```

### 停止服务并删除数据
```bash
docker-compose down -v
```

### 更新配置后重载
```bash
# 重载Prometheus配置（不需要重启）
curl -X POST http://localhost:9090/-/reload

# 重启Grafana（应用新配置）
docker-compose restart grafana
```

### 备份Grafana仪表盘
```bash
# 导出仪表盘JSON
curl -H "Authorization: Bearer <api-key>" \
  http://localhost:3000/api/dashboards/uid/legend2-gateway > backup.json
```

## 性能优化建议

### 1. 降低采集频率（高负载场景）
```yaml
# prometheus.yml
global:
  scrape_interval: 15s  # 从5s提高到15s
```

### 2. 调整Grafana查询间隔
在Dashboard设置中将刷新间隔从5秒改为10秒或更长。

### 3. 限制Prometheus内存使用
```yaml
# docker-compose.yml
services:
  prometheus:
    deploy:
      resources:
        limits:
          memory: 2G
```

### 4. 使用远程存储（生产环境推荐）
配置Prometheus远程写入到InfluxDB、Thanos等长期存储。

## 故障排查

### Grafana无法连接Prometheus

1. 检查Prometheus是否运行：
   ```bash
   curl http://localhost:9090/-/healthy
   ```

2. 检查Docker网络连通性：
   ```bash
   docker-compose exec grafana ping prometheus
   ```

3. 验证数据源配置：
   - Grafana → Configuration → Data sources → Prometheus
   - 点击 "Test" 按钮

### 仪表盘显示"No Data"

1. 检查Prometheus是否正在采集数据：
   - 访问 http://localhost:9090/targets
   - 确认Gateway的target状态为"UP"

2. 手动查询metrics：
   - 在Prometheus界面执行查询：`gateway_forward_total`
   - 确认是否返回数据

3. 检查时间范围是否合理（网关是否在该时间段运行）

### 告警未触发

1. 检查告警规则状态：
   - 访问 http://localhost:9090/alerts
   - 查看规则评估状态

2. 验证Alertmanager连接：
   ```bash
   curl http://localhost:9093/-/healthy
   ```

3. 查看Alertmanager日志：
   ```bash
   docker-compose logs alertmanager
   ```

## 高级特性

### 自定义仪表盘变量

在Grafana中添加新变量：
1. Dashboard Settings → Variables → Add variable
2. 例如添加 `$service` 变量用于筛选服务

### 设置告警通知渠道

1. Grafana → Alerting → Contact points
2. 添加邮件、Slack、Webhook等通知方式
3. 配置告警路由规则

### 集成到现有监控系统

- **与Kubernetes集成**: 使用ServiceMonitor CRD
- **与Consul集成**: 使用consul_sd_configs动态发现
- **联邦查询**: 配置多个Prometheus实例聚合

## 参考资料

- [Prometheus官方文档](https://prometheus.io/docs/)
- [Grafana官方文档](https://grafana.com/docs/)
- [prometheus-cpp客户端](https://github.com/jupp0r/prometheus-cpp)
- [PromQL查询语言](https://prometheus.io/docs/prometheus/latest/querying/basics/)

## 版本信息

- Prometheus: v2.47.0
- Grafana: v10.1.0
- Alertmanager: v0.26.0
- Dashboard Version: 1.0

## 许可证

本监控配置遵循Legend2项目的许可证。

## 联系方式

如有问题或建议，请提交Issue到项目仓库。
