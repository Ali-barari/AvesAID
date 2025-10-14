#!/bin/bash

# create-release.sh - Extract highest version tag from main for publishing
# Usage: ./scripts/deployment/create-release.sh [--dry-run] [--verbose]

set -euo pipefail

DRY_RUN=false
VERBOSE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --dry-run) DRY_RUN=true; VERBOSE=true; shift ;;
        --verbose) VERBOSE=true; shift ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

log() { [[ "$VERBOSE" == true ]] && echo "[INFO] $1" >&2; }

# Validate main branch
BRANCH=$(git branch --show-current)
if [[ "$BRANCH" != "main" ]]; then
    echo "[ERROR] Must run on main branch (currently on: $BRANCH)" >&2
    exit 1
fi
log "Running on main branch"

# Find AvesAID tags: v1.15.4-1.5.1 format
FULL_TAG=$(git tag --merged main --list "v*.*.*-*.*.*" 2>/dev/null | grep -E '^v[0-9]+\.[0-9]+\.[0-9]+-[0-9]+\.[0-9]+\.[0-9]+$' | sort -V | tail -1 || true)

if [[ -z "$FULL_TAG" ]]; then
    echo "[ERROR] No AvesAID version tags found on main branch" >&2
    echo "[ERROR] Expected format: v1.15.4-1.5.1" >&2
    exit 1
fi

# Extract semantic version: v1.15.4-1.5.1 → v1.5.1
HIGHEST_VERSION=$(echo "$FULL_TAG" | sed 's/^v[^-]*-/v/')
log "Found tag: $FULL_TAG, extracted version: $HIGHEST_VERSION"

# Generate release notes from recent commits (truncate to 1000 chars for API Gateway limit)
RELEASE_NOTES=$(git log --format="• %s" HEAD~10..HEAD 2>/dev/null | grep -v "^• Merged in" | head -20 | tr '\n' ' ' | sed 's/  */ /g' | sed 's/^ *• *//' || echo "Updates and improvements")

# Truncate release notes to 1000 characters (API Gateway limit)
if [ ${#RELEASE_NOTES} -gt 1000 ]; then
    RELEASE_NOTES="${RELEASE_NOTES:0:997}..."
    log "Release notes truncated to 1000 characters"
fi

if [[ "$DRY_RUN" == true ]]; then
    echo "[DRY-RUN] Would publish version: $HIGHEST_VERSION" >&2
    echo "[DRY-RUN] Release notes: $RELEASE_NOTES" >&2
fi

# Output version metadata (matches generate-version-metadata.sh format)
cat << EOF
{
  "version": "$HIGHEST_VERSION",
  "shortVersion": "${HIGHEST_VERSION#v}",
  "buildDate": "$(date -u +"%Y-%m-%dT%H:%M:%SZ")",
  "gitCommit": "$(git rev-parse HEAD)",
  "gitBranch": "$BRANCH",
  "releaseNotes": "$(echo "$RELEASE_NOTES" | sed 's/"/\\"/g')",
  "isTaggedRelease": true,
  "commitsAhead": 0
}
EOF
