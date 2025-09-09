#!/bin/bash

# upload-flight-controller-binary.sh - Upload flight controller binaries to S3 with checksums
# Usage: ./scripts/deployment/upload-flight-controller-binary.sh --file <path> --type <v6c|v6x> --version <version> [--dry-run]
# 
# This script uploads PX4 firmware binaries to S3 with proper versioning, checksums, and metadata
# Supports v6c and v6x flight controller types

set -euo pipefail

# Global variables
DRY_RUN=false
VERBOSE=false
FORCE_OVERWRITE=false
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Parameters
FILE_PATH=""
CONTROLLER_TYPE=""
VERSION=""

# S3 Configuration
S3_BUCKET="avestec-dev-update-binaries"
S3_KEY_PREFIX="flight-controller"

# Color codes for output
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly CYAN='\033[0;36m'
readonly NC='\033[0m' # No Color

# Progress tracking variables
UPLOAD_START_TIME=""
TOTAL_SIZE=0

# Logging functions
log_info() {
    if [[ "$VERBOSE" == true ]] || [[ "$DRY_RUN" == true ]]; then
        echo -e "${BLUE}[INFO]${NC} $1" >&2
    fi
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1" >&2
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

log_success() {
    if [[ "$VERBOSE" == true ]] || [[ "$DRY_RUN" == true ]]; then
        echo -e "${GREEN}[SUCCESS]${NC} $1" >&2
    fi
}

log_progress() {
    if [[ "$VERBOSE" == true ]] || [[ "$DRY_RUN" == true ]]; then
        echo -e "${CYAN}[PROGRESS]${NC} $1" >&2
    fi
}

# Help function
show_help() {
    cat << EOF
S3 Binary Upload Script

USAGE:
    $(basename "$0") --file <path> --type <v6c|v6x> --version <version> [OPTIONS]

REQUIRED PARAMETERS:
    --file <path>       Path to the PX4 firmware binary file
    --type <v6c|v6x>    Flight controller type (v6c or v6x)
    --version <version> Version string (e.g., v1.15.4-1.0.0)

OPTIONS:
    --dry-run           Show what would be uploaded without actual upload
    --verbose           Enable verbose output for debugging
    --force-overwrite   Overwrite existing files (default: fail if exists)
    --help              Show this help message

DESCRIPTION:
    Uploads PX4 firmware binaries to S3 with comprehensive metadata and integrity checking.
    
    Features:
    - SHA256 checksum generation and validation
    - Progress reporting for large files
    - Comprehensive error handling with retry logic
    - S3 metadata tagging for build traceability
    - Cross-account AWS authentication support

    S3 Upload Path:
    s3://$S3_BUCKET/$S3_KEY_PREFIX/v{version}/px4_fmu-{type}_default.px4

ENVIRONMENT VARIABLES:
    AWS_ACCOUNT_ID              Target AWS account ID
    AWS_REGION                  AWS region (default: ca-central-1)
    CROSS_ACCOUNT_ROLE_ARN      Cross-account role ARN
    CROSS_ACCOUNT_EXTERNAL_ID   External ID for role assumption
    UPLOAD_TIMEOUT              Upload timeout in seconds (default: 300)
    MAX_RETRIES                 Maximum retry attempts (default: 3)

EXAMPLES:
    $(basename "$0") --file build/px4_fmu-v6c_default.px4 --type v6c --version v1.15.4-1.0.0 --dry-run
    $(basename "$0") --file build/px4_fmu-v6x_default.px4 --type v6x --version v1.15.4-1.0.0 --verbose

EOF
}

# Parse command line arguments
parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --file)
                FILE_PATH="$2"
                shift 2
                ;;
            --type)
                CONTROLLER_TYPE="$2"
                shift 2
                ;;
            --version)
                VERSION="$2"
                shift 2
                ;;
            --dry-run)
                DRY_RUN=true
                VERBOSE=true  # Enable verbose output in dry-run mode
                shift
                ;;
            --verbose)
                VERBOSE=true
                shift
                ;;
            --force-overwrite)
                FORCE_OVERWRITE=true
                shift
                ;;
            --help|-h)
                show_help
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                echo "Use --help for usage information"
                exit 1
                ;;
        esac
    done
}

