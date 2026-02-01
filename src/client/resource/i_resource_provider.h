// =============================================================================
// Legend2 资源提供者接口 (Resource Provider Interface)
// =============================================================================

#ifndef LEGEND2_CLIENT_RESOURCE_I_RESOURCE_PROVIDER_H
#define LEGEND2_CLIENT_RESOURCE_I_RESOURCE_PROVIDER_H

#include <optional>
#include <string>

namespace mir2::client {

struct Sprite;
struct MapData;

/// 资源提供者接口
/// 解耦资源获取与具体加载实现
class IResourceProvider {
public:
    virtual ~IResourceProvider() = default;

    /// 获取精灵
    virtual std::optional<Sprite> get_sprite(const std::string& archive, int index) = 0;

    /// 加载地图
    virtual std::optional<MapData> load_map(const std::string& path) = 0;

    /// 检查档案是否已加载
    virtual bool is_archive_loaded(const std::string& name) const = 0;

    /// 加载档案
    virtual bool load_archive(const std::string& path) = 0;
};

} // namespace mir2::client

#endif // LEGEND2_CLIENT_RESOURCE_I_RESOURCE_PROVIDER_H
