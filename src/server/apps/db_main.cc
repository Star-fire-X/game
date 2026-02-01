#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

#include "db/db_server.h"

namespace {

std::atomic<bool> g_running{true};

void SignalHandler(int signal) {
  std::cout << "Received signal " << signal << ", shutting down..." << std::endl;
  g_running = false;
}

std::string ParseConfigPath(int argc, char* argv[]) {
  std::string config_path = "config/db.yaml";
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc) {
      config_path = argv[++i];
    }
  }
  return config_path;
}

}  // namespace

int main(int argc, char* argv[]) {
  const std::string config_path = ParseConfigPath(argc, argv);
  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);

  mir2::db::DbServer server;
  if (!server.Initialize(config_path)) {
    std::cerr << "DbServer init failed" << std::endl;
    return 1;
  }

  server.Run();

  while (g_running.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  server.Shutdown();
  return 0;
}
