#!/bin/bash

# publish-avesaid-version.sh - Publish AvesAID version via API
# Usage: ./publish-avesaid-version.sh --version <version>

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
S3_BUCKET="avestec-prod-update-binaries"
API_URL="${UPDATE_API_URL:-https://a5tk7g3y3j.execute-api.ca-central-1.amazonaws.com/dev}"
API_ENDPOINT="/v1/components/flightController/publish"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --version) VERSION="$2"; shift 2 ;;
        --verbose) shift ;;  # Ignore for compatibility
        *) echo "Error: Unknown option: $1" >&2; exit 1 ;;
    esac
done

# Validate
[[ -z "${VERSION:-}" ]] && { echo "Error: Missing --version parameter" >&2; exit 1; }
[[ -z "${UPDATE_API_KEY:-}" ]] && { echo "Error: UPDATE_API_KEY environment variable required" >&2; exit 1; }

SHORT_VERSION="${VERSION#v}"

# Generate release notes from git history (truncate to 1000 chars for API Gateway limit)
RELEASE_NOTES=$(git log --format="• %s" HEAD~10..HEAD 2>/dev/null | \
    grep -v "^• Merged in" | head -20 | tr '\n' ' ' | \
    sed 's/  */ /g' | sed 's/^ *• *//' || echo "Updates and improvements")

# Truncate release notes to 1000 characters (API Gateway limit)
if [ ${#RELEASE_NOTES} -gt 1000 ]; then
    RELEASE_NOTES="${RELEASE_NOTES:0:997}..."
    echo "Release notes truncated to 1000 characters" >&2
fi

# Publish binaries for both controller types
PUBLISHED_COUNT=0
TOTAL_COUNT=0

for TYPE in v6c v6x; do
    S3_KEY="flight-controller/v${SHORT_VERSION}/px4_fmu-${TYPE}_default.px4"

    # Check if binary exists in S3
    if ! METADATA=$("$SCRIPT_DIR/aws-cross-account.sh" s3api head-object \
        --bucket "$S3_BUCKET" \
        --key "$S3_KEY" \
        --output json 2>/dev/null); then
        echo "Warning: Binary not found for $TYPE, skipping" >&2
        continue
    fi

    TOTAL_COUNT=$((TOTAL_COUNT + 1))

    # Extract S3 metadata
    SHA256=$(echo "$METADATA" | jq -r '.Metadata.sha256 // empty')
    SIZE=$(echo "$METADATA" | jq -r '.ContentLength')

    # Validate metadata
    if [[ -z "$SHA256" ]] || [[ "$SHA256" == "null" ]]; then
        echo "Error: SHA256 not found in S3 metadata for $TYPE" >&2
        exit 1
    fi

    if [[ -z "$SIZE" ]] || [[ "$SIZE" == "null" ]] || [[ "$SIZE" -le 0 ]]; then
        echo "Error: Invalid size ($SIZE) in S3 metadata for $TYPE" >&2
        exit 1
    fi

    # Create API payload using jq for proper JSON escaping
    PAYLOAD=$(jq -n \
        --arg version "$SHORT_VERSION" \
        --arg s3Key "$S3_KEY" \
        --arg sha256 "$SHA256" \
        --argjson size "$SIZE" \
        --arg releaseNotes "$RELEASE_NOTES" \
        '{
            version: $version,
            s3Key: $s3Key,
            sha256: $sha256,
            size: $size,
            releaseNotes: $releaseNotes,
            mandatory: false,
            rolloutPercentage: 100
        }')

    # Debug: Show payload (only in verbose mode or if DEBUG env var is set)
    if [[ "${DEBUG:-false}" == "true" ]]; then
        echo "DEBUG: API Payload for $TYPE:" >&2
        echo "$PAYLOAD" | jq '.' >&2
        echo "---" >&2
    fi

    # Make API request
    echo "Publishing $TYPE version $SHORT_VERSION..."

    RESPONSE=$(curl -s -w "\n%{http_code}" \
        -X POST \
        -H "Content-Type: application/json" \
        -H "x-api-key: ${UPDATE_API_KEY}" \
        -d "$PAYLOAD" \
        "${API_URL}${API_ENDPOINT}" 2>/dev/null)

    HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
    RESPONSE_BODY=$(echo "$RESPONSE" | sed '$d')

    if [[ "$HTTP_CODE" =~ ^(200|201)$ ]]; then
        echo "Published: $TYPE version $SHORT_VERSION"
        PUBLISHED_COUNT=$((PUBLISHED_COUNT + 1))
    elif [[ "$HTTP_CODE" == "409" ]]; then
        echo "Already exists: $TYPE version $SHORT_VERSION (skipping)"
        PUBLISHED_COUNT=$((PUBLISHED_COUNT + 1))
    else
        echo "Error: Failed to publish $TYPE (HTTP $HTTP_CODE)" >&2
        echo "Response: $RESPONSE_BODY" >&2
        exit 1
    fi
done

# Check results
[[ $TOTAL_COUNT -eq 0 ]] && { echo "Error: No binaries found for version $VERSION" >&2; exit 1; }
[[ $PUBLISHED_COUNT -eq $TOTAL_COUNT ]] || { echo "Error: Only $PUBLISHED_COUNT/$TOTAL_COUNT binaries published" >&2; exit 1; }

echo "Successfully published all AvesAID binaries ($PUBLISHED_COUNT/$TOTAL_COUNT)"
