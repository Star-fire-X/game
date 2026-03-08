/**
 * @file MAP_SYSTEM_ARCHITECTURE.md
 * @brief 客户端地图系统架构文档（职责分离）
 *
 * ## 概述
 *
 * 经过 P0 和 P1 阶段的重构，MapSystem 已从单一职责的单体类演进为清晰的协调者模式。
 *
 * ## 职责分离设计
 *
 * ### 1. PathFinder（寻路器）
 *
 * **职责**:
 * - 实现 A* 寻路算法
 * - 管理启发式函数
 * - 生成邻居节点
 * - 路径重建
 *
 * **依赖**:
 * - IWalkabilityProvider（用于查询地图的可行走性）
 *
 * **接口**:
 * ```cpp
 * class PathFinder {
 *     std::vector<Position> find_path(const Position& start, const Position& end);
 *     void set_max_path_length(int max_length);
 * };
 * ```
 *
 * **优点**:
 * - 可独立测试寻路算法
 * - 易于替换为其他算法（JPS*, HPA*等）
 * - 无地图加载或渲染依赖
 *
 * ### 2. PortalManager（传送门管理器）
 *
 * **职责**:
 * - 管理当前地图的传送门列表
 * - 注册/注销传送门
 * - 验证传送门配置
 * - 查询传送门信息
 *
 * **不负责**:
 * - 地图加载（由 MapSystem 协调）
 * - 传送门转换执行（由 MapSystem 协调）
 *
 * **接口**:
 * ```cpp
 * class PortalManager {
 *     bool register_portal(const Portal& portal);
 *     std::optional<Portal> get_portal_at(const Position& pos);
 *     void clear();
 * };
 * ```
 *
 * **优点**:
 * - 传送门管理逻辑清晰
 * - 可独立测试
 * - 易于扩展（如添加门类型、条件等）
 *
 * ### 3. MapSystem（协调者）
 *
 * **职责**:
 * - 整合 PathFinder、PortalManager、MapData
 * - 协调地图加载流程
 * - 处理传送门转换（调用 PathFinder 加载新地图，使用 PortalManager 查询）
 * - 实现 IWalkabilityProvider 接口
 * - 维护向后兼容性
 *
 * **关键改进**:
 * 1. 组件化：将算法和管理逻辑提取为独立类
 * 2. 双重接口：同时支持旧的 bool/PortalTransitionResult 接口和新的 Result<T> 接口
 * 3. 依赖倒置：通过 IWalkabilityProvider 解耦 PathFinder 与具体地图实现
 *
 * ## 架构图
 *
 * ```
 *                         MapRenderer
 *                              |
 *                              v
 *                        MapSystem (协调者)
 *                         /    |    \\
 *                        /     |     \\
 *                  PathFinder  |  PortalManager
 *                       |      |
 *                       v      v
 *            IWalkabilityProvider + MapData
 * ```
 *
 * ## 交互流程
 *
 * ### 寻路流程
 * 1. MapSystem::find_path(start, end) 被调用
 * 2. MapSystem 委托给 PathFinder::find_path()
 * 3. PathFinder 通过 IWalkabilityProvider 查询可行走性
 * 4. 返回路径或空列表
 *
 * ### 传送门转换流程
 * 1. MapSystem::execute_portal_transition(pos) 被调用
 * 2. MapSystem 通过 PortalManager 查询传送门
 * 3. MapSystem 加载目标地图
 * 4. PortalManager 被清空并重新填充
 *
 * ## 错误处理策略
 *
 * 为了向后兼容和循序渐进的现代化：
 *
 * **旧接口**（保留）:
 * - `bool find_path()` 失败时返回空路径
 * - `PortalTransitionResult` 结构体表达错误
 *
 * **新接口**（推荐）:
 * - `Result<std::vector<Position>> find_path_result()` 返回 ErrorCode
 * - `Result<PortalTransition> check_portal_transition_result()` 返回 ErrorCode
 *
 * ## 测试策略
 *
 * ### 单元测试
 * ```cpp
 * // PathFinder 的独立测试
 * TEST(PathFinderTest, SimplePathfinding) { }
 * TEST(PathFinderTest, UnreachableDestination) { }
 * TEST(PathFinderTest, DiagonalMovement) { }
 *
 * // PortalManager 的独立测试
 * TEST(PortalManagerTest, RegisterPortal) { }
 * TEST(PortalManagerTest, InvalidPortal) { }
 *
 * // MapSystem 协调测试
 * TEST(MapSystemTest, LoadMapAndFindPath) { }
 * ```
 *
 * ### 集成测试
 * - 完整的地图加载→寻路→渲染流程
 * - 传送门转换的正确性
 *
 * ## 性能特性
 *
 * ### A* 算法
 * - 时间复杂度：O(n log n)（采用改进的启发式）
 * - 空间复杂度：O(n)
 * - 实际探索节点数（512×512 地图）：200-300 （修复前：2000-3000）
 *
 * ### PortalManager
 * - 查询复杂度：O(n)（传送门数量通常 < 100）
 * - 可优化为 O(1) 的哈希表（如需要）
 *
 * ## 未来扩展点
 *
 * 1. **多路径算法**：
 *    - 创建 PathFinderFactory，支持 Dijkstra、JPS*、HPA* 等
 *    - MapSystem 通过工厂创建 PathFinder
 *
 * 2. **条件传送门**：
 *    - 为 Portal 添加条件检查（如等级限制）
 *    - PortalManager::can_use_portal(pos, actor)
 *
 * 3. **动态地图修改**：
 *    - MapData 的可行走性变化通知机制
 *    - PathFinder 缓存失效处理\n *\n * 4. **线程化寻路**：\n *    - 异步 PathFinder::find_path_async()\n *    - 回调或 Future 返回结果\n *\n */
