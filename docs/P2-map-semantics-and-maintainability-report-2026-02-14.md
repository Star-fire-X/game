# P2 Map 模块语义与可维护性更新（2026-02-14）

## 1) AOI Leave 坐标语义

- 已在 `AOIEventType::kLeave` 注释中明确：
  - 回调中的 `(x, y)` 表示“离开方（target）最新坐标”。
  - 若上层需要旧坐标，应扩展事件载荷显式携带。

## 2) AOI 重复逻辑提取

- 在 `aoi_manager.cc` 提取了通用辅助函数：
  - `CollectSurroundingEntities(...)`
  - `EmitGridDeltaEvents(...)`
- 复用到 `Enter/Leave/Move`，减少重复的九宫格遍历与差集网格事件发射逻辑。

## 3) 路径基准缓存与可配置基准路径

- Map 基准路径：
  - 新增环境变量 `LEGEND2_MAP_BASE_PATH`（推荐绝对路径）。
  - 未设置时仍回退到默认 `cwd/Map`。
- Gate 配置基准路径：
  - 新增环境变量 `LEGEND2_CONFIG_BASE_PATH`（推荐绝对路径）。
  - 未设置时仍回退到默认 `cwd/config`。

## 4) 地图瓦片加载顺序说明

- 已在 `map_loader.cc` 明确注释：
  - 文件流读取顺序采用列优先（`x` 外层、`y` 内层）。
  - 内部存储统一为行优先索引（`index = y * width + x`）。

## 5) 日志统一

- `GateManager::LoadFromConfig` 异常路径由 `std::cerr` 改为 `SYSLOG_ERROR`。
