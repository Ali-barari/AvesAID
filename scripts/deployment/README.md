# Deployment Scripts for AvesAID Remote Update System

Automated AvesAID deployment pipeline for PX4 flight controller binaries with S3 storage and API publishing.

## Core Scripts

- **`generate-version-metadata.sh`**: Extract version info from git tags (supports `v1.15.4-1.2.3` format)
- **`upload-flight-controller-binary.sh`**: Upload binaries to S3 with SHA256 validation
- **`publish-avesaid-version.sh`**: Publish versions via API (supports PX4-AvesAID version format)
- **`mark-faulty-version.sh`**: Mark released versions as faulty to prevent distribution to devices
- **`aws-cross-account.sh`**: Handle cross-account AWS authentication

## Environment Variables

### Required AWS Configuration
```bash
# Management Account Credentials
AWS_ACCESS_KEY_ID=<your-access-key>
AWS_SECRET_ACCESS_KEY=<your-secret-key>

# Cross-Account Setup
AWS_ACCOUNT_ID=<target-account-id>
AWS_REGION=<aws-region>
CROSS_ACCOUNT_ROLE_ARN=arn:aws:iam::<account-id>:role/<role-name>
CROSS_ACCOUNT_EXTERNAL_ID=<external-id>
CROSS_ACCOUNT_SESSION_NAME=<session-name>

# API Configuration
UPDATE_API_URL=https://<api-id>.execute-api.<region>.amazonaws.com/<stage>
UPDATE_API_KEY=<api-key>
```

### Optional Configuration
```bash
API_TIMEOUT=30        # Request timeout (default: 30s)
MAX_RETRIES=3         # Retry attempts (default: 3)
```

## Quick Start

### Complete Pipeline Test (Dry-Run)
```bash
# Test full deployment workflow without making changes
./scripts/deployment/generate-version-metadata.sh --dry-run
./scripts/deployment/upload-flight-controller-binary.sh --file test.px4 --type v6c --version v1.15.4-1.2.3 --dry-run
./scripts/deployment/publish-avesaid-version.sh --version v1.15.4-1.2.3 --dry-run
```

### Production Deployment
```bash
# 1. Get current version
VERSION=$(./scripts/deployment/generate-version-metadata.sh --output-json | jq -r '.version')

# 2. Upload binaries
./scripts/deployment/upload-flight-controller-binary.sh --file build/px4_fmu-v6c_default.px4 --type v6c --version $VERSION
./scripts/deployment/upload-flight-controller-binary.sh --file build/px4_fmu-v6x_default.px4 --type v6x --version $VERSION

# 3. Publish via API
./scripts/deployment/publish-avesaid-version.sh --version $VERSION
```

## Key Features

- **Full PX4-AvesAID Version Support**: API now supports `v1.15.4-1.2.3` format (preserves both PX4 and AvesAID version info)
- **Individual Binary Publishing**: Sends separate API requests per binary type (v6c, v6x)
- **Comprehensive Error Handling**: HTTP 409 (version exists), HTTP 400 validation, retry logic
- **Dry-Run Testing**: All scripts support `--dry-run` for safe testing
- **Cross-Account AWS**: Secure role assumption for AWS operations
- **Binary Integrity**: SHA256 checksums for all uploads

## Troubleshooting

### Quick Diagnostics
```bash
# Test AWS connectivity
./scripts/deployment/aws-cross-account.sh sts get-caller-identity

# Test API with curl
curl -X POST "$UPDATE_API_URL/v1/components/flightController/publish" \
  -H "x-api-key: $UPDATE_API_KEY" \
  -H "Content-Type: application/json" \
  -d '{"version": "1.15.4-1.2.3", "s3Key": "test", "sha256": "test", "size": 1000, "releaseNotes": "test", "mandatory": false, "rolloutPercentage": 100}'

# Debug script issues
<script-name> --verbose --dry-run
```

### Common Issues
- **HTTP 409**: Version already exists (expected for duplicate versions)
- **HTTP 400**: Invalid payload format or version format
- **AWS auth errors**: Check cross-account role configuration
- **S3 upload failures**: Verify bucket permissions and file paths

---

## Marking Released Versions as Faulty

If a released firmware version is discovered to have critical bugs, mark it as faulty to prevent distribution to devices.

### Quick Start

1. **Identify the faulty version** (must already be released and in S3)
   - Example: v1.15.4-1.5.3

2. **Create annotated git tag with reason**:
   ```bash
   git tag -a faulty/v1.15.4-1.5.3 -m "Critical boot failure on v6c hardware rev 2"
   ```

3. **Push tag to trigger pipeline**:
   ```bash
   git push origin faulty/v1.15.4-1.5.3
   ```

4. **Monitor Bitbucket pipeline** - Verify "Mark Version as Faulty" step succeeds

### Tag Naming Rules
- **Format**: `faulty/v{version}` (e.g., `faulty/v1.15.4-1.5.3`)
- **Version**: Must match existing release compound format (e.g., `1.15.4-1.5.3`)
- **Annotation**: Always include reason via `-a -m "reason"`

### What Happens
- ✓ Both v6c and v6x binaries marked faulty together
- ✓ DynamoDB updated: `healthy=false`, `faultyReason`, `markedFaultyAt`
- ✓ Remote update system automatically filters out faulty versions
- ✓ Devices will NOT receive faulty version during update checks
