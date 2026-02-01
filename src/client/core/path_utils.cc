// =============================================================================
// Legend2 Path Utilities
// =============================================================================

#include "core/path_utils.h"

#include <filesystem>
#include <vector>

namespace mir2::core {
namespace {

const std::vector<std::string> kDataPaths = {
    "Data/",
    "../Data/",
    "../../Data/",
    "./Data/"
};

bool has_entry(const std::string& base, const std::string& test_file) {
    std::error_code ec;
    if (test_file.empty()) {
        return std::filesystem::is_directory(base, ec);
    }

    const std::filesystem::path candidate = std::filesystem::path(base) / test_file;
    return std::filesystem::exists(candidate, ec);
}

}  // namespace

std::string find_data_path(const std::string& test_file) {
    for (const auto& base : kDataPaths) {
        if (has_entry(base, test_file)) {
            return base;
        }
    }
    return "";
}

}  // namespace mir2::core
