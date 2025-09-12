#!/bin/bash

# publish-firmware-version.sh - Publish flight controller version via API
# Usage: ./scripts/deployment/publish-firmware-version.sh --version <version> [--dry-run]
# 
# This script publishes flight controller versions to the Remote Update API
# Integrates with the deployed API Gateway endpoints

set -euo pipefail

# Global variables
DRY_RUN=false
VERBOSE=false
VERSION=""
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# API Configuration
DEFAULT_API_URL="https://a5tk7g3y3j.execute-api.ca-central-1.amazonaws.com/dev"
API_ENDPOINT="/v1/components/flightController/publish"

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
API Version Publishing Script

USAGE:
    $(basename "$0") --version <version> [OPTIONS]

REQUIRED PARAMETERS:
    --version <version>     Version string to publish (e.g., v1.15.4-1.0.0)

OPTIONS:
    --dry-run              Show API request that would be made without publishing
    --verbose              Enable verbose output for debugging
    --help                 Show this help message

DESCRIPTION:
    Publishes flight controller versions to the Remote Update API Gateway endpoints.
    Automatically publishes both v6c and v6x binaries for the specified version.
    
    Features:
    - API key authentication with proper headers
    - Comprehensive response handling and validation
    - Retry logic with exponential backoff
    - Support for multiple environments
    - Dry-run mode for testing API connectivity

    API Endpoint:
    $DEFAULT_API_URL$API_ENDPOINT

ENVIRONMENT VARIABLES:
    UPDATE_API_URL          API Gateway base URL (default: $DEFAULT_API_URL)
    UPDATE_API_KEY          API key for authentication (required, no default)
    API_TIMEOUT             API request timeout in seconds (default: 30)
    MAX_RETRIES             Maximum retry attempts (default: 3)

EXAMPLES:
    $(basename "$0") --version v1.15.4-1.0.0 --dry-run
    $(basename "$0") --version v1.15.4-1.0.0 --verbose
    UPDATE_API_KEY=\$API_KEY $(basename "$0") --version v1.15.4-1.0.0

EOF
}

# Parse command line arguments
parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
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
    if [[ -z "$VERSION" ]]; then
        log_error "Missing required parameter: --version"
        exit 1
    fi
    
    # Validate version format (allow both strict and flexible formats)
    if [[ ! "$VERSION" =~ ^v[0-9]+\.[0-9]+\.[0-9]+-[0-9]+\.[0-9]+\.[0-9]+ ]] && [[ ! "$VERSION" =~ ^[a-zA-Z0-9.-]+$ ]]; then
        log_warn "Version format may not match expected pattern: $VERSION"
        log_warn "Expected format: v1.15.4-1.0.0 or development version"
    fi
    
    log_success "Parameter validation completed"
    log_info "Version: $VERSION"
}

