/**
 * @file data_validator.h
 * @brief Data integrity validation (Epic 3: Story 3.2)
 */

#ifndef MIR2_PERSISTENCE_DATA_VALIDATOR_H
#define MIR2_PERSISTENCE_DATA_VALIDATOR_H

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mir2::persistence {

/**
 * @brief Validation result
 */
struct ValidationResult {
    bool is_valid;
    std::string error_message;
    std::vector<std::string> warnings;

    static ValidationResult Valid() {
        return ValidationResult{true, "", {}};
    }

    static ValidationResult Invalid(const std::string& error) {
        return ValidationResult{false, error, {}};
    }
};

/**
 * @brief Data integrity validator (Story 3.2)
 *
 * Responsibilities:
 * - Verify checksums (SHA256)
 * - Validate business rules (gold >= 0, level in range, etc.)
 * - Check component structure
 * - Detect missing entities
 * - Log validation failures
 *
 * Single-threaded (called during startup/load).
 */
class DataValidator {
 public:
    using ComponentValidator = std::function<ValidationResult(const std::vector<uint8_t>&)>;

    /**
     * @brief Create validator
     */
    DataValidator() = default;

    /**
     * @brief Register component-specific validator
     * @param component_name Component name
     * @param validator Validation function
     */
    void RegisterComponentValidator(
        const std::string& component_name,
        ComponentValidator validator);

    /**
     * @brief Validate snapshot data (Story 3.2)
     * @param data Serialized snapshot
     * @param checksum Expected SHA256 checksum
     * @return Validation result
     */
    ValidationResult ValidateSnapshot(
        const std::vector<uint8_t>& data,
        const std::string& checksum);

    /**
     * @brief Validate component data
     * @param component_name Component name
     * @param data Serialized component
     * @return Validation result
     */
    ValidationResult ValidateComponent(
        const std::string& component_name,
        const std::vector<uint8_t>& data);

    /**
     * @brief Validate entity structure
     */
    ValidationResult ValidateEntity(
        uint64_t entity_id,
        const std::string& entity_type,
        const std::vector<uint8_t>& data);

 private:
    std::map<std::string, ComponentValidator> validators_;

    /**
     * @brief Calculate SHA256 checksum
     */
    std::string CalculateChecksum(const std::vector<uint8_t>& data);
};

}  // namespace mir2::persistence

#endif  // MIR2_PERSISTENCE_DATA_VALIDATOR_H
