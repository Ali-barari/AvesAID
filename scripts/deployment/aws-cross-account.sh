#!/bin/bash

# AWS CLI Cross-Account Helper Script
# Usage: ./scripts/deployment/aws-cross-account.sh [aws-cli-commands]
# Example: ./scripts/deployment/aws-cross-account.sh lambda list-functions

set -e

# Load environment variables from .env file
if [ -f .env ]; then
    export $(cat .env | grep -v '^#' | xargs)
fi

# Validate required environment variables
if [ -z "$CROSS_ACCOUNT_ROLE_ARN" ] || [ -z "$CROSS_ACCOUNT_EXTERNAL_ID" ] || [ -z "$AWS_ACCOUNT_ID" ]; then
    echo "Error: Required environment variables not set. Check your .env file."
    exit 1
fi

# Check if we're already in the target account
CURRENT_ACCOUNT=$(aws sts get-caller-identity --query 'Account' --output text 2>/dev/null)
CURRENT_ARN=$(aws sts get-caller-identity --query 'Arn' --output text 2>/dev/null)

if [ "$CURRENT_ACCOUNT" = "$AWS_ACCOUNT_ID" ] && [[ "$CURRENT_ARN" == *"BinshopsAWSImplementationRole"* ]]; then
    echo "Already using cross-account role in target account: $CURRENT_ACCOUNT" >&2
else
    # Assume cross-account role
    echo "Assuming cross-account role..." >&2
    TEMP_CREDS=$(aws sts assume-role \
        --role-arn "$CROSS_ACCOUNT_ROLE_ARN" \
        --external-id "$CROSS_ACCOUNT_EXTERNAL_ID" \
        --role-session-name "$CROSS_ACCOUNT_SESSION_NAME" \
        --duration-seconds 3600 \
        --output json)

    if [ $? -ne 0 ]; then
        echo "Error: Failed to assume cross-account role" >&2
        exit 1
    fi

    # Export temporary credentials
    export AWS_ACCESS_KEY_ID=$(echo $TEMP_CREDS | jq -r '.Credentials.AccessKeyId')
    export AWS_SECRET_ACCESS_KEY=$(echo $TEMP_CREDS | jq -r '.Credentials.SecretAccessKey')
    export AWS_SESSION_TOKEN=$(echo $TEMP_CREDS | jq -r '.Credentials.SessionToken')
fi

# Verify account access
ACCOUNT_ID=$(aws sts get-caller-identity --query 'Account' --output text)
if [ "$ACCOUNT_ID" != "$AWS_ACCOUNT_ID" ]; then
    echo "Error: Connected to wrong account: $ACCOUNT_ID" >&2
    exit 1
fi

echo "Connected to account: $ACCOUNT_ID" >&2

# Execute the AWS CLI command passed as arguments
aws "$@"