# Validate input parameters
validate_parameters() {
    log_info "Validating input parameters..."
    
    # Check required parameters
    if [[ -z "$FILE_PATH" ]]; then
        log_error "Missing required parameter: --file"
        exit 1
    fi
    
    if [[ -z "$CONTROLLER_TYPE" ]]; then
        log_error "Missing required parameter: --type"
        exit 1
    fi
    
    if [[ -z "$VERSION" ]]; then
        log_error "Missing required parameter: --version"
        exit 1
    fi
    
    # Validate controller type
    if [[ "$CONTROLLER_TYPE" != "v6c" ]] && [[ "$CONTROLLER_TYPE" != "v6x" ]]; then
        log_error "Invalid controller type: $CONTROLLER_TYPE. Must be 'v6c' or 'v6x'"
        exit 1
    fi
    
    # Validate version format
    if [[ ! "$VERSION" =~ ^v[0-9]+\.[0-9]+\.[0-9]+-[0-9]+\.[0-9]+\.[0-9]+ ]] && [[ ! "$VERSION" =~ ^[a-zA-Z0-9.-]+$ ]]; then
        log_warn "Version format may not match expected pattern: $VERSION"
        log_warn "Expected format: v1.15.4-1.0.0 or development version"
    fi
    
    # Validate file exists and is readable
    if [[ ! -f "$FILE_PATH" ]]; then
        log_error "File not found: $FILE_PATH"
        exit 1
    fi
    
    if [[ ! -r "$FILE_PATH" ]]; then
        log_error "File not readable: $FILE_PATH"
        exit 1
    fi
    
    # Get file size for progress tracking
    TOTAL_SIZE=$(stat -f%z "$FILE_PATH" 2>/dev/null || stat -c%s "$FILE_PATH" 2>/dev/null || echo "0")
    
    log_success "Parameter validation completed"
    log_info "File: $FILE_PATH"
    log_info "Type: $CONTROLLER_TYPE"
    log_info "Version: $VERSION"
    log_info "File size: $(format_bytes $TOTAL_SIZE)"
}

# Format bytes for human-readable output
format_bytes() {
    local bytes=$1
    if [[ $bytes -lt 1024 ]]; then
        echo "${bytes}B"
    elif [[ $bytes -lt 1048576 ]]; then
        echo "$((bytes/1024))KB"
    elif [[ $bytes -lt 1073741824 ]]; then
        echo "$((bytes/1048576))MB"
    else
        echo "$((bytes/1073741824))GB"
    fi
}

# Generate S3 key path
generate_s3_key() {
    local s3_key="${S3_KEY_PREFIX}/${VERSION}/px4_fmu-${CONTROLLER_TYPE}_default.px4"
    echo "$s3_key"
}

# Generate SHA256 checksum
generate_checksum() {
    local file_path="$1"
    log_progress "Generating SHA256 checksum..."
    
    local checksum
    if command -v sha256sum >/dev/null 2>&1; then
        checksum=$(sha256sum "$file_path" | cut -d' ' -f1)
    elif command -v shasum >/dev/null 2>&1; then
        checksum=$(shasum -a 256 "$file_path" | cut -d' ' -f1)
    else
        log_error "Neither sha256sum nor shasum command found"
        exit 1
    fi
    
    log_success "Checksum generated: $checksum"
    echo "$checksum"
}

# Validate AWS environment
validate_aws_environment() {
    log_info "Validating AWS environment..."
    
    # Check if aws-cross-account.sh exists
    local aws_script="$SCRIPT_DIR/aws-cross-account.sh"
    if [[ ! -f "$aws_script" ]]; then
        log_error "AWS cross-account script not found: $aws_script"
        exit 1
    fi
    
    if [[ ! -x "$aws_script" ]]; then
        log_error "AWS cross-account script not executable: $aws_script"
        exit 1
    fi
    
    # Test AWS authentication
    if [[ "$DRY_RUN" == false ]]; then
        log_info "Testing AWS authentication..."
        if ! "$aws_script" sts get-caller-identity >/dev/null 2>&1; then
            log_error "AWS authentication failed. Check your credentials and cross-account setup."
            exit 1
        fi
        log_success "AWS authentication verified"
    else
        log_info "Skipping AWS authentication test in dry-run mode"
    fi
}

