/**
 * @file data_validator.cc
 * @brief Data validator implementation
 */

#include "persistence/data_validator.h"
#include "log/logger.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

#ifdef HAVE_OPENSSL
#include <openssl/sha.h>
#endif

namespace mir2::persistence {

void DataValidator::RegisterComponentValidator(
    const std::string& component_name,
    ComponentValidator validator) {
    validators_[component_name] = validator;
    SYSLOG_DEBUG("Registered validator for component: {}", component_name);
}

ValidationResult DataValidator::ValidateSnapshot(
    const std::vector<uint8_t>& data,
    const std::string& expected_checksum) {
    try {
        if (data.empty()) {
            return ValidationResult::Invalid("Empty snapshot data");
        }

        // Calculate checksum
        std::string calculated = CalculateChecksum(data);

        if (!expected_checksum.empty() && calculated != expected_checksum) {
            std::string error = "Checksum mismatch: expected " + expected_checksum +
                              ", got " + calculated;
            SYSLOG_ERROR("{}", error);
            return ValidationResult::Invalid(error);
        }

        SYSLOG_DEBUG("Snapshot checksum validation passed");
        return ValidationResult::Valid();
    } catch (const std::exception& e) {
        return ValidationResult::Invalid(
            std::string("Validation exception: ") + e.what());
    }
}

ValidationResult DataValidator::ValidateComponent(
    const std::string& component_name,
    const std::vector<uint8_t>& data) {
    try {
        auto it = validators_.find(component_name);
        if (it != validators_.end()) {
            return it->second(data);
        }

        // No validator registered for this component
        SYSLOG_DEBUG("No validator registered for component: {}", component_name);
        return ValidationResult::Valid();
    } catch (const std::exception& e) {
        return ValidationResult::Invalid(
            std::string("Component validation exception: ") + e.what());
    }
}

ValidationResult DataValidator::ValidateEntity(
    uint64_t entity_id,
    const std::string& entity_type,
    const std::vector<uint8_t>& data) {
    try {
        if (data.empty()) {
            return ValidationResult::Invalid("Empty entity data");
        }

        // Basic entity validation
        if (entity_type.empty()) {
            return ValidationResult::Invalid("Empty entity type");
        }

        SYSLOG_DEBUG("Entity {} validation passed (type: {})", entity_id, entity_type);
        return ValidationResult::Valid();
    } catch (const std::exception& e) {
        return ValidationResult::Invalid(
            std::string("Entity validation exception: ") + e.what());
    }
}

std::string DataValidator::CalculateChecksum(const std::vector<uint8_t>& data) {
#ifdef HAVE_OPENSSL
    // SHA256 using OpenSSL
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(data.data(), data.size(), hash);

    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(hash[i]);
    }
    return oss.str();
#else
    // Fallback: simple 32-bit checksum (not cryptographically secure)
    SYSLOG_WARN("OpenSSL not available, using weak checksum");
    uint32_t checksum = 0;
    for (uint8_t byte : data) {
        checksum = ((checksum << 1) | (checksum >> 31)) + byte;
    }

    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(8) << checksum;
    return oss.str();
#endif
}

}  // namespace mir2::persistence
