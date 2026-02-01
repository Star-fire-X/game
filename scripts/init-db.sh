#!/usr/bin/env bash

################################################################################
# Database Initialization Script
# Description: Execute SQL migration files in order (001-010.sql)
# Usage: ./scripts/init-db.sh
################################################################################

set -euo pipefail  # Exit on error, undefined variable, or pipe failure

# Color codes for output
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly NC='\033[0m' # No Color

# Configuration
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
readonly MIGRATIONS_DIR="${PROJECT_ROOT}/migrations"
readonly LOG_FILE="${PROJECT_ROOT}/logs/db-init-$(date +%Y%m%d_%H%M%S).log"

# Create logs directory if it doesn't exist
mkdir -p "$(dirname "${LOG_FILE}")"

################################################################################
# Logging Functions
################################################################################

log() {
    local level="$1"
    shift
    local message="$*"
    local timestamp
    timestamp=$(date '+%Y-%m-%d %H:%M:%S')

    echo -e "${timestamp} [${level}] ${message}" | tee -a "${LOG_FILE}"
}

log_info() {
    log "INFO" "${BLUE}$*${NC}"
}

log_success() {
    log "SUCCESS" "${GREEN}$*${NC}"
}

log_warning() {
    log "WARNING" "${YELLOW}$*${NC}"
}

log_error() {
    log "ERROR" "${RED}$*${NC}"
}

################################################################################
# Utility Functions
################################################################################

print_banner() {
    echo -e "${BLUE}"
    echo "╔════════════════════════════════════════════════════════════╗"
    echo "║          MIR2 Database Initialization Script             ║"
    echo "╚════════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
}

check_prerequisites() {
    log_info "Checking prerequisites..."

    # Check if psql is installed
    if ! command -v psql &> /dev/null; then
        log_error "psql command not found. Please install PostgreSQL client."
        exit 1
    fi

    # Check if migrations directory exists
    if [[ ! -d "${MIGRATIONS_DIR}" ]]; then
        log_error "Migrations directory not found: ${MIGRATIONS_DIR}"
        exit 1
    fi

    # Check if .env file exists
    if [[ ! -f "${PROJECT_ROOT}/.env" ]]; then
        log_warning ".env file not found. Using default values from .env.example"
        if [[ -f "${PROJECT_ROOT}/.env.example" ]]; then
            cp "${PROJECT_ROOT}/.env.example" "${PROJECT_ROOT}/.env"
            log_info "Created .env from .env.example"
        fi
    fi

    log_success "Prerequisites check passed"
}

load_env() {
    log_info "Loading environment variables..."

    if [[ -f "${PROJECT_ROOT}/.env" ]]; then
        # Export variables from .env file
        set -a
        source "${PROJECT_ROOT}/.env"
        set +a
    fi

    # Set defaults if not defined
    export POSTGRES_HOST="${MIR2_DB_HOST:-localhost}"
    export POSTGRES_PORT="${MIR2_DB_PORT:-5432}"
    export POSTGRES_DB="${MIR2_DB_NAME:-mir2}"
    export POSTGRES_USER="${MIR2_DB_USER:-mir2}"
    export POSTGRES_PASSWORD="${MIR2_DB_PASSWORD:-mir2_password}"

    log_success "Environment variables loaded"
    log_info "  Database: ${POSTGRES_DB}"
    log_info "  Host: ${POSTGRES_HOST}:${POSTGRES_PORT}"
    log_info "  User: ${POSTGRES_USER}"
}

test_connection() {
    log_info "Testing database connection..."

    export PGPASSWORD="${POSTGRES_PASSWORD}"

    if psql -h "${POSTGRES_HOST}" -p "${POSTGRES_PORT}" -U "${POSTGRES_USER}" -d postgres -c '\q' 2>/dev/null; then
        log_success "Database connection successful"
    else
        log_error "Failed to connect to database"
        log_error "  Host: ${POSTGRES_HOST}:${POSTGRES_PORT}"
        log_error "  User: ${POSTGRES_USER}"
        log_error "  Database: postgres"
        exit 1
    fi
}