# Check if S3 object already exists
check_s3_object_exists() {
    local s3_key="$1"
    local aws_script="$SCRIPT_DIR/aws-cross-account.sh"
    
    log_info "Checking if S3 object already exists: s3://$S3_BUCKET/$s3_key"
    
    if [[ "$DRY_RUN" == true ]]; then
        log_info "Skipping S3 existence check in dry-run mode"
        return 0
    fi
    
    if "$aws_script" s3api head-object --bucket "$S3_BUCKET" --key "$s3_key" >/dev/null 2>&1; then
        if [[ "$FORCE_OVERWRITE" == true ]]; then
            log_warn "Object exists but --force-overwrite specified, will overwrite"
            return 0
        else
            log_error "S3 object already exists: s3://$S3_BUCKET/$s3_key"
            log_error "Use --force-overwrite to overwrite existing files"
            exit 1
        fi
    else
        log_success "S3 path is available"
        return 0
    fi
}

# Upload file to S3 with metadata
upload_to_s3() {
    local file_path="$1"
    local s3_key="$2"
    local checksum="$3"
    local aws_script="$SCRIPT_DIR/aws-cross-account.sh"
    
    # Build metadata
    local git_commit
    local git_branch
    local build_date
    
    git_commit=$(git rev-parse HEAD 2>/dev/null || echo "unknown")
    git_branch=$(git branch --show-current 2>/dev/null || echo "unknown")
    build_date=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
    
    local metadata="git-commit=$git_commit,git-branch=$git_branch,build-date=$build_date,controller-type=$CONTROLLER_TYPE,version=$VERSION,sha256=$checksum,file-size=$TOTAL_SIZE"
    
    log_progress "Uploading to S3: s3://$S3_BUCKET/$s3_key"
    log_info "Metadata: $metadata"
    
    if [[ "$DRY_RUN" == true ]]; then
        log_info "🧪 DRY-RUN: Would upload file with the following command:"
        echo "    $aws_script s3 cp \"$file_path\" \"s3://$S3_BUCKET/$s3_key\" \\"
        echo "      --content-type \"application/octet-stream\" \\"
        echo "      --metadata \"$metadata\" \\"
        echo "      --metadata-directive REPLACE"
        return 0
    fi
    
    # Start timing upload
    UPLOAD_START_TIME=$(date +%s)
    
    # Perform upload with retry logic
    local max_retries="${MAX_RETRIES:-3}"
    local timeout="${UPLOAD_TIMEOUT:-300}"
    
    for ((attempt=1; attempt<=max_retries; attempt++)); do
        log_progress "Upload attempt $attempt of $max_retries..."
        
        # Try with timeout if available, otherwise run directly
        if command -v timeout >/dev/null 2>&1; then
            timeout_cmd="timeout $timeout"
        else
            timeout_cmd=""
        fi
        
        if $timeout_cmd "$aws_script" s3 cp "$file_path" "s3://$S3_BUCKET/$s3_key" \
            --content-type "application/octet-stream" \
            --metadata "$metadata" \
            --metadata-directive REPLACE; then
            
            # Calculate upload duration
            local upload_end_time=$(date +%s)
            local upload_duration=$((upload_end_time - UPLOAD_START_TIME))
            local upload_speed
            
            if [[ $upload_duration -gt 0 ]]; then
                upload_speed=$((TOTAL_SIZE / upload_duration))
                log_success "Upload completed in ${upload_duration}s ($(format_bytes $upload_speed)/s)"
            else
                log_success "Upload completed"
            fi
            
            return 0
        else
            local exit_code=$?
            if [[ $attempt -eq $max_retries ]]; then
                log_error "Upload failed after $max_retries attempts (exit code: $exit_code)"
                return 1
            else
                local delay=$((attempt * 2))
                log_warn "Upload attempt $attempt failed, retrying in ${delay}s..."
                sleep "$delay"
            fi
        fi
    done
}