# Validate environment configuration
validate_environment() {
    log_info "Validating environment configuration..."
    
    # Check API URL
    local api_url="${UPDATE_API_URL:-$DEFAULT_API_URL}"
    if [[ ! "$api_url" =~ ^https?:// ]]; then
        log_error "Invalid API URL format: $api_url"
        exit 1
    fi
    
    # Check API key
    if [[ -z "${UPDATE_API_KEY:-}" ]]; then
        log_error "UPDATE_API_KEY environment variable is required"
        log_error "Set it to your API key value before running this script"
        exit 1
    fi
    
    # Validate API key format (should not be empty or placeholder)
    if [[ "${UPDATE_API_KEY}" == "<retrieve-from-aws>" ]] || [[ "${UPDATE_API_KEY}" == "your-api-key-here" ]]; then
        log_error "UPDATE_API_KEY appears to be a placeholder. Please set it to the actual API key value."
        exit 1
    fi
    
    log_success "Environment validation completed"
    log_info "API URL: $api_url"
    log_info "API Key: ${UPDATE_API_KEY:0:8}..." # Show only first 8 characters for security
}

# Test API connectivity
test_api_connectivity() {
    local api_url="${UPDATE_API_URL:-$DEFAULT_API_URL}"
    
    log_info "Testing API connectivity..."
    
    if [[ "$DRY_RUN" == true ]]; then
        log_info "Skipping API connectivity test in dry-run mode"
        return 0
    fi
    
    # Test basic connectivity with a simple endpoint (if available)
    # For now, we'll just test that we can reach the domain
    local domain=$(echo "$api_url" | sed 's|https\?://||' | sed 's|/.*||')
    
    if command -v curl >/dev/null 2>&1; then
        if curl -f -s --connect-timeout 10 "https://$domain" >/dev/null 2>&1; then
            log_success "API connectivity verified"
        else
            log_warn "API connectivity test failed, but continuing (API may still work)"
        fi
    else
        log_warn "curl not available, skipping connectivity test"
    fi
}

# Get binary information from S3
get_binary_info() {
    local version="$1"
    local controller_type="$2"
    local aws_script="$SCRIPT_DIR/aws-cross-account.sh"
    
    # Extract version using metadata script for S3 path
    local metadata_script="$SCRIPT_DIR/generate-version-metadata.sh"
    if [[ ! -f "$metadata_script" ]]; then
        log_error "Version metadata script not found: $metadata_script" >&2
        return 1
    fi
    
    # Extract shortVersion from metadata script
    local short_version
    if ! short_version=$("$metadata_script" --output-json 2>/dev/null | jq -r '.shortVersion' 2>/dev/null); then
        log_error "Failed to extract version metadata for S3 path" >&2
        return 1
    fi
    
    if [[ -z "$short_version" ]] || [[ "$short_version" == "null" ]]; then
        log_error "Could not extract shortVersion from metadata script for S3 path" >&2
        return 1
    fi
    
    # Generate S3 key using extracted version
    local s3_key="${S3_KEY_PREFIX}/v${short_version}/px4_fmu-${controller_type}_default.px4"
    
    log_info "Retrieving binary info for $controller_type from S3: $s3_key" >&2
    
    if [[ "$DRY_RUN" == true ]]; then
        # Return mock data for dry-run
        cat << EOF
{
    "s3Key": "$s3_key",
    "sha256": "abc123def456789...",
    "size": 2097152,
    "exists": true
}
EOF
        return 0
    fi
    
    # Check if aws-cross-account.sh exists
    if [[ ! -f "$aws_script" ]] || [[ ! -x "$aws_script" ]]; then
        log_error "AWS cross-account script not found or not executable: $aws_script" >&2
        return 1
    fi
    
    # Get object metadata from S3
    local metadata_json
    if metadata_json=$("$aws_script" s3api head-object --bucket "$S3_BUCKET" --key "$s3_key" --output json 2>/dev/null); then
        # Extract information
        local size
        size=$(echo "$metadata_json" | jq -r '.ContentLength // 0')
        
        local sha256
        sha256=$(echo "$metadata_json" | jq -r '.Metadata.sha256 // "unknown"')
        
        # Return JSON with binary information
        cat << EOF
{
    "s3Key": "$s3_key",
    "sha256": "$sha256",
    "size": $size,
    "exists": true
}
EOF
        log_success "Binary info retrieved for $controller_type" >&2
        return 0
    else
        log_error "Binary not found in S3: $s3_key" >&2
        cat << EOF
{
    "s3Key": "$s3_key",
    "sha256": "",
    "size": 0,
    "exists": false
}
EOF
        return 1
    fi
}

# Generate release notes
generate_release_notes() {
    local version="$1"
    
    # Try to generate release notes using the version metadata script
    local metadata_script="$SCRIPT_DIR/generate-version-metadata.sh"
    
    if [[ -f "$metadata_script" ]] && [[ -x "$metadata_script" ]]; then
        log_info "Generating release notes using version metadata script..."
        local version_info
        if version_info=$("$metadata_script" --output-json 2>/dev/null); then
            local release_notes
            release_notes=$(echo "$version_info" | jq -r '.releaseNotes // "Updates and improvements"')
            echo "$release_notes"
            return 0
        fi
    fi
    
    # Fallback to simple release notes
    echo "Flight controller update to version $version with bug fixes and performance improvements"
}

# Create API request payload for individual binary
create_binary_payload() {
    local version="$1"
    local binary_info="$2"
    local controller_type="$3"
    
    log_info "Creating API request payload for $controller_type..."
    
    # Extract version using metadata script
    log_progress "Extracting version metadata for API payload..."
    
    local metadata_script="$SCRIPT_DIR/generate-version-metadata.sh"
    if [[ ! -f "$metadata_script" ]]; then
        log_error "Version metadata script not found: $metadata_script"
        return 1
    fi
    
    # Extract shortVersion from metadata script
    local api_version
    if ! api_version=$("$metadata_script" --output-json 2>/dev/null | jq -r '.shortVersion' 2>/dev/null); then
        log_error "Failed to extract version metadata for API"
        return 1
    fi
    
    if [[ -z "$api_version" ]] || [[ "$api_version" == "null" ]]; then
        log_error "Could not extract shortVersion from metadata script for API"
        return 1
    fi
    
    log_info "Using extracted API version: $api_version"
    
    # Extract binary information
    local s3_key sha256 size exists
    s3_key=$(echo "$binary_info" | jq -r '.s3Key')
    sha256=$(echo "$binary_info" | jq -r '.sha256')
    size=$(echo "$binary_info" | jq -r '.size')
    exists=$(echo "$binary_info" | jq -r '.exists')
    
    # Check if binary exists
    if [[ "$exists" != "true" ]]; then
        log_error "Binary does not exist for $controller_type"
        return 1
    fi
    
    # Generate release notes
    local release_notes
    if ! release_notes=$(generate_release_notes "$version"); then
        release_notes="Flight controller update to version $version"
    fi
    
    # Escape release notes for JSON
    local escaped_notes
    escaped_notes=$(echo "$release_notes" | sed 's/"/\\"/g' | sed 's/$/\\n/' | tr -d '\n' | sed 's/\\n$//')
    
    # Create payload matching Lambda expected schema
    local payload
    payload=$(cat << EOF
{
    "version": "$api_version",
    "s3Key": "$s3_key",
    "sha256": "$sha256",
    "size": $size,
    "releaseNotes": "$escaped_notes",
    "mandatory": false,
    "rolloutPercentage": 100
}
EOF
    )
    
    echo "$payload"
}

# Make API request with retry logic
make_api_request() {
    local payload="$1"
    local api_url="${UPDATE_API_URL:-$DEFAULT_API_URL}"
    local full_url="${api_url}${API_ENDPOINT}"
    local timeout="${API_TIMEOUT:-30}"
    local max_retries="${MAX_RETRIES:-3}"
    
    log_progress "Making API request to: $full_url"
    
    if [[ "$DRY_RUN" == true ]]; then
        log_info "🧪 DRY-RUN: Would make the following API request:"
        echo "URL: $full_url" >&2
        echo "Method: POST" >&2
        echo "Headers:" >&2
        echo "  Content-Type: application/json" >&2
        echo "  x-api-key: ${UPDATE_API_KEY:0:8}..." >&2
        echo "Payload:" >&2
        echo "$payload" | jq '.' 2>&1 || echo "$payload" >&2
        echo "" >&2
        echo "Curl command equivalent:" >&2
        echo "curl -X POST \\" >&2
        echo "  -H \"Content-Type: application/json\" \\" >&2
        echo "  -H \"x-api-key: \$UPDATE_API_KEY\" \\" >&2
        echo "  -d '$payload' \\" >&2
        echo "  \"$full_url\"" >&2
        return 0
    fi
    
    # Debug: Show the payload being sent
    log_info "DEBUG - Payload being sent:"
    echo "$payload" | jq '.' 2>&1 || echo "$payload" >&2
    
    # Make request with retry logic
    for ((attempt=1; attempt<=max_retries; attempt++)); do
        log_progress "API request attempt $attempt of $max_retries..."
        
        local response
        local http_code
        local curl_exit_code
        
        # Make the request
        response=$(curl -s -w "\n%{http_code}" \
            --max-time "$timeout" \
            -X POST \
            -H "Content-Type: application/json" \
            -H "x-api-key: ${UPDATE_API_KEY}" \
            -d "$payload" \
            "$full_url" 2>/dev/null)
        curl_exit_code=$?
        
        if [[ $curl_exit_code -eq 0 ]]; then
            # Extract HTTP status code and response body
            http_code=$(echo "$response" | tail -n1)
            local response_body
            response_body=$(echo "$response" | sed '$d')
            
            log_info "API Response: HTTP $http_code"
            
            # Handle different HTTP status codes
            case $http_code in
                200|201)
                    log_success "API request successful"
                    echo "$response_body"
                    return 0
                    ;;
                400)
                    log_error "Bad request (HTTP 400): Invalid payload or parameters"
                    log_error "Response: $response_body"
                    return 1
                    ;;
                401)
                    log_error "Unauthorized (HTTP 401): Invalid API key"
                    log_error "Check your UPDATE_API_KEY environment variable"
                    return 1
                    ;;
                403)
                    log_error "Forbidden (HTTP 403): API key lacks required permissions"
                    return 1
                    ;;
                409)
                    log_error "Conflict (HTTP 409): Version already exists"
                    log_error "Response: $response_body"
                    return 1
                    ;;
                429)
                    if [[ $attempt -lt $max_retries ]]; then
                        local delay=$((attempt * 5))
                        log_warn "Rate limited (HTTP 429), retrying in ${delay}s..."
                        sleep "$delay"
                        continue
                    else
                        log_error "Rate limited (HTTP 429) - maximum retries exceeded"
                        return 1
                    fi
                    ;;
                5*)
                    if [[ $attempt -lt $max_retries ]]; then
                        local delay=$((attempt * 2))
                        log_warn "Server error (HTTP $http_code), retrying in ${delay}s..."
                        sleep "$delay"
                        continue
                    else
                        log_error "Server error (HTTP $http_code) - maximum retries exceeded"
                        log_error "Response: $response_body"
                        return 1
                    fi
                    ;;
                *)
                    log_error "Unexpected HTTP status code: $http_code"
                    log_error "Response: $response_body"
                    return 1
                    ;;
            esac
        else
            # Curl failed
            if [[ $attempt -lt $max_retries ]]; then
                local delay=$((attempt * 2))
                log_warn "Request failed (curl exit code: $curl_exit_code), retrying in ${delay}s..."
                sleep "$delay"
                continue
            else
                log_error "Request failed after $max_retries attempts (curl exit code: $curl_exit_code)"
                return 1
            fi
        fi
    done
}

