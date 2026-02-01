/**
 * @file main.cpp
 * @brief PNG元数据生成器 - 为导出的PNG文件生成偏移量等元数据
 * 
 * 用法:
 *   png_metadata_generator <wil_directory> <png_directory> -o <output_json>
 * 
 * 功能:
 *   1. 读取原始WIL文件的精灵偏移量信息
 *   2. 扫描对应的PNG导出目录
 *   3. 生成包含所有元数据的JSON文件
 * 
 * 输出JSON格式:
 *   {
 *     "archive_name": "Tiles",
 *     "image_count": 5000,
 *     "sprites": [
 *       {
 *         "index": 0,
 *         "filename": "0.png",
 *         "width": 48,
 *         "height": 32,
 *         "offset_x": 0,
 *         "offset_y": 0
 *       },
 *       ...
 *     ]
 *   }
 */

#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>
#include <vector>
#include <algorithm>

#include <nlohmann/json.hpp>
#include "client/resource/resource_loader.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

struct Options {
    std::string wil_directory;
    std::string png_directory;
    std::string output_json;
    bool verbose = false;
    bool show_help = false;
};

void print_help(const char* program_name) {
    std::cout << "PNG元数据生成器 - 为导出的PNG生成偏移量元数据\n\n";
    std::cout << "用法:\n";
    std::cout << "  " << program_name << " <wil_directory> <png_directory> -o <output_json> [options]\n\n";
    std::cout << "示例:\n";
    std::cout << "  " << program_name << " Data/ wil2png_all/ -o metadata_all.json\n";
    std::cout << "  " << program_name << " Data/Tiles.wil wil2png_all/Tiles/ -o tiles_meta.json\n\n";
    std::cout << "选项:\n";
    std::cout << "  -o, --output <file>  输出JSON文件（必需）\n";
    std::cout << "  --verbose            显示详细进度\n";
    std::cout << "  -h, --help           显示帮助信息\n";
}

bool parse_args(int argc, char* argv[], Options& opts) {
    if (argc < 2) {
        return false;
    }
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            opts.show_help = true;
            return true;
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) {
                opts.output_json = argv[++i];
            } else {
                std::cerr << "错误: " << arg << " 需要指定输出文件\n";
                return false;
            }
        } else if (arg == "--verbose") {
            opts.verbose = true;
        } else if (arg[0] != '-') {
            if (opts.wil_directory.empty()) {
                opts.wil_directory = arg;
            } else if (opts.png_directory.empty()) {
                opts.png_directory = arg;
            } else {
                std::cerr << "错误: 多余的参数 " << arg << "\n";
                return false;
            }
        } else {
            std::cerr << "错误: 未知选项 " << arg << "\n";
            return false;
        }
    }
    
    if (opts.wil_directory.empty()) {
        std::cerr << "错误: 未指定WIL目录\n";
        return false;
    }
    
    if (opts.png_directory.empty()) {
        std::cerr << "错误: 未指定PNG目录\n";
        return false;
    }
    
    if (opts.output_json.empty()) {
        std::cerr << "错误: 未指定输出JSON文件 (-o)\n";
        return false;
    }
    
    return true;
}

/**
 * @brief 查找目录下所有WIL文件
 */
std::vector<std::string> find_wil_files(const std::string& directory) {
    std::vector<std::string> wil_files;
    
    if (!fs::is_directory(directory)) {
        // 如果是单个文件，直接返回
        if (fs::is_regular_file(directory)) {
            std::string ext = fs::path(directory).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".wil") {
                wil_files.push_back(directory);
            }
        }
        return wil_files;
    }
    
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".wil") {
                wil_files.push_back(entry.path().string());
            }
        }
    }
    
    std::sort(wil_files.begin(), wil_files.end());
    return wil_files;
}

/**
 * @brief 生成单个WIL档案的元数据
 */
