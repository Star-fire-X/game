/**
 * @file resource_loader.cpp
 * @brief Legend2 客户端资源加载器实现
 * 
 * 本文件实现游戏资源加载功能，包括：
 * - WIL/WIX 图像档案解析（支持8/16/24/32位色深）
 * - 地图文件(.map)解析
 * - Sprite 精灵数据提取和转换
 * - ResourceManager 资源管理和LRU缓存
 */

#include "resource_loader.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace mir2::client {

// =============================================================================
// Sprite 类实现
// =============================================================================

/**
 * @brief 序列化精灵为 JSON 字符串
 * @return JSON 字符串
 */
std::string Sprite::serialize() const {
    nlohmann::json j;
    to_json(j, *this);
    return j.dump();
}

/**
 * @brief 从 JSON 字符串反序列化精灵
 * @param data JSON 字符串
 * @return std::optional<Sprite> 成功返回精灵，失败返回 nullopt
 */
std::optional<Sprite> Sprite::deserialize(const std::string& data) {
    try {
        nlohmann::json j = nlohmann::json::parse(data);
        Sprite sprite;
        from_json(j, sprite);
        return sprite;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void to_json(nlohmann::json& j, const Sprite& s) {
    j = nlohmann::json{
        {"width", s.width},
        {"height", s.height},
        {"offset_x", s.offset_x},
        {"offset_y", s.offset_y},
        {"pixels", s.pixels}
    };
}

void from_json(const nlohmann::json& j, Sprite& s) {
    j.at("width").get_to(s.width);
    j.at("height").get_to(s.height);
    j.at("offset_x").get_to(s.offset_x);
    j.at("offset_y").get_to(s.offset_y);
    j.at("pixels").get_to(s.pixels);
}

// =============================================================================
// WilArchive 类实现
// =============================================================================

/**
 * @brief 加载 WIL 档案
 * @param wil_path WIL 文件路径
 * @return true 如果加载成功
 * 
 * 加载流程：
 * 1. 自动查找对应的 WIX 索引文件
 * 2. 加载 WIX 索引
 * 3. 加载 WIL 头部和调色板
 * 4. 构建图像大小表
 */
bool WilArchive::load(const std::string& wil_path) {
    loaded_ = false;
    wil_path_ = wil_path;
    
    // Derive WIX path from WIL path
    std::filesystem::path path(wil_path);
    std::string stem = path.stem().string();
    std::string parent = path.parent_path().string();
    
    // Try different WIX extensions (case variations)
    std::vector<std::string> wix_extensions = {".WIX", ".wix", ".Wix"};
    bool wix_found = false;
    
    for (const auto& ext : wix_extensions) {
        wix_path_ = parent.empty() ? (stem + ext) : (parent + "/" + stem + ext);
        if (std::filesystem::exists(wix_path_)) {
            wix_found = true;
            break;
        }
    }
    
    if (!wix_found) {
        // Try same case as WIL file
        std::string wil_ext = path.extension().string();
        if (wil_ext == ".wil") {
            wix_path_ = parent.empty() ? (stem + ".wix") : (parent + "/" + stem + ".wix");
        } else {
            wix_path_ = parent.empty() ? (stem + ".WIX") : (parent + "/" + stem + ".WIX");
        }
    }
    
    name_ = stem;
    
    // Load WIX index file
    if (!load_wix(wix_path_)) {
        return false;
    }
    
    // Load WIL header and palette
    if (!load_wil_header(wil_path_)) {
        return false;
    }

    // Build image size table for payload calculations
    build_image_sizes();
    
    loaded_ = true;
    return true;
}

/**
 * @brief 加载 WIX 索引文件
 * @param wix_path WIX 文件路径
 * @return true 如果加载成功
 * 
 * 支持两种格式：
 * - INDX 格式（ILIB）
 * - 传统 WEMADE 格式
 */
bool WilArchive::load_wix(const std::string& wix_path) {
    std::ifstream file(wix_path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Cannot open WIX file: " << wix_path << std::endl;
        return false;
    }
    
    // Peek header to detect INDX vs legacy WIX
    char magic[4] = {};
    file.read(magic, sizeof(magic));
    if (!file.good()) {
        std::cerr << "Failed to read WIX header" << std::endl;
        return false;
    }
    file.seekg(0, std::ios::beg);
    
    if (std::memcmp(magic, "#IND", 4) == 0) {
        // INDX header (ILIB format)
        char title[36] = {};
        file.read(title, sizeof(title));
        if (!file.good()) {
            std::cerr << "Failed to read INDX header" << std::endl;
            return false;
        }
        if (std::string(title, 5) != "#INDX") {
            std::cerr << "Invalid INDX signature" << std::endl;
            return false;
        }
        
        int32_t version = 0;
        int32_t data_offset = 0;
        int32_t index_count = 0;
        file.read(reinterpret_cast<char*>(&version), sizeof(int32_t));
        file.read(reinterpret_cast<char*>(&data_offset), sizeof(int32_t));
        file.read(reinterpret_cast<char*>(&index_count), sizeof(int32_t));
        if (!file.good()) {
            std::cerr << "Failed to read INDX metadata" << std::endl;
            return false;
        }
        (void)version;
        (void)data_offset;
        
        // Check if image count is reasonable
        if (index_count < 0 || index_count > 500000) {
            std::cerr << "Invalid INDX index count: " << index_count << std::endl;
            return false;
        }
        
        image_count_ = index_count;
        std::cout << "INDX: " << image_count_ << " images indexed" << std::endl;
        
        image_offsets_.resize(image_count_);
        file.read(reinterpret_cast<char*>(image_offsets_.data()),
                  image_count_ * sizeof(int32_t));
        return file.good();
    }
    
    // Legacy WIX header (WEMADE Pascal string)
    WixHeader header{};
    file.read(header.title, WIX_TITLE_SIZE);
    file.read(reinterpret_cast<char*>(&header.index_count), sizeof(int32_t));
    
    if (!file.good()) {
        std::cerr << "Failed to read WIX header" << std::endl;
        return false;
    }
    
    // Validate header
    if (!header.is_valid()) {
        std::cerr << "Invalid WIX signature" << std::endl;
        return false;
    }
    
    // Check version - new version header is 4 bytes shorter
    // Read ver_flag to determine version
    file.read(reinterpret_cast<char*>(&header.ver_flag), sizeof(int32_t));
    bool is_new_version = (header.ver_flag == 0) || (header.ver_flag == 65536);
    
    if (is_new_version) {
        // New version: ver_flag is actually the first index, seek back
        file.seekg(WIX_TITLE_SIZE + sizeof(int32_t), std::ios::beg);
    }
    
    // Check if image count is reasonable
    if (header.index_count < 0 || header.index_count > 500000) {
        // Attempt recovery using file size to infer count (some WIX headers are malformed)
        std::error_code ec;
        auto file_size = std::filesystem::file_size(wix_path, ec);
        if (!ec) {
            std::uintmax_t header_size = is_new_version
                ? static_cast<std::uintmax_t>(WIX_TITLE_SIZE + sizeof(int32_t))   // new version omits ver_flag
                : static_cast<std::uintmax_t>(WIX_TITLE_SIZE + 2 * sizeof(int32_t)); // legacy includes ver_flag
            if (file_size > header_size) {
                int fallback_count = static_cast<int>((file_size - header_size) / sizeof(int32_t));
                if (fallback_count > 0 && fallback_count <= 500000) {
                    std::cerr << "Invalid WIX index count: " << header.index_count
                              << ", using fallback " << fallback_count << std::endl;
                    header.index_count = fallback_count;
                }
            }
        }

        if (header.index_count < 0 || header.index_count > 500000) {
            std::cerr << "Invalid WIX index count: " << header.index_count << std::endl;
            return false;
        }
    }
    
    image_count_ = header.index_count;
    std::cout << "WIX: " << image_count_ << " images indexed" << std::endl;
    
    // Read image offsets (each is 4 bytes)
    image_offsets_.resize(image_count_);
    file.read(reinterpret_cast<char*>(image_offsets_.data()), 
              image_count_ * sizeof(int32_t));
    
    return file.good();
}

/**
 * @brief 加载 WIL 文件头
 * @param wil_path WIL 文件路径
 * @return true 如果加载成功
 * 
 * 读取文件头信息和调色板（8 位色深时）。
 */
bool WilArchive::load_wil_header(const std::string& wil_path) {
    std::ifstream file(wil_path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Cannot open WIL file: " << wil_path << std::endl;
        return false;
    }
    
    // Detect ILIB vs legacy WEMADE header
    char magic[4] = {};
    file.read(magic, sizeof(magic));
    if (!file.good()) {
        std::cerr << "Failed to read WIL header" << std::endl;
        return false;
    }
    file.seekg(0, std::ios::beg);
    
    if (std::memcmp(magic, "#ILI", 4) == 0) {
        // ILIB header (36-byte ASCII title + 5 int32 fields)
        char title[36] = {};
        file.read(title, sizeof(title));
        if (!file.good()) {
            std::cerr << "Failed to read ILIB header" << std::endl;
            return false;
        }
        if (std::string(title, 5) != "#ILIB") {
            std::cerr << "Invalid ILIB signature" << std::endl;
            return false;
        }
        
        int32_t version = 0;
        int32_t data_offset = 0;
        int32_t image_count = 0;
        int32_t color_count = 0;
        int32_t palette_size = 0;
        file.read(reinterpret_cast<char*>(&version), sizeof(int32_t));
        file.read(reinterpret_cast<char*>(&data_offset), sizeof(int32_t));
        file.read(reinterpret_cast<char*>(&image_count), sizeof(int32_t));
        file.read(reinterpret_cast<char*>(&color_count), sizeof(int32_t));
        file.read(reinterpret_cast<char*>(&palette_size), sizeof(int32_t));
        if (!file.good()) {
            std::cerr << "Failed to read ILIB metadata" << std::endl;
            return false;
        }
        (void)version;
        (void)data_offset;
        
        std::memset(wil_header_.title, 0, sizeof(wil_header_.title));
        std::memcpy(wil_header_.title, title, sizeof(title));
        wil_header_.image_count = image_count;
        wil_header_.color_count = color_count;
        wil_header_.palette_size = palette_size;
        wil_header_.ver_flag = 0;  // ILIB uses the 8-byte image header layout
        is_ilib_ = true;
    } else {
        // Legacy WIL header - Pascal string[40] (41 bytes)
        file.read(wil_header_.title, WIL_TITLE_SIZE);
        file.read(reinterpret_cast<char*>(&wil_header_.image_count), sizeof(int32_t));
        file.read(reinterpret_cast<char*>(&wil_header_.color_count), sizeof(int32_t));
        file.read(reinterpret_cast<char*>(&wil_header_.palette_size), sizeof(int32_t));
        file.read(reinterpret_cast<char*>(&wil_header_.ver_flag), sizeof(int32_t));
        
        if (!file.good()) {
            std::cerr << "Failed to read WIL header" << std::endl;
            return false;
        }
        
        if (!wil_header_.is_valid()) {
            std::cerr << "Invalid WIL signature" << std::endl;
            return false;
        }
        is_ilib_ = false;
    }
    
    // Determine version and bit depth
    bool is_new_version = wil_header_.is_new_version();
    int bit_depth = wil_header_.get_bit_depth();
    bit_depth_ = bit_depth;
    
    std::cout << "WIL Header: images=" << wil_header_.image_count 
              << ", colors=" << wil_header_.color_count 
              << ", bit_depth=" << bit_depth
              << ", new_version=" << (is_new_version ? "yes" : "no") << std::endl;
    
    // For legacy new version, the header is 4 bytes shorter, so seek back
    if (!is_ilib_ && is_new_version) {
        file.seekg(WIL_TITLE_SIZE + 3 * sizeof(int32_t), std::ios::beg);
    }
    
    // Read palette (only for 8-bit images)
    palette_.resize(256);
    if (bit_depth == 8) {
        // Read 256 RGBQUAD entries (4 bytes each: B, G, R, Reserved)
        std::vector<uint8_t> palette_data(256 * 4);
        file.read(reinterpret_cast<char*>(palette_data.data()), palette_data.size());
        
        for (int i = 0; i < 256; ++i) {
            uint8_t b = palette_data[i * 4 + 0];
            uint8_t g = palette_data[i * 4 + 1];
            uint8_t r = palette_data[i * 4 + 2];
            // Black (0,0,0) is transparent in Legend2
            uint8_t a = (r == 0 && g == 0 && b == 0) ? 0 : 255;
            palette_[i] = (a << 24) | (b << 16) | (g << 8) | r;  // ABGR format for SDL
        }
    } else {
        // For 16/24/32 bit, fill with identity palette
        for (int i = 0; i < 256; ++i) {
            palette_[i] = 0xFF000000 | (i << 16) | (i << 8) | i;
        }
    }
    
    std::error_code ec;
    wil_file_size_ = std::filesystem::file_size(wil_path, ec);
    if (ec) {
        wil_file_size_ = 0;
    }
    
    return true;
}

/**
 * @brief 获取精灵图像
 * @param index 图像索引
 * @return std::optional<Sprite> 成功返回精灵，失败返回 nullopt
 * 
 * 根据色深解码图像数据：
 * - 8 位：索引色，使用调色板
 * - 16 位：RGB565 格式
 * - 24 位：RGB 格式
 * - 32 位：BGRA 格式
 */
std::optional<Sprite> WilArchive::get_sprite(int index) {
    if (!loaded_ || index < 0 || index >= image_count_) {
        return std::nullopt;
    }
    
    int32_t offset = image_offsets_[index];
    
    // Offset 0 or negative typically means empty/invalid image
    if (offset <= 0) {
        return std::nullopt;
    }
    
    // Open WIL file if not already open
    if (!wil_file_.is_open()) {
        wil_file_.open(wil_path_, std::ios::binary);
        if (!wil_file_.is_open()) {
            return std::nullopt;
        }
    }
    
    // Seek to image data
    wil_file_.seekg(offset);
    if (!wil_file_.good()) {
        return std::nullopt;
    }
    
    // Read image header (8 bytes for new version, 12 bytes for old version)
    WilImageHeader img_header{};
    bool is_new_version = is_ilib_ || wil_header_.is_new_version();
    
    if (is_new_version) {
        // New version: only 8 bytes (no bits pointer)
        wil_file_.read(reinterpret_cast<char*>(&img_header), 8);
    } else {
        // Old version: 12 bytes (includes bits pointer which we ignore)
        wil_file_.read(reinterpret_cast<char*>(&img_header), sizeof(WilImageHeader));
        // Skip the 4-byte bits pointer
        wil_file_.seekg(4, std::ios::cur);
    }
    
    if (!wil_file_.good()) {
        return std::nullopt;
    }
    
    int width = img_header.width;
    int height = img_header.height;
    int offset_x = img_header.offset_x;
    int offset_y = img_header.offset_y;
    
    int header_bytes = is_new_version ? 8 : 12;
    int payload_size = get_image_payload_size(index, header_bytes);
    if (payload_size > 0) {
        int expected_default = calculate_data_size(width, height);
        int expected_swapped = calculate_data_size(img_header.offset_x, img_header.height);
        if (expected_swapped == payload_size && expected_default != payload_size) {
            width = img_header.offset_x;
            offset_x = img_header.width;
        }
    }
    
    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }
    
    // Sanity check dimensions
    if (width > 2048 || height > 2048) {
        return std::nullopt;
    }
    
    Sprite sprite;
    sprite.width = width;
    sprite.height = height;
    sprite.offset_x = offset_x;
    sprite.offset_y = offset_y;
    
    int pixel_count = sprite.width * sprite.height;
    sprite.pixels.resize(pixel_count);
    
    // Calculate data size based on bit depth
    int data_size = calculate_data_size(sprite.width, sprite.height);
    
    // Read raw pixel data
    std::vector<uint8_t> raw_data(data_size);
    wil_file_.read(reinterpret_cast<char*>(raw_data.data()), data_size);
    
    if (!wil_file_.good()) {
        return std::nullopt;
    }
    
    // Convert to RGBA based on bit depth
    switch (bit_depth_) {
        case 8: {
            // 8-bit indexed color with 4-byte row alignment
            int row_bytes = ((sprite.width * 8 + 31) / 32) * 4;
            for (int y = 0; y < sprite.height; ++y) {
                for (int x = 0; x < sprite.width; ++x) {
                    uint8_t color_index = raw_data[y * row_bytes + x];
                    sprite.pixels[y * sprite.width + x] = palette_[color_index];
                }
            }
            break;
        }
        case 16: {
            // 16-bit RGB565
            for (int i = 0; i < pixel_count; ++i) {
                uint16_t pixel = raw_data[i * 2] | (raw_data[i * 2 + 1] << 8);
                // RGB565 to RGBA8888
                uint8_t r = ((pixel >> 11) & 0x1F) << 3;
                uint8_t g = ((pixel >> 5) & 0x3F) << 2;
                uint8_t b = (pixel & 0x1F) << 3;
                uint8_t a = (pixel == 0) ? 0 : 255;  // Black is transparent
                sprite.pixels[i] = (a << 24) | (b << 16) | (g << 8) | r;
            }
            break;
        }
        case 24: {
            // 24-bit RGB
            for (int i = 0; i < pixel_count; ++i) {
                uint8_t b = raw_data[i * 3 + 0];
                uint8_t g = raw_data[i * 3 + 1];
                uint8_t r = raw_data[i * 3 + 2];
                uint8_t a = (r == 0 && g == 0 && b == 0) ? 0 : 255;
                sprite.pixels[i] = (a << 24) | (b << 16) | (g << 8) | r;
            }
            break;
        }
        case 32: {
            // 32-bit BGRA
            for (int i = 0; i < pixel_count; ++i) {
                uint8_t b = raw_data[i * 4 + 0];
                uint8_t g = raw_data[i * 4 + 1];
                uint8_t r = raw_data[i * 4 + 2];
                uint8_t a = raw_data[i * 4 + 3];
                if (a == 0 && r == 0 && g == 0 && b == 0) a = 0;
                else if (a == 0) a = 255;
                sprite.pixels[i] = (a << 24) | (b << 16) | (g << 8) | r;
            }
            break;
        }
        default:
            return std::nullopt;
    }
    
    return sprite;
}

/**
 * @brief 构建图像大小表
 * 
 * 根据相邻图像的偏移量差计算每个图像的大小。
 */
void WilArchive::build_image_sizes() {
    image_sizes_.assign(image_offsets_.size(), 0);
    if (wil_file_size_ == 0) {
        return;
    }
    
    int last_index = -1;
    int32_t last_offset = 0;
    
    for (size_t i = 0; i < image_offsets_.size(); ++i) {
        int32_t offset = image_offsets_[i];
        if (offset <= 0) {
            continue;
        }
        if (last_index >= 0) {
            int32_t size = offset - last_offset;
            if (size > 0) {
                image_sizes_[last_index] = size;
            }
        }
        last_index = static_cast<int>(i);
        last_offset = offset;
    }
    
    if (last_index >= 0) {
        int64_t size = static_cast<int64_t>(wil_file_size_) - last_offset;
        if (size > 0) {
            image_sizes_[last_index] = static_cast<int32_t>(size);
        }
    }
}

/**
 * @brief 计算图像数据大小
 * @param width 图像宽度
 * @param height 图像高度
 * @return 数据字节数
 */
int WilArchive::calculate_data_size(int width, int height) const {
    if (width <= 0 || height <= 0) {
        return 0;
    }
    
    if (bit_depth_ == 8) {
        // 8-bit: each row is aligned to 4 bytes
        int row_bytes = ((width * 8 + 31) / 32) * 4;
        return row_bytes * height;
    }
    
    return width * height * (bit_depth_ / 8);
}

/**
 * @brief 获取图像负载大小
 * @param index 图像索引
 * @param header_bytes 头部字节数
 * @return 负载字节数
 */
int WilArchive::get_image_payload_size(int index, int header_bytes) const {
    if (index < 0 || static_cast<size_t>(index) >= image_sizes_.size()) {
        return 0;
    }
    
    int size = image_sizes_[index] - header_bytes;
    return size > 0 ? size : 0;
}


// =============================================================================
// MapTile JSON 序列化
// =============================================================================

/**
 * @brief 将 MapTile 序列化为 JSON
 * @param j 输出的 JSON 对象
 * @param t 地图瓦片
 */
void to_json(nlohmann::json& j, const MapTile& t) {
    j = nlohmann::json{
        {"background", t.background},
        {"middle", t.middle},
        {"object", t.object},
        {"door_index", t.door_index},
        {"door_offset", t.door_offset},
        {"anim_frame", t.anim_frame},
        {"anim_tick", t.anim_tick},
        {"area", t.area},
        {"light", t.light}
    };
}

void from_json(const nlohmann::json& j, MapTile& t) {
    j.at("background").get_to(t.background);
    j.at("middle").get_to(t.middle);
    j.at("object").get_to(t.object);
    j.at("door_index").get_to(t.door_index);
    j.at("door_offset").get_to(t.door_offset);
    j.at("anim_frame").get_to(t.anim_frame);
    j.at("anim_tick").get_to(t.anim_tick);
    j.at("area").get_to(t.area);
    if (j.contains("light")) {
        j.at("light").get_to(t.light);
    } else if (j.contains("flag")) {
        j.at("flag").get_to(t.light);
    }
}

// =============================================================================
// MapData 类实现
// =============================================================================

/**
 * @brief 构建可行走性矩阵
 * @return 二维布尔矩阵
 */
std::vector<std::vector<bool>> MapData::build_walkability_matrix() const {
    std::vector<std::vector<bool>> matrix(height, std::vector<bool>(width, false));
    
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            matrix[y][x] = is_walkable(x, y);
        }
    }
    
    return matrix;
}