create_database() {
    log_info "Checking if database '${POSTGRES_DB}' exists..."

    export PGPASSWORD="${POSTGRES_PASSWORD}"

    # Check if database exists
    local db_exists
    db_exists=$(psql -h "${POSTGRES_HOST}" -p "${POSTGRES_PORT}" -U "${POSTGRES_USER}" -d postgres -tAc \
        "SELECT 1 FROM pg_database WHERE datname='${POSTGRES_DB}'")

    if [[ "${db_exists}" == "1" ]]; then
        log_warning "Database '${POSTGRES_DB}' already exists"

        # Ask user if they want to recreate the database
        read -p "Do you want to drop and recreate the database? (y/N): " -n 1 -r
        echo

        if [[ $REPLY =~ ^[Yy]$ ]]; then
            log_warning "Dropping database '${POSTGRES_DB}'..."
            psql -h "${POSTGRES_HOST}" -p "${POSTGRES_PORT}" -U "${POSTGRES_USER}" -d postgres \
                -c "DROP DATABASE IF EXISTS ${POSTGRES_DB};" || {
                log_error "Failed to drop database"
                exit 1
            }
            log_success "Database dropped"

            log_info "Creating database '${POSTGRES_DB}'..."
            psql -h "${POSTGRES_HOST}" -p "${POSTGRES_PORT}" -U "${POSTGRES_USER}" -d postgres \
                -c "CREATE DATABASE ${POSTGRES_DB};" || {
                log_error "Failed to create database"
                exit 1
            }
            log_success "Database created"
        else
            log_info "Keeping existing database"
        fi
    else
        log_info "Creating database '${POSTGRES_DB}'..."
        psql -h "${POSTGRES_HOST}" -p "${POSTGRES_PORT}" -U "${POSTGRES_USER}" -d postgres \
            -c "CREATE DATABASE ${POSTGRES_DB};" || {
            log_error "Failed to create database"
            exit 1
        }
        log_success "Database created"
    fi
}

execute_migrations() {
    log_info "Starting migration execution..."

    export PGPASSWORD="${POSTGRES_PASSWORD}"

    local migration_files=()
    local success_count=0
    local failed_count=0

    # Find all migration files (001-010.sql)
    while IFS= read -r -d '' file; do
        migration_files+=("$file")
    done < <(find "${MIGRATIONS_DIR}" -name '[0-9][0-9][0-9]*.sql' -print0 | sort -z)

    if [[ ${#migration_files[@]} -eq 0 ]]; then
        log_error "No migration files found in ${MIGRATIONS_DIR}"
        exit 1
    fi

    log_info "Found ${#migration_files[@]} migration file(s)"
    echo

    # Execute each migration file in order
    for migration_file in "${migration_files[@]}"; do
        local filename
        filename=$(basename "${migration_file}")

        log_info "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        log_info "Executing: ${filename}"

        # Execute the SQL file
        if psql -h "${POSTGRES_HOST}" -p "${POSTGRES_PORT}" \
                -U "${POSTGRES_USER}" -d "${POSTGRES_DB}" \
                -f "${migration_file}" \
                -v ON_ERROR_STOP=1 \
                --echo-errors >> "${LOG_FILE}" 2>&1; then
            log_success "✓ ${filename} executed successfully"
            ((success_count++))
        else
            log_error "✗ ${filename} failed"
            ((failed_count++))

            log_error "Migration failed. Check log file: ${LOG_FILE}"
            log_error "Aborting remaining migrations..."
            exit 1
        fi

        echo
    done

    log_info "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    log_success "Migration Summary:"
    log_success "  Total: ${#migration_files[@]}"
    log_success "  Succeeded: ${success_count}"

    if [[ ${failed_count} -gt 0 ]]; then
        log_error "  Failed: ${failed_count}"
        exit 1
    fi
}

verify_tables() {
    log_info "Verifying created tables..."

    export PGPASSWORD="${POSTGRES_PASSWORD}"

    local tables
    tables=$(psql -h "${POSTGRES_HOST}" -p "${POSTGRES_PORT}" \
                  -U "${POSTGRES_USER}" -d "${POSTGRES_DB}" \
                  -tAc "SELECT tablename FROM pg_tables WHERE schemaname='public' ORDER BY tablename;")

    if [[ -n "${tables}" ]]; then
        log_success "Created tables:"
        while IFS= read -r table; do
            log_info "  - ${table}"
        done <<< "${tables}"
    else
        log_warning "No tables found in database"
    fi
}

cleanup() {
    # Unset password from environment
    unset PGPASSWORD
}

################################################################################
# Main Function
################################################################################

main() {
    # Set trap to cleanup on exit
    trap cleanup EXIT

    print_banner

    log_info "Starting database initialization at $(date '+%Y-%m-%d %H:%M:%S')"
    log_info "Log file: ${LOG_FILE}"
    echo

    # Execute initialization steps
    check_prerequisites
    load_env
    test_connection
    create_database
    execute_migrations
    verify_tables

    echo
    log_success "╔════════════════════════════════════════════════════════════╗"
    log_success "║     Database initialization completed successfully!      ║"
    log_success "╚════════════════════════════════════════════════════════════╝"
    log_info "Full log available at: ${LOG_FILE}"
}

# Run main function
main "$@"
