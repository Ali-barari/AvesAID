# Deployment Scripts for AvesAID Remote Update System

This directory contains deployment scripts for the AvesAID Remote Update System, providing automated firmware deployment capabilities for PX4 flight controller binaries.

## Scripts Overview

### Core Deployment Scripts

- **`generate-version-metadata.sh`**: Extract version information from git tags/commits
  - Supports tagged releases (e.g., `v1.15.4-1.0.0`) and development builds
  - Generates comprehensive metadata including release notes
  - Outputs structured JSON for pipeline consumption

- **`upload-flight-controller-binary.sh`**: Upload flight controller binaries to S3 with checksums
  - Supports v6c and v6x flight controller types
  - Generates SHA256 checksums for integrity validation
  - Includes comprehensive error handling and retry logic

- **`publish-firmware-version.sh`**: Publish firmware versions via Remote Update API
  - Integrates with deployed API Gateway endpoints
  - Supports batch publishing of multiple binary types
  - Includes rollback and version conflict handling

- **`aws-cross-account.sh`**: AWS cross-account authentication helper
  - Handles cross-account role assumption for AWS operations
  - Provides secure access to target AWS resources
  - Used by other scripts for AWS API calls

## Environment Variables Required

### AWS Authentication (Required)
```bash
AWS_ACCESS_KEY_ID=<management-account-key>        # Management account access key
AWS_SECRET_ACCESS_KEY=<management-account-secret>  # Management account secret key
```

### Cross-Account Configuration (Required)
```bash
AWS_ACCOUNT_ID=241856856579                        # Target AWS account ID
AWS_REGION=ca-central-1                           # AWS region
CROSS_ACCOUNT_ROLE_ARN=arn:aws:iam::241856856579:role/BinshopsAWSImplementationRole
CROSS_ACCOUNT_EXTERNAL_ID=AVESTEC-2025-BINSHOPS   # External ID for role assumption
CROSS_ACCOUNT_SESSION_NAME=AvestecCDKDeployment   # Session name for role assumption
```

### API Configuration (Required)
```bash
UPDATE_API_URL=https://a5tk7g3y3j.execute-api.ca-central-1.amazonaws.com/dev  # API Gateway base URL
UPDATE_API_KEY=<retrieved-from-aws>               # API key for authentication (secured)
```

### Optional Configuration
```bash
API_TIMEOUT=30                                    # API request timeout in seconds (default: 30)
MAX_RETRIES=3                                     # Maximum retry attempts (default: 3)
```

## Usage Examples

### Local Development and Testing

#### 1. Generate Version Metadata
```bash
# Test version extraction (dry-run)
./scripts/deployment/generate-version-metadata.sh --dry-run --verbose

# Generate JSON output for pipeline use
./scripts/deployment/generate-version-metadata.sh --output-json > version.json

# Test with custom release notes
CUSTOM_RELEASE_NOTES="Critical security update" ./scripts/deployment/generate-version-metadata.sh
```

#### 2. Upload Flight Controller Binary
```bash
# Test upload (dry-run mode)
./scripts/deployment/upload-flight-controller-binary.sh \
  --file build/px4_fmu-v6c_default.px4 \
  --type v6c \
  --version 1.15.4-1.0.0 \
  --dry-run

# Actual upload with verbose output
./scripts/deployment/upload-flight-controller-binary.sh \
  --file build/px4_fmu-v6c_default.px4 \
  --type v6c \
  --version 1.15.4-1.0.0 \
  --verbose
```

#### 3. Publish Firmware Version
```bash
# Test API publishing (dry-run)
./scripts/deployment/publish-firmware-version.sh \
  --version 1.15.4-1.0.0 \
  --dry-run

# Publish to production with confirmation
./scripts/deployment/publish-firmware-version.sh \
  --version 1.15.4-1.0.0 \
  --verbose
```

### CI/CD Pipeline Usage

#### Complete Deployment Workflow
```bash
#!/bin/bash
# Example: Complete automated deployment

set -euo pipefail

echo "🚀 Starting automated firmware deployment..."

# 1. Generate version metadata
VERSION_INFO=$(./scripts/deployment/generate-version-metadata.sh --output-json)
VERSION=$(echo $VERSION_INFO | jq -r '.version')

echo "📦 Deploying firmware version: $VERSION"

# 2. Upload v6c binary
./scripts/deployment/upload-flight-controller-binary.sh \
  --file build/px4_fmu-v6c_default.px4 \
  --type v6c \
  --version $VERSION \
  --verbose

# 3. Upload v6x binary  
./scripts/deployment/upload-flight-controller-binary.sh \
  --file build/px4_fmu-v6x_default.px4 \
  --type v6x \
  --version $VERSION \
  --verbose

# 4. Publish version via API
./scripts/deployment/publish-firmware-version.sh \
  --version $VERSION \
  --verbose

echo "✅ Successfully deployed firmware version $VERSION"
```