std::string MapData::serialize() const {
    nlohmann::json j;
    to_json(j, *this);
    return j.dump();
}

std::optional<MapData> MapData::deserialize(const std::string& data) {
    try {
        nlohmann::json j = nlohmann::json::parse(data);
        MapData map;
        from_json(j, map);
        return map;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void to_json(nlohmann::json& j, const MapData& m) {
    j = nlohmann::json{
        {"width", m.width},
        {"height", m.height},
        {"tiles", m.tiles}
    };
}

void from_json(const nlohmann::json& j, MapData& m) {
    j.at("width").get_to(m.width);
    j.at("height").get_to(m.height);
    j.at("tiles").get_to(m.tiles);
}

// =============================================================================
// MapLoader 类实现
// =============================================================================

// Legend2 地图文件格式常量
constexpr int MAP_METADATA_SIZE = 48;      ///< 标题/日期/保留字段的有效载荷
constexpr int MAP_BASE_HEADER_SIZE = 4 + MAP_METADATA_SIZE;   ///< 基础头部大小：4 字节（宽高）+ 48 字节（元数据）
constexpr int MAP_TILE_MIN_SIZE = 12;      ///< 最小瓦片数据大小

namespace {

struct MapFormat {
    int header_size;
    int tile_size;
    const char* name;
};

constexpr std::array<MapFormat, 5> kMapFormats = {{
    {52, 20, "v2-20b-tiles"},
    {52, 12, "v1-12b-tiles"},
    {64, 12, "v1-12b-tiles+ext-header"},
    {8092, 12, "v1-12b-tiles+event-header"},
    {9652, 12, "v1-12b-tiles+extended-header"},
}};

std::optional<MapFormat> detect_map_format(int64_t file_size, int width, int height) {
    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }

    const int64_t tile_count = static_cast<int64_t>(width) * static_cast<int64_t>(height);
    if (tile_count <= 0) {
        return std::nullopt;
    }

    for (const auto& fmt : kMapFormats) {
        const int64_t expected = static_cast<int64_t>(fmt.header_size) +
                                 tile_count * static_cast<int64_t>(fmt.tile_size);
        if (expected == file_size) {
            return fmt;
        }
    }

    // Fallback: infer header size for supported tile sizes
    constexpr std::array<int, 4> kTileSizes = {12, 20, 16, 14};
    for (int tile_size : kTileSizes) {
        const int64_t header_size = file_size - tile_count * static_cast<int64_t>(tile_size);
        if (header_size >= MAP_BASE_HEADER_SIZE && header_size <= file_size) {
            return MapFormat{static_cast<int>(header_size), tile_size, "unknown"};
        }
    }

    return std::nullopt;
}

} // namespace

/**
 * @brief 加载地图文件
 * @param map_path 地图文件路径
 * @return std::optional<MapData> 成功返回地图数据，失败返回 nullopt
 */
std::optional<MapData> MapLoader::load(const std::string& map_path) {
    std::ifstream file(map_path, std::ios::binary);
    if (!file.is_open()) {
        return std::nullopt;
    }

    std::error_code ec;
    const int64_t file_size = static_cast<int64_t>(std::filesystem::file_size(map_path, ec));
    if (ec || file_size < MAP_BASE_HEADER_SIZE) {
        std::cerr << "MapLoader: invalid file size for " << map_path << std::endl;
        return std::nullopt;
    }

    MapData map;
    int16_t width = 0;
    int16_t height = 0;

    if (!read_header(file, width, height)) {
        std::cerr << "MapLoader: failed to read header for " << map_path << std::endl;
        return std::nullopt;
    }

    auto format_opt = detect_map_format(file_size, width, height);
    if (!format_opt) {
        std::cerr << "MapLoader: unsupported map format for " << map_path
                  << " (size=" << file_size << ", width=" << width
                  << ", height=" << height << ")" << std::endl;
        return std::nullopt;
    }

    const MapFormat format = *format_opt;
    const int64_t tile_count = static_cast<int64_t>(width) * static_cast<int64_t>(height);
    const int64_t expected_size = static_cast<int64_t>(format.header_size) +
                                  tile_count * static_cast<int64_t>(format.tile_size);
    if (expected_size > file_size) {
        std::cerr << "MapLoader: map file truncated for " << map_path
                  << " (expected " << expected_size << ", got " << file_size << ")"
                  << std::endl;
        return std::nullopt;
    }
    if (expected_size < file_size) {
        std::cout << "MapLoader: extra trailing data detected for " << map_path
                  << " (extra=" << (file_size - expected_size) << " bytes)"
                  << std::endl;
    }

    std::cout << "MapLoader: " << map_path << " size=" << file_size
              << " width=" << width << " height=" << height
              << " header=" << format.header_size << " tile_size=" << format.tile_size
              << " format=" << format.name << std::endl;

    map.width = width;
    map.height = height;

    file.seekg(format.header_size, std::ios::beg);
    if (!file.good()) {
        std::cerr << "MapLoader: failed to seek to tile data for " << map_path << std::endl;
        return std::nullopt;
    }

    if (!read_tiles(file, map, format.tile_size)) {
        std::cerr << "MapLoader: failed to read tiles for " << map_path << std::endl;
        return std::nullopt;
    }

    return map;
}

/**
 * @brief 读取地图头部
 * @param file 文件流
 * @param width 输出宽度
 * @param height 输出高度
 * @return true 如果读取成功
 */
bool MapLoader::read_header(std::ifstream& file, int16_t& width, int16_t& height) {
    // 标准头部（52字节）:
    // - Bytes 0-1: Width (uint16, little-endian)
    // - Bytes 2-3: Height (uint16, little-endian)
    // 后续字段为标题、日期与保留字段（Delphi record）
    // 防破解头部（64字节）:
    // - Bytes 31-32: Width xor CheckKey
    // - Bytes 33-34: CheckKey (0xAA38)
    // - Bytes 35-36: Height xor CheckKey
    std::array<uint8_t, MAP_BASE_HEADER_SIZE> header{};
    file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (!file.good()) {
        return false;
    }

    auto read_le16 = [](const uint8_t* data) -> uint16_t {
        return static_cast<uint16_t>(data[0]) |
               (static_cast<uint16_t>(data[1]) << 8);
    };
    auto is_valid_dim = [](uint16_t w, uint16_t h) {
        return w > 0 && w <= 2000 && h > 0 && h <= 2000;
    };

    const uint16_t raw_width = read_le16(&header[0]);
    const uint16_t raw_height = read_le16(&header[2]);

    constexpr uint16_t kAntiHackKey = 0xAA38;
    const uint16_t enc_width = read_le16(&header[31]);
    const uint16_t key = read_le16(&header[33]);
    const uint16_t enc_height = read_le16(&header[35]);
    if (key == kAntiHackKey) {
        const uint16_t decoded_width = static_cast<uint16_t>(enc_width ^ key);
        const uint16_t decoded_height = static_cast<uint16_t>(enc_height ^ key);
        if (is_valid_dim(decoded_width, decoded_height)) {
            width = static_cast<int16_t>(decoded_width);
            height = static_cast<int16_t>(decoded_height);
            return true;
        }
    }

    if (!is_valid_dim(raw_width, raw_height)) {
        return false;
    }
    width = static_cast<int16_t>(raw_width);
    height = static_cast<int16_t>(raw_height);
    return true;
}

/**
 * @brief 读取地图瓦片数据
 * @param file 文件流
 * @param map 地图数据对象
 * @param tile_stride 单个瓦片数据长度（字节）
 * @return true 如果读取成功
 * 
 * 基础瓦片结构为 12 字节，其它版本可在末尾附加扩展数据：
 * - [0-1]: 背景层索引
 * - [2-3]: 中间层索引
 * - [4-5]: 物体层索引
 * - [6-7]: 门/传送门数据
 * - [8-9]: 动画数据
 * - [10]: 区域标识
 * - [11]: 光照强度
 */
bool MapLoader::read_tiles(std::ifstream& file, MapData& map, int tile_stride) {
    if (tile_stride < MAP_TILE_MIN_SIZE) {
        std::cerr << "MapLoader: invalid tile stride " << tile_stride << std::endl;
        return false;
    }

    const int64_t tile_count = static_cast<int64_t>(map.width) * static_cast<int64_t>(map.height);
    if (tile_count <= 0) {
        std::cerr << "MapLoader: invalid tile count " << tile_count << std::endl;
        return false;
    }

    map.tiles.resize(static_cast<size_t>(tile_count));
    
    // Legend2 map tile format (12 bytes per tile, column-major order):
    // - Bytes 0-1: Background tile index (uint16, little-endian)
    // - Bytes 2-3: Middle layer tile index (uint16, little-endian)
    // - Bytes 4-5: Object layer tile index (uint16, little-endian)
    // - Bytes 6-7: Door/portal data (door_index, door_offset)
    // - Bytes 8-9: Animation data (anim_frame, anim_tick)
    // - Byte 10: Area identifier
    // - Byte 11: Light level
    //
    // Walkability is determined by:
    // - Background/object high bit (0x8000)
    // - Door exists and is closed
    
    std::vector<uint8_t> tile_buffer(static_cast<size_t>(tile_stride));
    
    for (int64_t i = 0; i < tile_count; ++i) {
        file.read(reinterpret_cast<char*>(tile_buffer.data()), tile_stride);
        
        if (!file.good()) {
            const int tile_x = static_cast<int>(i / map.height);
            const int tile_y = static_cast<int>(i % map.height);
            std::cerr << "MapLoader: failed to read tile " << i
                      << " (" << tile_x << "," << tile_y << ")" << std::endl;
            return false;
        }
        
        const int tile_x = static_cast<int>(i / map.height);
        const int tile_y = static_cast<int>(i % map.height);
        const size_t out_index = static_cast<size_t>(tile_y * map.width + tile_x);
        MapTile& tile = map.tiles[out_index];
        
        // Parse tile data (little-endian)
        // Bytes 0-1: Background tile index
        tile.background = static_cast<uint16_t>(tile_buffer[0]) | 
                         (static_cast<uint16_t>(tile_buffer[1]) << 8);
        
        // Bytes 2-3: Middle layer tile index
        tile.middle = static_cast<uint16_t>(tile_buffer[2]) | 
                     (static_cast<uint16_t>(tile_buffer[3]) << 8);
        
        // Bytes 4-5: Object layer tile index
        tile.object = static_cast<uint16_t>(tile_buffer[4]) | 
                     (static_cast<uint16_t>(tile_buffer[5]) << 8);
        
        // Bytes 6-7: Door/portal data
        tile.door_index = tile_buffer[6];
        tile.door_offset = tile_buffer[7];
        
        // Bytes 8-9: Animation data
        tile.anim_frame = tile_buffer[8];
        tile.anim_tick = tile_buffer[9];
        
        // Byte 10: Area identifier
        tile.area = tile_buffer[10];
        
        // Byte 11: Light level
        tile.light = tile_buffer[11];
    }
    
    return true;
}

// =============================================================================
// ResourceManager 类实现
// =============================================================================

/**
 * @brief 构造函数 - 初始化缓存大小
 * @param sprite_cache_size 精灵缓存大小
 * @param map_cache_size 地图缓存大小
 */
ResourceManager::ResourceManager(size_t sprite_cache_size, size_t map_cache_size)
    : sprite_cache_(sprite_cache_size)
    , map_cache_(map_cache_size) {
}

bool ResourceManager::load_archive(const std::string& path) {
    return load_wil_archive(path);
}

/**
 * @brief 加载 WIL 档案
 * @param wil_path WIL 文件路径
 * @return true 如果加载成功
 */
bool ResourceManager::load_wil_archive(const std::string& wil_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto archive = std::make_unique<WilArchive>();
    
    if (!archive->load(wil_path)) {
        return false;
    }
    
    std::string name = archive->get_name();
    archives_[name] = std::move(archive);
    
    return true;
}

/**
 * @brief 获取精灵
 * @param archive_name 档案名称
 * @param index 精灵索引
 * @return std::optional<Sprite> 成功返回精灵，失败返回 nullopt
 * 
 * 优先从缓存获取，缓存未命中则从档案加载。
 */
std::optional<Sprite> ResourceManager::get_sprite(const std::string& archive_name, int index) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Check cache first
    std::string cache_key = make_sprite_key(archive_name, index);
    auto cached = sprite_cache_.get(cache_key);
    if (cached) {
        return cached;
    }

    // Find archive (case-insensitive)
    auto it = archives_.find(archive_name);
    if (it == archives_.end()) {
        // Try case-insensitive lookup
        std::string lower_name = archive_name;
        for (auto& c : lower_name) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        for (auto& kv : archives_) {
            std::string key_lower = kv.first;
            for (auto& c : key_lower) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (key_lower == lower_name) {
                it = archives_.find(kv.first);
                break;
            }
        }
        if (it == archives_.end()) {
            return std::nullopt;
        }
    }

    // Load sprite from archive
    auto sprite = it->second->get_sprite(index);
    if (sprite) {
        // Cache it
        sprite_cache_.put(cache_key, *sprite);
    }

    return sprite;
}

