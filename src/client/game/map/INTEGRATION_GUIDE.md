/**
 * @file INTEGRATION_GUIDE.md
 * @brief MapSystem 职责分离集成指南
 *
 * 本指南说明如何逐步将 PathFinder 和 PortalManager 集成到现有 MapSystem 中。
 *
 * ## 集成阶段
 *
 * ### 第一阶段：添加成员组件（非破坏性）
 *
 * 在 MapSystem 的 private 部分添加：
 *
 * ```cpp
 * class MapSystem : public IWalkabilityProvider {
 * private:
 *     // 新增成员
 *     PathFinder path_finder_;           // 寻路器
 *     PortalManager portal_manager_;     // 传送门管理器
 *
 *     // 现有成员保留...
 * };
 * ```
 *
 * ### 第二阶段：双重实现（逐步迁移）
 *
 * 保留现有的 `find_path()` 等方法，但在实现中：
 *
 * ```cpp
 * std::vector<Position> MapSystem::find_path(const Position& start,
 *                                            const Position& end) const {
 *     // 委托给 PathFinder
 *     return path_finder_.find_path(start, end);
 * }
 * ```
n *\n * ### 第三阶段：新接口实现（Result 版本）\n *\n * 实现现有的 `find_path_result()` 等方法：\n *\n * ```cpp\n * Result<std::vector<Position>> MapSystem::find_path_result(\n *     const Position& start, const Position& end) const {\n *     if (!is_valid_position(start) || !is_valid_position(end)) {\n *         return ErrorCode::INVALID_POSITION;\n *     }\n *\n *     auto path = path_finder_.find_path(start, end);\n *     if (path.empty()) {\n *         return ErrorCode::PATH_NOT_FOUND;\n *     }\n *\n *     return path;\n * }\n * ```\n *\n * ### 第四阶段：传送门管理迁移\n *\n * ```cpp\n * void MapSystem::register_portal(const Portal& portal) {\n *     // 委托给 PortalManager\n *     if (!portal_manager_.register_portal(portal)) {\n *         // 处理无效传送门\n *     }\n * }\n *\n * std::optional<Portal> MapSystem::get_portal_at(const Position& pos) const {\n *     return portal_manager_.get_portal_at(pos);\n * }\n * ```\n *\n * ### 第五阶段：清理旧代码\n *\n * 一旦 PathFinder 和 PortalManager 经过充分测试，删除 MapSystem 中对应的私有方法：\n * - `find_path()` 的旧实现\n * - `get_neighbors()`（因为 PathFinder 有自己的）\n * - `manhattan_distance()`\n * - `reconstruct_path()`（旧版本）\n * - `register_portal()` 的旧实现\n *\n * ## 集成检查清单\n *\n * ### 编译检查\n * - [ ] PathFinder 和 PortalManager 头文件包含正确\n * - [ ] MapSystem 包含新的头文件\n * - [ ] 所有成员变量初始化（构造函数）\n * - [ ] 代码编译通过，无链接错误\n *\n * ### 功能测试\n * - [ ] 寻路功能保持原有行为\n * - [ ] 传送门管理保持原有行为\n * - [ ] IWalkabilityProvider 接口实现完整\n * - [ ] Result<T> 新接口返回正确的 ErrorCode\n *\n * ### 性能验证\n * - [ ] 寻路性能无下降（实际应该改进）\n * - [ ] 内存占用无显著增加\n * - [ ] 渲染帧率保持稳定\n *\n * ### 向后兼容性\n * - [ ] 旧的 `find_path()` 接口仍然有效\n * - [ ] `PortalTransitionResult` 仍然有效\n * - [ ] 现有代码无需修改即可编译\n *\n * ## CMakeLists.txt 更新\n *\n * 在 `src/client/CMakeLists.txt` 中的客户端源文件列表中添加：\n *\n * ```cmake\n * set(GAME_SOURCES\n *     # ... 现有文件 ...\n *     game/map/path_finder.cpp\n *     game/map/portal_manager.cpp\n *     # ... 现有文件 ...\n * )\n * ```\n *\n * ## 潜在问题和解决方案\n *\n * ### 问题 1：PathFinder 需要访问 MapData\n *\n * **解决**：通过 IWalkabilityProvider 接口访问\n * - IWalkabilityProvider::is_walkable(x, y) 返回单个瓦片的状态\n * - MapSystem 实现该接口，PathFinder 只依赖接口\n *\n * ### 问题 2：性能下降（如果有）\n *\n * **原因**：额外的虚函数调用\n * **解决**：\n * - 考虑添加 `get_neighbors_unchecked()` 快速路径\n * - 使用内联优化\n * - 如果性能关键，考虑模板化 PathFinder<WalkabilityProvider>\n *\n * ### 问题 3：内存占用增加\n *\n * **原因**：新成员对象的开销\n * **解决**：\n * - PathFinder 和 PortalManager 本身很轻量级\n * - 如果关键，可使用 `std::unique_ptr` 延迟构造\n *\n * ## 验证脚本\n *\n * ### 编译验证\n * ```bash\n * cmake --preset vcpkg-debug\n * cmake --build --preset vcpkg-debug 2>&1 | grep -E \"error:|warning:\"\n * ```\n *\n * ### 运行现有测试\n * ```bash\n * ctest --test-dir build/vcpkg-debug --output-on-failure\n * ```\n *\n * ### 性能基准\n * ```bash\n * ./build/vcpkg-debug/bin/combat_core_benchmark\n * # 比较修复前后的结果\n * ```\n *\n * ## 时间线\n *\n * - **第1天**：第一、二阶段（添加成员，双重实现）\n * - **第2天**：第三、四阶段（新接口，传送门迁移）\n * - **第3天**：测试和验证\n * - **第4天**：第五阶段（清理旧代码）\n *\n * ## 相关文件\n *\n * - `map_system.h` - 主系统定义\n * - `path_finder.h/cpp` - 新增寻路组件\n * - `portal_manager.h/cpp` - 新增传送门组件\n * - `MAP_SYSTEM_ARCHITECTURE.md` - 架构文档\n * - `INTEGRATION_GUIDE.md` - 本文档\n */