## Dry-Run Testing Procedures

### Pre-Deployment Validation
All scripts support `--dry-run` mode for safe testing:

1. **Always test dry-run first**: Validate parameters and configuration without making changes
2. **Check AWS connectivity**: Ensure cross-account authentication works
3. **Validate file integrity**: Verify binary files exist and are readable
4. **Test API connectivity**: Confirm API endpoints are accessible with proper authentication

### Example Dry-Run Workflow
```bash
# Complete dry-run test of deployment workflow
echo "🧪 Testing deployment workflow (dry-run)..."

# Test version generation
./scripts/deployment/generate-version-metadata.sh --dry-run

# Test S3 uploads
./scripts/deployment/upload-flight-controller-binary.sh \
  --file test-binary.px4 --type v6c --version test-1.0.0 --dry-run

# Test API publishing  
./scripts/deployment/publish-firmware-version.sh \
  --version test-1.0.0 --dry-run

echo "✅ Dry-run tests completed successfully"
```

## Security Considerations

### API Key Management
- **Never commit API keys**: Use environment variables or CI/CD secrets
- **Rotate keys regularly**: Update API keys according to security policy
- **Limit key permissions**: Ensure API keys have minimal required permissions
- **Secure transmission**: Always use HTTPS for API communications

### Cross-Account Access
- **Role assumption**: Use temporary credentials via cross-account roles
- **External ID validation**: Ensure external ID matches expected value
- **Session naming**: Use descriptive session names for audit trails
- **Permission boundaries**: Limit cross-account role permissions to minimum required

### Binary Integrity
- **Checksum validation**: SHA256 checksums generated and validated for all uploads
- **Secure storage**: Binaries stored in private S3 buckets with encryption
- **Access logging**: All S3 and API access is logged for audit purposes

## Troubleshooting

### Common Issues

#### AWS Authentication Failures
```bash
# Verify AWS credentials
aws sts get-caller-identity

# Test cross-account role assumption
./scripts/deployment/aws-cross-account.sh sts get-caller-identity

# Check role permissions
./scripts/deployment/aws-cross-account.sh iam get-role --role-name BinshopsAWSImplementationRole
```

#### S3 Upload Failures
- **Check bucket permissions**: Ensure write access to target bucket
- **Verify file paths**: Confirm binary files exist and are readable
- **Network connectivity**: Test S3 access from deployment environment
- **File size limits**: Ensure binaries are within S3 upload limits

#### API Publishing Issues
- **Validate API key**: Test API connectivity with curl
- **Check endpoint status**: Verify API Gateway is deployed and accessible
- **Review request format**: Ensure JSON payload matches API specification
- **Monitor rate limits**: API may have rate limiting in place

#### Version Conflicts
- **Duplicate versions**: API will reject attempts to publish existing versions
- **Format validation**: Ensure version format matches expected pattern
- **Git tag consistency**: Verify git tags follow AvesAID versioning convention

### Debug Mode
Enable verbose output in all scripts using `--verbose` flag for detailed debugging information:

```bash
# Example: Debug S3 upload issues
./scripts/deployment/upload-flight-controller-binary.sh \
  --file problematic-binary.px4 \
  --type v6c \
  --version debug-1.0.0 \
  --verbose \
  --dry-run
```

### Getting Help
For additional support:
1. Check script output and error messages
2. Review AWS CloudTrail logs for API calls
3. Examine S3 access logs for upload issues
4. Verify Bitbucket pipeline logs for CI/CD problems

## Maintenance

### Regular Tasks
- **API key rotation**: Update API keys quarterly or as required
- **Permission audits**: Review and validate AWS permissions regularly
- **Performance monitoring**: Monitor upload times and API response times
- **Log cleanup**: Maintain reasonable log retention policies

### Updates and Changes
- **Script modifications**: Test all changes with dry-run mode first
- **Environment updates**: Coordinate changes with infrastructure team
- **Version format changes**: Update all scripts consistently if version format changes
- **Documentation updates**: Keep this README current with any script changes