// =============================================================================
// Legend2 Path Utilities
//
// Provides helpers for resolving common client data paths.
// =============================================================================

#ifndef LEGEND2_CORE_PATH_UTILS_H
#define LEGEND2_CORE_PATH_UTILS_H

#include <string>

namespace mir2::core {

// Finds the Data/ directory by probing common relative paths.
// If test_file is provided, it must exist inside the Data/ directory.
// Returns a path with a trailing slash when found; returns empty string if not.
std::string find_data_path(const std::string& test_file = "");

}  // namespace mir2::core

#endif  // LEGEND2_CORE_PATH_UTILS_H
