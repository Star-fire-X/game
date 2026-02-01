// =============================================================================
// Legend2 客户端 - 主入口点 (Main Entry Point)
//
// 功能说明:
//   - 解析命令行参数
//   - 创建并初始化游戏客户端
//   - 运行游戏主循环
//
// 命令行参数:
//   --fullscreen     全屏模式
//   --no-vsync       禁用垂直同步
//   --server <host>  服务器地址(默认: 127.0.0.1)
//   --port <port>    服务器端口(默认: 7000)
//   --connect        自动连接服务器
//   --width <w>      窗口宽度(默认: 800)
//   --height <h>     窗口高度(默认: 600)
//   --help, -h       显示帮助信息
// =============================================================================

#include "game/game_client.h"
#include <iostream>
#include <cstdlib>

// 使用新的模块化命名空间
using mir2::game::ClientConfig;
using mir2::game::GameClient;

/// 程序主入口
/// @param argc 命令行参数数量
/// @param argv 命令行参数数组
/// @return 程序退出码
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    // 设置默认客户端配置
    ClientConfig config;
    config.window_width = 800;
    config.window_height = 600;
    config.window_title = "Legend2 Client";
    config.target_fps = mir2::common::constants::TARGET_FPS;
    config.show_fps = true;
    
    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--fullscreen") {
            config.fullscreen = true;
        } else if (arg == "--no-vsync") {
            config.vsync = false;
        } else if (arg == "--server" && i + 1 < argc) {
            config.server_host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            config.server_port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "--connect") {
            config.auto_connect = true;
        } else if (arg == "--width" && i + 1 < argc) {
            config.window_width = std::atoi(argv[++i]);
        } else if (arg == "--height" && i + 1 < argc) {
            config.window_height = std::atoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            // 显示帮助信息
            std::cout << "Legend2 Client\n"
                      << "Usage: legend2_client [options]\n"
                      << "Options:\n"
                      << "  --fullscreen     Run in fullscreen mode\n"
                      << "  --no-vsync       Disable vsync\n"
                      << "  --server <host>  Server hostname (default: 127.0.0.1)\n"
                      << "  --port <port>    Server port (default: 7000)\n"
                      << "  --connect        Auto-connect to server\n"
                      << "  --width <w>      Window width (default: 800)\n"
                      << "  --height <h>     Window height (default: 600)\n"
                      << "  --help, -h       Show this help\n";
            return 0;
        }
    }

    // 创建并运行游戏客户端
    GameClient client;
    
    if (!client.initialize(config)) {
        std::cerr << "Failed to initialize game client" << std::endl;
        return 1;
    }
    
    int result = client.run();
    
    return result;
}