bool generate_metadata_for_archive(const std::string& wil_path,
                                    const std::string& png_dir,
                                    json& output_json,
                                    const Options& opts) {
    legend2::WilArchive archive;
    
    if (!archive.load(wil_path)) {
        std::cerr << "加载WIL文件失败: " << wil_path << "\n";
        return false;
    }
    
    std::string archive_name = archive.get_name();
    int image_count = archive.get_image_count();
    
    std::cout << "处理 " << archive_name << " (" << image_count << " 图像)...\n";
    
    // 构建PNG子目录路径
    fs::path png_subdir = fs::path(png_dir) / archive_name;
    if (!fs::exists(png_subdir)) {
        std::cerr << "PNG目录不存在: " << png_subdir << "\n";
        return false;
    }
    
    // 构建JSON结构
    json archive_json;
    archive_json["archive_name"] = archive_name;
    archive_json["image_count"] = image_count;
    archive_json["wil_path"] = wil_path;
    archive_json["png_directory"] = png_subdir.string();
    
    json sprites_array = json::array();
    
    int valid_count = 0;
    int invalid_count = 0;
    
    for (int i = 0; i < image_count; ++i) {
        auto sprite_opt = archive.get_sprite(i);
        
        // 构建PNG文件路径
        fs::path png_path = png_subdir / (std::to_string(i) + ".png");
        bool png_exists = fs::exists(png_path);
        
        json sprite_json;
        sprite_json["index"] = i;
        sprite_json["filename"] = std::to_string(i) + ".png";
        sprite_json["png_exists"] = png_exists;
        
        if (sprite_opt && sprite_opt->is_valid()) {
            sprite_json["width"] = sprite_opt->width;
            sprite_json["height"] = sprite_opt->height;
            sprite_json["offset_x"] = sprite_opt->offset_x;
            sprite_json["offset_y"] = sprite_opt->offset_y;
            sprite_json["valid"] = true;
            valid_count++;
            
            if (opts.verbose && png_exists) {
                std::cout << "  [" << i << "] " 
                          << sprite_opt->width << "x" << sprite_opt->height
                          << " offset=(" << sprite_opt->offset_x << "," 
                          << sprite_opt->offset_y << ")\n";
            }
        } else {
            sprite_json["width"] = 0;
            sprite_json["height"] = 0;
            sprite_json["offset_x"] = 0;
            sprite_json["offset_y"] = 0;
            sprite_json["valid"] = false;
            invalid_count++;
        }
        
        sprites_array.push_back(sprite_json);
    }
    
    archive_json["sprites"] = sprites_array;
    archive_json["statistics"] = {
        {"valid_sprites", valid_count},
        {"invalid_sprites", invalid_count},
        {"total_sprites", image_count}
    };
    
    output_json[archive_name] = archive_json;
    
    std::cout << "  完成: " << valid_count << " 有效, " 
              << invalid_count << " 无效\n";
    
    return true;
}

int main(int argc, char* argv[]) {
    Options opts;
    
    if (!parse_args(argc, argv, opts)) {
        print_help(argv[0]);
        return 1;
    }
    
    if (opts.show_help) {
        print_help(argv[0]);
        return 0;
    }
    
    // 查找所有WIL文件
    auto wil_files = find_wil_files(opts.wil_directory);
    
    if (wil_files.empty()) {
        std::cerr << "未找到WIL文件: " << opts.wil_directory << "\n";
        return 1;
    }
    
    std::cout << "找到 " << wil_files.size() << " 个WIL文件\n\n";
    
    json output_json;
    output_json["metadata_version"] = "1.0";
    output_json["generated_at"] = "2026-01-16";  // 可使用std::chrono生成时间戳
    output_json["wil_directory"] = opts.wil_directory;
    output_json["png_directory"] = opts.png_directory;
    
    json archives_json = json::object();
    
    int success_count = 0;
    int fail_count = 0;
    
    for (const auto& wil_path : wil_files) {
        if (generate_metadata_for_archive(wil_path, opts.png_directory, 
                                          archives_json, opts)) {
            success_count++;
        } else {
            fail_count++;
        }
    }
    
    output_json["archives"] = archives_json;
    output_json["summary"] = {
        {"total_archives", wil_files.size()},
        {"success_archives", success_count},
        {"failed_archives", fail_count}
    };
    
    // 写入JSON文件
    std::ofstream out_file(opts.output_json);
    if (!out_file.is_open()) {
        std::cerr << "无法创建输出文件: " << opts.output_json << "\n";
        return 1;
    }
    
    out_file << output_json.dump(2);  // 2空格缩进
    out_file.close();
    
    std::cout << "\n========================================\n";
    std::cout << "元数据已生成: " << opts.output_json << "\n";
    std::cout << "处理档案: " << success_count << " 成功, " 
              << fail_count << " 失败\n";
    
    return 0;
}
