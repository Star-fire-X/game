/**
 * @file main.cpp
 * @brief WIL to PNG Converter Tool
 * 
 * 将传奇2的WIL图像资源批量转换为PNG格式
 * 
 * 用法:
 *   wil2png <wil_file_or_directory> -o <output_dir> [options]
 * 
 * 选项:
 *   -o, --output <dir>   输出目录（必需）
 *   --batch              批量模式，转换目录下所有WIL文件
 *   --verbose            显示详细进度
 *   --skip-empty         跳过空图像
 *   -h, --help           显示帮助信息
 */

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cstring>

#include <SDL.h>
#include <SDL_image.h>

#include "client/resource/resource_loader.h"

namespace fs = std::filesystem;

// 命令行参数
struct Options {
    std::string input_path;
    std::string output_dir;
    bool batch_mode = false;
    bool verbose = false;
    bool skip_empty = false;
    bool show_help = false;
};

// 打印帮助信息
void print_help(const char* program_name) {
    std::cout << "WIL to PNG Converter - 传奇2图像资源转换工具\n\n";
    std::cout << "用法:\n";
    std::cout << "  " << program_name << " <wil_file_or_directory> -o <output_dir> [options]\n\n";
    std::cout << "示例:\n";
    std::cout << "  " << program_name << " Data/Hum.wil -o output/\n";
    std::cout << "  " << program_name << " Data/ -o output/ --batch\n\n";
    std::cout << "选项:\n";
    std::cout << "  -o, --output <dir>   输出目录（必需）\n";
    std::cout << "  --batch              批量模式，转换目录下所有WIL文件\n";
    std::cout << "  --verbose            显示详细进度\n";
    std::cout << "  --skip-empty         跳过空图像\n";
    std::cout << "  -h, --help           显示帮助信息\n";
}

// 解析命令行参数
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
                opts.output_dir = argv[++i];
            } else {
                std::cerr << "错误: " << arg << " 需要指定输出目录\n";
                return false;
            }
        } else if (arg == "--batch") {
            opts.batch_mode = true;
        } else if (arg == "--verbose") {
            opts.verbose = true;
        } else if (arg == "--skip-empty") {
            opts.skip_empty = true;
        } else if (arg[0] != '-') {
            if (opts.input_path.empty()) {
                opts.input_path = arg;
            } else {
                std::cerr << "错误: 多个输入路径\n";
                return false;
            }
        } else {
            std::cerr << "错误: 未知选项 " << arg << "\n";
            return false;
        }
    }
    
    if (opts.input_path.empty()) {
        std::cerr << "错误: 未指定输入文件或目录\n";
        return false;
    }
    
    if (opts.output_dir.empty()) {
        std::cerr << "错误: 未指定输出目录 (-o)\n";
        return false;
    }
    
    return true;
}

/**
 * @brief 将Sprite保存为PNG文件
 * @param sprite 精灵数据
 * @param output_path 输出路径
 * @return true 如果保存成功
 */
bool save_sprite_as_png(const legend2::Sprite& sprite, const std::string& output_path) {
    if (!sprite.is_valid()) {
        return false;
    }
    
    // 创建SDL_Surface
    // RGBA格式：每像素4字节
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
        0, sprite.width, sprite.height, 32, SDL_PIXELFORMAT_RGBA32
    );
    
    if (!surface) {
        std::cerr << "创建SDL_Surface失败: " << SDL_GetError() << "\n";
        return false;
    }
    
    // 复制像素数据
    SDL_LockSurface(surface);
    // WIL 像素数据是从底部开始的，这里按行倒置写入确保导出的 PNG 方向正确
    const uint8_t* src = reinterpret_cast<const uint8_t*>(sprite.pixels.data());
    uint8_t* dst = static_cast<uint8_t*>(surface->pixels);
    const int row_bytes = sprite.width * static_cast<int>(sizeof(uint32_t));
    for (int y = 0; y < sprite.height; ++y) {
        const int src_y = sprite.height - 1 - y;  // bottom -> top
        std::memcpy(dst + y * surface->pitch, src + src_y * row_bytes, row_bytes);
    }
    SDL_UnlockSurface(surface);
    
    // 保存为PNG
    int result = IMG_SavePNG(surface, output_path.c_str());
    SDL_FreeSurface(surface);
    
    if (result != 0) {
        std::cerr << "保存PNG失败: " << IMG_GetError() << "\n";
        return false;
    }
    
    return true;
}

