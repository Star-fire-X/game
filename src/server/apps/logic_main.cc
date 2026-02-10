#include <iostream>
#include <string>

#include "logic/logic_server.h"

namespace {

std::string ParseConfigPath(int argc, char* argv[]) {
  std::string config_path = "config/logic.yaml";
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

  mir2::logic::LogicServer server;
  if (!server.Initialize(config_path)) {
    std::cerr << "LogicServer init failed" << std::endl;
    return 1;
  }

  server.Run();
  return 0;
}
