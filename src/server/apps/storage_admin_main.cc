#include <iostream>
#include <string>

#include "storage_engine/utils/storage_admin_tool.h"

int main(int argc, char** argv) {
  mir2::storage_engine::utils::storage_admin::CommandOptions options;
  std::string error;
  if (!mir2::storage_engine::utils::storage_admin::ParseCommandLine(
          argc, argv, &options, &error)) {
    const std::string usage =
        mir2::storage_engine::utils::storage_admin::BuildUsage(
            argc > 0 ? argv[0] : "mir2_storage_admin");
    if (!error.empty()) {
      std::cerr << "Error: " << error << "\n\n" << usage;
      return 1;
    }
    std::cout << usage;
    return 0;
  }

  const auto result =
      mir2::storage_engine::utils::storage_admin::Execute(options);
  if (!result.stdout_text.empty()) {
    std::cout << result.stdout_text;
  }
  if (!result.stderr_text.empty()) {
    std::cerr << result.stderr_text;
  }
  return result.exit_code;
}
