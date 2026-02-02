# C++ Health API 接口规范

## 概述

本文档定义了 Legend2 游戏服务 C++ 端需要实现的 HTTP API 接口规范。
使用 Boost.Beast 库实现，复用现有服务端口。

## 基础信息

| 项目 | 说明 |
|------|------|
| 协议 | HTTP/1.1 |
| 格式 | JSON |
| 路径前缀 | `/api/` |
| 认证 | 内网调用，暂不需要 |

## 服务端口

| 服务 | 端口 | 说明 |
|------|------|------|
| Gateway | 7000 | 网关服务 |
| World | 7100 | 世界服务 |
| Game | 7200 | 游戏服务 |
| DB | 7300 | 数据库服务 |

---

## API 端点

### 1. 健康检查

**GET /api/health**

返回服务健康状态。

**Response 200:**
```json
{
  "status": "ok",
  "service": "gateway",
  "uptime": 3600,
  "version": "1.0.0"
}
```

**字段说明:**
| 字段 | 类型 | 说明 |
|------|------|------|
| status | string | "ok" 或 "error" |
| service | string | 服务名称 |
| uptime | int | 运行秒数 |
| version | string | 版本号 |

---

### 2. 性能指标

**GET /api/metrics**

返回服务性能指标。

**Response 200:**
```json
{
  "cpu_percent": 25.5,
  "memory_mb": 512,
  "connections": 100,
  "requests_per_sec": 50
}
```

**字段说明:**
| 字段 | 类型 | 说明 |
|------|------|------|
| cpu_percent | float | CPU 使用率 |
| memory_mb | int | 内存使用 MB |
| connections | int | 当前连接数 |
| requests_per_sec | int | 每秒请求数 |

---

### 3. 配置热重载

**POST /api/config/reload**

触发配置文件热重载。

**Response 200:**
```json
{
  "success": true,
  "message": "config reloaded"
}
```

**Response 500:**
```json
{
  "success": false,
  "message": "failed to reload: invalid yaml"
}
```

---

## Boost.Beast 实现示例

```cpp
// http_api_server.hpp
#include <boost/beast.hpp>
#include <boost/asio.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class HttpApiServer {
public:
    using Handler = std::function<
        http::response<http::string_body>(
            const http::request<http::string_body>&)>;

    void register_handler(
        http::verb method,
        const std::string& path,
        Handler handler);

    void start(uint16_t port);
    void stop();
};
```

---

## 错误响应格式

所有错误统一返回:
```json
{
  "error": "error message",
  "code": 500
}
```

---

*文档版本*: 1.0
*日期*: 2026-02-01