# Verify upload integrity
verify_upload_integrity() {
    local s3_key="$1"
    local expected_checksum="$2"
    local aws_script="$SCRIPT_DIR/aws-cross-account.sh"
    
    log_progress "Verifying upload integrity..."
    
    if [[ "$DRY_RUN" == true ]]; then
        log_info "🧪 DRY-RUN: Would verify upload integrity"
        return 0
    fi
    
    # Get object metadata to verify upload
    local metadata_json
    if metadata_json=$("$aws_script" s3api head-object --bucket "$S3_BUCKET" --key "$s3_key" --output json 2>/dev/null); then
        # Extract metadata - user metadata is under "Metadata" key
        local stored_checksum
        stored_checksum=$(echo "$metadata_json" | jq -r '.Metadata.sha256 // empty' 2>/dev/null)
        
        local stored_size
        stored_size=$(echo "$metadata_json" | jq -r '.ContentLength // empty' 2>/dev/null)
        
        # Verify checksum
        if [[ "$stored_checksum" == "$expected_checksum" ]]; then
            log_success "Checksum verification passed"
        else
            log_error "Checksum mismatch! Expected: $expected_checksum, Got: $stored_checksum"
            return 1
        fi
        
        # Verify file size
        if [[ "$stored_size" == "$TOTAL_SIZE" ]]; then
            log_success "File size verification passed ($stored_size bytes)"
        else
            log_error "File size mismatch! Expected: $TOTAL_SIZE, Got: $stored_size"
            return 1
        fi
        
        log_success "Upload integrity verification completed"
        return 0
    else
        log_error "Failed to retrieve object metadata for verification"
        return 1
    fi
}

# Cleanup function for error handling
cleanup_on_error() {
    local s3_key="$1"
    local aws_script="$SCRIPT_DIR/aws-cross-account.sh"
    
    if [[ "$DRY_RUN" == false ]] && [[ -n "$s3_key" ]]; then
        log_warn "Cleaning up failed upload..."
        "$aws_script" s3 rm "s3://$S3_BUCKET/$s3_key" >/dev/null 2>&1 || true
    fi
}

# Main function
main() {
    log_info "Starting S3 binary upload process..."
    
    if [[ "$DRY_RUN" == true ]]; then
        log_info "🧪 DRY-RUN MODE: No actual uploads will be performed"
    fi
    
    # Parse arguments
    parse_args "$@"
    
    # Validate parameters
    validate_parameters
    
    # Validate AWS environment
    validate_aws_environment
    
    # Generate S3 key
    local s3_key
    s3_key=$(generate_s3_key)
    log_info "Target S3 path: s3://$S3_BUCKET/$s3_key"
    
    # Check if object already exists
    check_s3_object_exists "$s3_key"
    
    # Generate checksum
    local checksum
    checksum=$(generate_checksum "$FILE_PATH")
    
    # Upload to S3
    if [[ "$DRY_RUN" == true ]]; then
        log_info "🧪 DRY-RUN: Upload summary:"
        echo "  Source file: $FILE_PATH"
        echo "  Target S3: s3://$S3_BUCKET/$s3_key"
        echo "  File size: $(format_bytes $TOTAL_SIZE)"
        echo "  SHA256: $checksum"
        echo "  Controller type: $CONTROLLER_TYPE"
        echo "  Version: $VERSION"
        log_success "🧪 DRY-RUN: S3 upload simulation completed successfully"
    else
        # Perform actual upload
        if upload_to_s3 "$FILE_PATH" "$s3_key" "$checksum"; then
            # Verify upload integrity
            if verify_upload_integrity "$s3_key" "$checksum"; then
                log_success "S3 binary upload completed successfully"
                echo "Upload details:"
                echo "  S3 Location: s3://$S3_BUCKET/$s3_key"
                echo "  File Size: $(format_bytes $TOTAL_SIZE)"
                echo "  SHA256: $checksum"
                echo "  Version: $VERSION"
            else
                cleanup_on_error "$s3_key"
                log_error "Upload verification failed"
                exit 1
            fi
        else
            cleanup_on_error "$s3_key"
            log_error "S3 upload failed"
            exit 1
        fi
    fi
}

# Handle script interruption
trap 'cleanup_on_error "${s3_key:-}"' EXIT

# Execute main function with all arguments
main "$@"