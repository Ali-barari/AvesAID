#!/bin/bash
#
# Exchange this step's Bitbucket OIDC token for an AWS role, and refuse any other
# identity. MUST BE SOURCED - it exports:
#
#   source scripts/deployment/oidc-login.sh avestec-cicd-avesaid
#
# Without the identity check, a step with a stale static key runs green on the
# wrong credential and nothing downstream shows which identity did the work.

EXPECTED="${1:?usage: source oidc-login.sh <expected-role-name>}"

# $HOME, never the clone - the workspace is cached and build/ is an artifact.
export AWS_WEB_IDENTITY_TOKEN_FILE="$HOME/.bb-oidc-token"
( umask 077; printf '%s' "$BITBUCKET_STEP_OIDC_TOKEN" > "$AWS_WEB_IDENTITY_TOKEN_FILE" )
export AWS_ROLE_ARN="arn:aws:iam::${AWS_ACCOUNT_ID}:role/${EXPECTED}"
export AWS_ROLE_SESSION_NAME="bb-${EXPECTED}-${BITBUCKET_BUILD_NUMBER}"
export AWS_REGION="${AWS_REGION:-ca-central-1}"
export AWS_DEFAULT_REGION="$AWS_REGION"

# aws-cross-account.sh re-checks the ARN shape rather than trusting this flag.
export SKIP_ROLE_ASSUMPTION=true

ARN="$(aws sts get-caller-identity --query Arn --output text 2>/dev/null)" || ARN=""
echo "Authenticated as: ${ARN:-<no credentials resolved>}"

# The trailing slash anchors on the whole role name.
case "$ARN" in
    *":assumed-role/${EXPECTED}/"*) echo "OK: credentials belong to ${EXPECTED}" ;;
    *) echo "FATAL: expected an assumed-role session for ${EXPECTED}" >&2; exit 1 ;;
esac
