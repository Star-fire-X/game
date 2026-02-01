#include "core/utils.h"

#include <algorithm>
#include <chrono>
#include <cctype>

namespace mir2::core {

int64_t GetCurrentTimestampMs() {
  const auto now = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             now.time_since_epoch())
      .count();
}

std::string ToLower(const std::string& input) {
  std::string output = input;
  std::transform(output.begin(), output.end(), output.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return output;
}

}  // namespace mir2::core
