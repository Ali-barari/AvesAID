#!/bin/bash

# upload-flight-controller-binary.sh - Upload flight controller binaries to S3
# Usage: ./upload-flight-controller-binary.sh --file <path> --type <v6c|v6x> --version <version>

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
S3_BUCKET="avestec-prod-update-binaries"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --file) FILE_PATH="$2"; shift 2 ;;
        --type) TYPE="$2"; shift 2 ;;
        --version) VERSION="$2"; shift 2 ;;
        --verbose) shift ;;  # Ignore for compatibility
        *) echo "Error: Unknown option: $1" >&2; exit 1 ;;
    esac
done

# Validate required parameters
[[ -z "${FILE_PATH:-}" ]] && { echo "Error: Missing --file parameter" >&2; exit 1; }
[[ -z "${TYPE:-}" ]] && { echo "Error: Missing --type parameter" >&2; exit 1; }
[[ -z "${VERSION:-}" ]] && { echo "Error: Missing --version parameter" >&2; exit 1; }
[[ -f "$FILE_PATH" ]] || { echo "Error: File not found: $FILE_PATH" >&2; exit 1; }
[[ "$TYPE" =~ ^(v6c|v6x)$ ]] || { echo "Error: Invalid type '$TYPE', must be 'v6c' or 'v6x'" >&2; exit 1; }

# Generate S3 key
SHORT_VERSION="${VERSION#v}"
S3_KEY="flight-controller/v${SHORT_VERSION}/px4_fmu-${TYPE}_default.px4"

# Generate checksum
if command -v sha256sum >/dev/null 2>&1; then
    CHECKSUM=$(sha256sum "$FILE_PATH" | cut -d' ' -f1)
elif command -v shasum >/dev/null 2>&1; then
    CHECKSUM=$(shasum -a 256 "$FILE_PATH" | cut -d' ' -f1)
else
    echo "Error: Neither sha256sum nor shasum found" >&2
    exit 1
fi

# Build metadata
GIT_COMMIT=$(git rev-parse HEAD 2>/dev/null || echo "unknown")
GIT_BRANCH=$(git branch --show-current 2>/dev/null || echo "unknown")
BUILD_DATE=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
FILE_SIZE=$(stat -f%z "$FILE_PATH" 2>/dev/null || stat -c%s "$FILE_PATH" 2>/dev/null || echo "0")
METADATA="git-commit=$GIT_COMMIT,git-branch=$GIT_BRANCH,build-date=$BUILD_DATE,controller-type=$TYPE,version=$VERSION,sha256=$CHECKSUM,file-size=$FILE_SIZE"

# Upload to S3
echo "Uploading $TYPE binary to s3://$S3_BUCKET/$S3_KEY..."
"$SCRIPT_DIR/aws-cross-account.sh" s3 cp "$FILE_PATH" "s3://$S3_BUCKET/$S3_KEY" \
    --content-type "application/octet-stream" \
    --metadata "$METADATA" \
    --metadata-directive REPLACE

# Verify upload
METADATA_JSON=$("$SCRIPT_DIR/aws-cross-account.sh" s3api head-object \
    --bucket "$S3_BUCKET" \
    --key "$S3_KEY" \
    --output json 2>/dev/null)

STORED_CHECKSUM=$(echo "$METADATA_JSON" | jq -r '.Metadata.sha256 // empty')
[[ "$STORED_CHECKSUM" == "$CHECKSUM" ]] || { echo "Error: Checksum verification failed" >&2; exit 1; }

echo "Upload details:"
echo "  S3 Location: s3://$S3_BUCKET/$S3_KEY"
echo "  File Size: $((FILE_SIZE / 1048576))MB"
echo "  SHA256: $CHECKSUM"
echo "  Version: $VERSION"
