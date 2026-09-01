#!/bin/bash
# mark-faulty-version.sh - Mark released firmware version as faulty
# Triggered by CI/CD when faulty/* tag is pushed to main branch

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
S3_BUCKET="avestec-prod-update-binaries"

# Validate required environment variables
: "${BITBUCKET_TAG:?Error: BITBUCKET_TAG not set}"
: "${AWS_REGION:?Error: AWS_REGION not set}"
if [[ "${SKIP_ROLE_ASSUMPTION:-false}" != "true" ]]; then
    : "${CROSS_ACCOUNT_ROLE_ARN:?Error: CROSS_ACCOUNT_ROLE_ARN not set}"
    : "${CROSS_ACCOUNT_EXTERNAL_ID:?Error: CROSS_ACCOUNT_EXTERNAL_ID not set}"
fi

# Extract version from tag: "faulty/v1.5.4-1.3.5" → "1.5.4-1.3.5"
# Supports both simple (1.5.3) and compound (1.5.4-1.3.5) version formats
if [[ ! "$BITBUCKET_TAG" =~ ^faulty/v([0-9]+\.[0-9]+\.[0-9]+(-[0-9]+\.[0-9]+\.[0-9]+)?)$ ]]; then
    echo "Error: Invalid tag format '$BITBUCKET_TAG'" >&2
    echo "Expected format: faulty/v{version} (e.g., faulty/v1.5.4-1.3.5 or faulty/v1.5.3)" >&2
    exit 1
fi

VERSION="${BASH_REMATCH[1]}"
# Extract semantic version: 1.15.4-1.5.9 → 1.5.9
VERSION=$(echo "$VERSION" | sed 's/^[^-]*-//')
echo "Processing faulty tag for version: $VERSION"

# Verify at least one binary exists in S3
echo "Validating version $VERSION exists in S3..."

V6C_EXISTS=false
V6X_EXISTS=false

if "$SCRIPT_DIR/aws-cross-account.sh" s3api head-object \
    --bucket "$S3_BUCKET" \
    --key "flight-controller/v${VERSION}/px4_fmu-v6c_default.px4" \
    --region "$AWS_REGION" >/dev/null 2>&1; then
    V6C_EXISTS=true
    echo "  ✓ Found v6c binary"
fi

if "$SCRIPT_DIR/aws-cross-account.sh" s3api head-object \
    --bucket "$S3_BUCKET" \
    --key "flight-controller/v${VERSION}/px4_fmu-v6x_default.px4" \
    --region "$AWS_REGION" >/dev/null 2>&1; then
    V6X_EXISTS=true
    echo "  ✓ Found v6x binary"
fi

if [[ "$V6C_EXISTS" == false ]] && [[ "$V6X_EXISTS" == false ]]; then
    echo "Error: Version $VERSION not found in S3 bucket $S3_BUCKET" >&2
    echo "No binaries found for v6c or v6x controller types" >&2
    exit 1
fi

# Extract git tag annotation message (first line only)
REASON=$(git tag -l --format='%(contents)' "$BITBUCKET_TAG" 2>/dev/null | head -1 | tr -d '\n' || echo "")

# Default reason if no annotation provided
if [[ -z "$REASON" ]]; then
    REASON="Marked faulty via CI/CD pipeline"
fi

# Truncate reason to 500 characters (DynamoDB string limit consideration)
if [ ${#REASON} -gt 500 ]; then
    REASON="${REASON:0:497}..."
fi

echo "Faulty reason: $REASON"

# Update DynamoDB table (idempotent - creates or updates attributes)
TABLE_NAME="avestec-${ENVIRONMENT:-prod}-update-system"
TIMESTAMP=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

echo "Updating DynamoDB table: $TABLE_NAME"
echo "  PK: COMPONENT#flight"
echo "  SK: VERSION#${VERSION}"

# Validate DynamoDB item exists before updating
echo "Validating version record exists in DynamoDB..."
if ! "$SCRIPT_DIR/aws-cross-account.sh" dynamodb get-item \
    --table-name "$TABLE_NAME" \
    --key "{\"PK\": {\"S\": \"COMPONENT#flight\"}, \"SK\": {\"S\": \"VERSION#${VERSION}\"}}" \
    --region "$AWS_REGION" \
    --output json 2>/dev/null | grep -q "\"Item\""; then
    echo "Error: Version $VERSION not found in DynamoDB table $TABLE_NAME" >&2
    echo "Version must be published via API before it can be marked faulty" >&2
    exit 1
fi
echo "  ✓ Version record found"

# Properly escape reason for JSON using jq
REASON_ESCAPED=$(printf '%s' "$REASON" | jq -Rs . | sed 's/^"//; s/"$//')

"$SCRIPT_DIR/aws-cross-account.sh" dynamodb update-item \
    --table-name "$TABLE_NAME" \
    --key "{\"PK\": {\"S\": \"COMPONENT#flight\"}, \"SK\": {\"S\": \"VERSION#${VERSION}\"}}" \
    --update-expression "SET healthy = :false, faultyReason = :reason, markedFaultyAt = :timestamp" \
    --expression-attribute-values "{\":false\": {\"BOOL\": false}, \":reason\": {\"S\": \"${REASON_ESCAPED}\"}, \":timestamp\": {\"S\": \"${TIMESTAMP}\"}}" \
    --region "$AWS_REGION" \
    --output json >/dev/null

echo "✓ Successfully marked version $VERSION as faulty"
echo "  Timestamp: $TIMESTAMP"
echo "  Reason: $REASON"

echo ""
echo "=========================================="
echo "Version $VERSION marked as FAULTY"
echo "=========================================="
echo "Binaries affected:"
[[ "$V6C_EXISTS" == true ]] && echo "  - px4_fmu-v6c_default.px4"
[[ "$V6X_EXISTS" == true ]] && echo "  - px4_fmu-v6x_default.px4"
echo ""
echo "Remote update system will now filter out this version"