# Parse and display API response
parse_api_response() {
    local response="$1"
    
    log_info "Parsing API response..."
    
    if [[ -z "$response" ]]; then
        log_warn "Empty API response"
        return 0
    fi
    
    # Try to parse JSON response
    if echo "$response" | jq . >/dev/null 2>&1; then
        log_success "Flight controller version published successfully"
        
        # Display key information from response
        local published_version
        published_version=$(echo "$response" | jq -r '.version // "N/A"')
        echo "Published version: $published_version"
        
        # Display published binaries
        local binaries
        binaries=$(echo "$response" | jq -r '.binaries[]? | "  - \(.type): \(.s3Key)"' 2>/dev/null || echo "  Binary information not available in response")
        if [[ -n "$binaries" ]]; then
            echo "Published binaries:"
            echo "$binaries"
        fi
        
        # Display any additional response information
        local status
        status=$(echo "$response" | jq -r '.status // ""')
        if [[ -n "$status" ]] && [[ "$status" != "null" ]]; then
            echo "Status: $status"
        fi
    else
        log_warn "Response is not valid JSON, displaying raw response:"
        echo "$response"
    fi
}

# Main function
main() {
    log_info "Starting flight controller version publishing process..."
    
    if [[ "$DRY_RUN" == true ]]; then
        log_info "🧪 DRY-RUN MODE: No actual API requests will be made"
    fi
    
    # Parse arguments
    parse_args "$@"
    
    # Validate parameters
    validate_parameters
    
    # Validate environment
    validate_environment
    
    # Test API connectivity
    test_api_connectivity
    
    # Get binary information from S3
    local v6c_info v6x_info
    log_info "Checking for v6c and v6x binaries in S3..."
    
    v6c_info=$(get_binary_info "$VERSION" "v6c")
    local v6c_status=$?
    
    v6x_info=$(get_binary_info "$VERSION" "v6x")
    local v6x_status=$?
    
    # Check if at least one binary exists
    local v6c_exists v6x_exists
    v6c_exists=$(echo "$v6c_info" | jq -r '.exists')
    v6x_exists=$(echo "$v6x_info" | jq -r '.exists')
    
    if [[ "$v6c_exists" != "true" ]] && [[ "$v6x_exists" != "true" ]]; then
        log_error "No binaries found for version $VERSION"
        log_error "Please ensure binaries are uploaded to S3 before publishing"
        exit 1
    fi
    
    if [[ "$v6c_exists" == "true" ]]; then
        log_success "Found v6c binary for version $VERSION"
    fi
    
    if [[ "$v6x_exists" == "true" ]]; then
        log_success "Found v6x binary for version $VERSION"
    fi
    
    # Publish each binary type separately
    local success_count=0
    local total_binaries=0
    
    # Publish v6c binary if it exists
    if [[ "$v6c_exists" == "true" ]]; then
        total_binaries=$((total_binaries + 1))
        log_info "Publishing v6c binary..."
        
        local v6c_payload
        if v6c_payload=$(create_binary_payload "$VERSION" "$v6c_info" "v6c"); then
            local v6c_response
            if v6c_response=$(make_api_request "$v6c_payload"); then
                if [[ "$DRY_RUN" == false ]]; then
                    log_success "v6c binary published successfully"
                    parse_api_response "$v6c_response"
                fi
                success_count=$((success_count + 1))
            else
                log_error "Failed to publish v6c binary"
            fi
        else
            log_error "Failed to create payload for v6c binary"
        fi
    fi
    
    # Publish v6x binary if it exists
    if [[ "$v6x_exists" == "true" ]]; then
        total_binaries=$((total_binaries + 1))
        log_info "Publishing v6x binary..."
        
        local v6x_payload
        if v6x_payload=$(create_binary_payload "$VERSION" "$v6x_info" "v6x"); then
            local v6x_response
            if v6x_response=$(make_api_request "$v6x_payload"); then
                if [[ "$DRY_RUN" == false ]]; then
                    log_success "v6x binary published successfully"
                    parse_api_response "$v6x_response"
                fi
                success_count=$((success_count + 1))
            else
                log_error "Failed to publish v6x binary"
            fi
        else
            log_error "Failed to create payload for v6x binary"
        fi
    fi
    
    # Check overall success
    if [[ $success_count -eq $total_binaries ]]; then
        log_success "All flight controller binaries published successfully ($success_count/$total_binaries)"
    elif [[ $success_count -gt 0 ]]; then
        log_warn "Partial success: $success_count/$total_binaries binaries published"
        exit 1
    else
        log_error "No binaries were published successfully"
        exit 1
    fi
}

# Execute main function with all arguments
main "$@"