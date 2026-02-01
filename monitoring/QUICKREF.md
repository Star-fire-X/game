# Legend2 Gateway Monitoring - Quick Reference

## 快速启动
```bash
cd monitoring
./start.sh              # 启动所有服务
./validate.sh           # 验证服务状态
docker-compose logs -f  # 查看日志
docker-compose down     # 停止服务
```

## 访问地址
| 服务 | URL | 默认凭证 |
|------|-----|---------|
| Grafana | http://localhost:3000 | admin/admin |
| Prometheus | http://localhost:9090 | - |
| Alertmanager | http://localhost:9093 | - |
| Gateway Metrics | http://localhost:9101/metrics | - |

## 核心Metrics
| 指标 | 类型 | 说明 |
|------|------|------|
| gateway_forward_total | Counter | 总转发量 |
| gateway_route_table_connection_count | Gauge | 连接路由表大小 |
| gateway_route_table_user_count | Gauge | 用户路由表大小 |
| gateway_user_register | Counter | 用户注册数 |
| gateway_session_unregister | Counter | 会话注销数 |
| gateway_service_connected_* | Counter | 服务连接成功数 |
| gateway_service_disconnected_* | Counter | 服务断连数 |

## 常用PromQL查询
```promql
# 消息转发速率（每秒）
rate(gateway_forward_total[1m])

# 服务连接成功率
gateway_service_connected_world / (gateway_service_connected_world + gateway_service_disconnected_world)

# 用户活跃趋势
rate(gateway_user_register[5m]) - rate(gateway_session_unregister[5m])

# 路由表增长速率
deriv(gateway_route_table_user_count[5m])
```

## Dashboard面板
1. **Overview**: 当前连接/用户、转发速率、注销速率
2. **Traffic**: 消息转发趋势、按服务分类、用户注册/注销
3. **Services**: 服务连接成功率、断连趋势
4. **Routes**: 路由表大小变化

## 告警规则
| 告警 | 条件 | 级别 |
|------|------|------|
| HighServiceDisconnectionRate | 断连率 > 0.05/s | Warning |
| WorldServiceConnectionFailure | 成功率 < 95% | Critical |
| HighSessionUnregisterRate | 注销率 > 10/s | Warning |
| LargeRouteTableSize | 路由表 > 5000 | Warning |
| NoForwardTraffic | 5分钟无转发 | Warning |

## 故障排查
```bash
# 检查服务健康状态
curl http://localhost:9090/-/healthy
curl http://localhost:3000/api/health

# 检查Prometheus采集状态
curl http://localhost:9090/api/v1/targets

# 手动查询metrics
curl http://localhost:9101/metrics | grep gateway_

# 查看容器日志
docker-compose logs prometheus
docker-compose logs grafana

# 重载Prometheus配置
curl -X POST http://localhost:9090/-/reload
```

## 配置文件位置
```
monitoring/
├── docker-compose.yml                          # Docker编排
├── grafana/
│   ├── datasources/prometheus.yaml            # 数据源配置
│   ├── dashboards/gateway-dashboard.json      # 仪表盘定义
│   └── dashboard-config.yaml                  # 自动加载配置
├── prometheus/
│   ├── prometheus.yml                         # 主配置
│   └── rules/gateway_alerts.yml               # 告警规则
└── alertmanager/
    └── alertmanager.yml                       # 告警路由
```

## 性能优化
- 降低采集频率：修改 `prometheus.yml` 中的 `scrape_interval`
- 调整刷新间隔：Dashboard设置中修改 `refresh` 参数
- 限制内存使用：在 `docker-compose.yml` 中添加资源限制

## 扩展功能
- 添加邮件/Slack通知：配置 `alertmanager.yml`
- 自定义Dashboard：Grafana UI中编辑或导入JSON
- 添加新Metrics：在代码中调用 `metrics->IncrementCounter()`
- 长期存储：配置Prometheus远程写入

## 技术栈版本
- Prometheus: v2.47.0
- Grafana: v10.1.0
- Alertmanager: v0.26.0
- prometheus-cpp: 客户端库

## 文档链接
- 详细部署：[README.md](README.md)
- 代码集成：[INTEGRATION.md](INTEGRATION.md)
- Prometheus: https://prometheus.io/docs/
- Grafana: https://grafana.com/docs/

---
最后更新：2024