/**
 * @brief 转换单个WIL文件
 * @param wil_path WIL文件路径
 * @param output_dir 输出目录
 * @param opts 选项
 * @return 成功转换的图像数量，-1表示失败
 */
int convert_wil_file(const std::string& wil_path, const std::string& output_dir, 
                     const Options& opts) {
    legend2::WilArchive archive;
    
    if (!archive.load(wil_path)) {
        std::cerr << "加载WIL文件失败: " << wil_path << "\n";
        return -1;
    }
    
    std::string archive_name = archive.get_name();
    int image_count = archive.get_image_count();
    
    std::cout << "转换 " << archive_name << " (" << image_count << " 图像)...\n";
    
    // 创建输出子目录
    fs::path out_subdir = fs::path(output_dir) / archive_name;
    fs::create_directories(out_subdir);
    
    int converted = 0;
    int skipped = 0;
    int failed = 0;
    
    for (int i = 0; i < image_count; ++i) {
        auto sprite = archive.get_sprite(i);
        
        if (!sprite.has_value() || !sprite->is_valid()) {
            if (opts.skip_empty) {
                ++skipped;
                continue;
            }
            // 空图像也跳过，但不计入skipped
            ++skipped;
            continue;
        }
        
        // 构建输出路径
        fs::path output_path = out_subdir / (std::to_string(i) + ".png");
        
        if (save_sprite_as_png(*sprite, output_path.string())) {
            ++converted;
            if (opts.verbose) {
                std::cout << "  [" << i << "/" << image_count << "] " 
                          << sprite->width << "x" << sprite->height << " -> " 
                          << output_path.filename().string() << "\n";
            }
        } else {
            ++failed;
            if (opts.verbose) {
                std::cerr << "  [" << i << "] 转换失败\n";
            }
        }
    }
    
    std::cout << "  完成: " << converted << " 成功";
    if (skipped > 0) std::cout << ", " << skipped << " 跳过";
    if (failed > 0) std::cout << ", " << failed << " 失败";
    std::cout << "\n";
    
    return converted;
}

/**
 * @brief 查找目录下所有WIL文件
 */
std::vector<std::string> find_wil_files(const std::string& directory) {
    std::vector<std::string> wil_files;
    
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            // 转为小写比较
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".wil") {
                wil_files.push_back(entry.path().string());
            }
        }
    }
    
    // 按文件名排序
    std::sort(wil_files.begin(), wil_files.end());
    return wil_files;
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
    
    // 初始化SDL（只需要基本功能）
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL初始化失败: " << SDL_GetError() << "\n";
        return 1;
    }
    
    // 初始化SDL_image（PNG支持）
    int img_flags = IMG_INIT_PNG;
    if (!(IMG_Init(img_flags) & img_flags)) {
        std::cerr << "SDL_image初始化失败: " << IMG_GetError() << "\n";
        SDL_Quit();
        return 1;
    }
    
    // 创建输出目录
    fs::create_directories(opts.output_dir);
    
    int total_converted = 0;
    int total_files = 0;
    
    fs::path input_path(opts.input_path);
    
    if (fs::is_directory(input_path)) {
        if (!opts.batch_mode) {
            std::cerr << "输入是目录，请使用 --batch 选项进行批量转换\n";
            IMG_Quit();
            SDL_Quit();
            return 1;
        }
        
        // 批量模式
        auto wil_files = find_wil_files(opts.input_path);
        
        if (wil_files.empty()) {
            std::cerr << "目录中未找到WIL文件: " << opts.input_path << "\n";
            IMG_Quit();
            SDL_Quit();
            return 1;
        }
        
        std::cout << "找到 " << wil_files.size() << " 个WIL文件\n\n";
        
        for (const auto& wil_path : wil_files) {
            int converted = convert_wil_file(wil_path, opts.output_dir, opts);
            if (converted >= 0) {
                total_converted += converted;
                ++total_files;
            }
        }
    } else if (fs::is_regular_file(input_path)) {
        // 单文件模式
        int converted = convert_wil_file(opts.input_path, opts.output_dir, opts);
        if (converted >= 0) {
            total_converted += converted;
            ++total_files;
        }
    } else {
        std::cerr << "输入路径不存在: " << opts.input_path << "\n";
        IMG_Quit();
        SDL_Quit();
        return 1;
    }
    
    std::cout << "\n========================================\n";
    std::cout << "总计: " << total_files << " 个WIL文件, " 
              << total_converted << " 张图像已转换\n";
    
    // 清理
    IMG_Quit();
    SDL_Quit();
    
    return 0;
}
