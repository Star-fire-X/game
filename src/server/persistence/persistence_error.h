/**
 * @file persistence_error.h
 * @brief Persistence system error definitions and exception classes
 */

#ifndef MIR2_PERSISTENCE_ERROR_H
#define MIR2_PERSISTENCE_ERROR_H

#include <stdexcept>
#include <string>

namespace mir2::persistence {

/**
 * @brief Base exception for all persistence operations
 */
class PersistenceException : public std::runtime_error {
 public:
    explicit PersistenceException(const std::string& message)
        : std::runtime_error(message) {}
    virtual ~PersistenceException() = default;
};

/**
 * @brief Thrown when a component type is not registered
 */
class UnregisteredComponentException : public PersistenceException {
 public:
    explicit UnregisteredComponentException(const std::string& component_name)
        : PersistenceException("Component not registered: " + component_name) {}
};

/**
 * @brief Thrown when serialization fails
 */
class SerializationException : public PersistenceException {
 public:
    explicit SerializationException(const std::string& message)
        : PersistenceException("Serialization failed: " + message) {}
};

/**
 * @brief Thrown when deserialization fails
 */
class DeserializationException : public PersistenceException {
 public:
    explicit DeserializationException(const std::string& message)
        : PersistenceException("Deserialization failed: " + message) {}
};

/**
 * @brief Thrown when database operation fails
 */
class DatabaseException : public PersistenceException {
 public:
    explicit DatabaseException(const std::string& message)
        : PersistenceException("Database error: " + message) {}
};

/**
 * @brief Thrown when validation fails
 */
class ValidationException : public PersistenceException {
 public:
    explicit ValidationException(const std::string& message)
        : PersistenceException("Validation error: " + message) {}
};

/**
 * @brief Thrown when round-trip validation fails
 */
class RoundTripValidationException : public ValidationException {
 public:
    explicit RoundTripValidationException(const std::string& component_name)
        : ValidationException("Round-trip validation failed for: " + component_name) {}
};

}  // namespace mir2::persistence

#endif  // MIR2_PERSISTENCE_ERROR_H