/**
 * @brief 加载地图
 * @param map_path 地图文件路径
 * @return std::optional<MapData> 成功返回地图数据，失败返回 nullopt
 * 
 * 优先从缓存获取，缓存未命中则从文件加载。
 */
std::optional<MapData> ResourceManager::load_map(const std::string& map_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Check cache first
    auto cached = map_cache_.get(map_path);
    if (cached) {
        return cached;
    }
    
    // Load from file
    auto map = map_loader_.load(map_path);
    if (map) {
        // Cache it
        map_cache_.put(map_path, *map);
    }
    
    return map;
}

/**
 * @brief 清除所有缓存
 */
void ResourceManager::clear_cache() {
    std::lock_guard<std::mutex> lock(mutex_);
    sprite_cache_.clear();
    map_cache_.clear();
}

/**
 * @brief 从缓存移除精灵
 * @param archive_name 档案名称
 * @param index 精灵索引
 * @return true 如果成功移除
 */
bool ResourceManager::remove_sprite_from_cache(const std::string& archive_name, int index) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string cache_key = make_sprite_key(archive_name, index);
    return sprite_cache_.remove(cache_key);
}

/**
 * @brief 从缓存移除地图
 * @param map_path 地图路径
 * @return true 如果成功移除
 */
bool ResourceManager::remove_map_from_cache(const std::string& map_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    return map_cache_.remove(map_path);
}

