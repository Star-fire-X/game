#ifndef MIR2_TESTS_SERVER_STORAGE_ENGINE_TEST_BACKEND_MOCKS_H_
#define MIR2_TESTS_SERVER_STORAGE_ENGINE_TEST_BACKEND_MOCKS_H_

#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "storage_engine/interfaces/storage_backend.h"

namespace mir2::storage_engine::test {

class NoopStorageBackend : public IStorageBackend {
 public:
  StorageResult Save(const std::string&,
                     uint64_t,
                     const std::vector<uint8_t>&) override {
    return StorageResult{true, "", 0};
  }

  StorageResult SaveBatch(
      const std::vector<std::tuple<std::string, uint64_t, std::vector<uint8_t>>>&)
      override {
    return StorageResult{true, "", 0};
  }

  StorageResult Delete(const std::string&,
                       uint64_t,
                       bool) override {
    return StorageResult{true, "", 0};
  }

  StorageResult DeleteBatch(
      const std::vector<std::pair<std::string, uint64_t>>&,
      bool) override {
    return StorageResult{true, "", 0};
  }

  std::optional<std::pair<uint64_t, std::vector<uint8_t>>> Load(
      const std::string&) override {
    return std::nullopt;
  }

  std::optional<std::map<std::string, std::pair<uint64_t, std::vector<uint8_t>>>> LoadAll()
      override {
    return std::nullopt;
  }

  StorageResult Validate() override {
    return StorageResult{true, "", 0};
  }

  bool IsHealthy() const override {
    return true;
  }
};

}  // namespace mir2::storage_engine::test

#endif  // MIR2_TESTS_SERVER_STORAGE_ENGINE_TEST_BACKEND_MOCKS_H_