/**
 * @brief 检查精灵是否在缓存中
 * @param archive_name 档案名称
 * @param index 精灵索引
 * @return true 如果在缓存中
 */
bool ResourceManager::is_sprite_cached(const std::string& archive_name, int index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string cache_key = make_sprite_key(archive_name, index);
    return sprite_cache_.contains(cache_key);
}

/**
 * @brief 检查地图是否在缓存中
 * @param map_path 地图路径
 * @return true 如果在缓存中
 */
bool ResourceManager::is_map_cached(const std::string& map_path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return map_cache_.contains(map_path);
}

/**
 * @brief 检查档案是否已加载
 * @param archive_name 档案名称
 * @return true 如果已加载
 */
bool ResourceManager::is_archive_loaded(const std::string& archive_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return archives_.find(archive_name) != archives_.end();
}

/**
 * @brief 估算缓存内存使用量
 * @return 估算的字节数
 */
size_t ResourceManager::get_estimated_cache_memory() const {
    std::lock_guard<std::mutex> lock(mutex_);
    // Rough estimate: each sprite pixel is 4 bytes (RGBA)
    // This is a simplified estimate - actual memory usage may vary
    size_t sprite_memory = 0;
    // We can't iterate the cache directly, so we estimate based on average sprite size
    // Assuming average sprite is 64x64 pixels = 4096 pixels * 4 bytes = 16KB
    sprite_memory = sprite_cache_.size() * 64 * 64 * sizeof(uint32_t);
    
    // Map memory: each tile is ~12 bytes, average map is ~500x500 tiles
    // This is also a rough estimate
    size_t map_memory = map_cache_.size() * 500 * 500 * sizeof(MapTile);
    
    return sprite_memory + map_memory;
}

/**
 * @brief 生成精灵缓存键
 * @param archive 档案名称
 * @param index 精灵索引
 * @return 缓存键字符串
 */
std::string ResourceManager::make_sprite_key(const std::string& archive, int index) {
    return archive + ":" + std::to_string(index);
}

} // namespace mir2::client
