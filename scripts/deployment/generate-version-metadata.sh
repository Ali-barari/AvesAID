#!/bin/bash

# generate-version-metadata.sh - Extract version information from git repository state
# Usage: ./scripts/deployment/generate-version-metadata.sh [--dry-run] [--verbose] [--output-json] [--help]
# 
# This script extracts version information from git tags following format: v{digit}.{digit}.{digit} (e.g., v1.15.4)
# Falls back to branch + commit hash for untagged builds
# Supports dry-run and verbose modes for testing

set -euo pipefail

# Global variables
DRY_RUN=false
VERBOSE=false
OUTPUT_JSON=false
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Color codes for output
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
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

# Help function
show_help() {
    cat << EOF
Generate Version Metadata Script

USAGE:
    $(basename "$0") [OPTIONS]

OPTIONS:
    --dry-run       Show what would be generated without creating output
    --verbose       Enable verbose output for debugging
    --output-json   Output structured JSON format (default: human-readable)
    --help          Show this help message

DESCRIPTION:
    Extracts version information from git repository state using simplified version format.
    
    Version Detection Priority:
    1. Git tag on current commit (e.g., v1.15.4)
    2. Latest tag + commits ahead (e.g., v1.15.4+5.abc123)
    3. Branch name + short commit (e.g., develop-abc123)

    Expected tag format: v{digit}.{digit}.{digit} (e.g., v1.15.4, v2.0.1)

    Supports environment variable overrides:
    - CUSTOM_RELEASE_NOTES: Override generated release notes
    - OVERRIDE_VERSION: Force specific version string (must follow v{digit}.{digit}.{digit} format)
    - MAX_RELEASE_NOTES_LENGTH: Limit release notes length (default: 1000)

EXAMPLES:
    $(basename "$0") --dry-run --verbose
    $(basename "$0") --output-json
    CUSTOM_RELEASE_NOTES="Critical security update" $(basename "$0") --output-json

EOF
}

# Parse command line arguments
parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --dry-run)
                DRY_RUN=true
                VERBOSE=true  # Enable verbose output in dry-run mode
                shift
                ;;
            --verbose)
                VERBOSE=true
                shift
                ;;
            --output-json)
                OUTPUT_JSON=true
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

# Validate git repository
validate_git_repo() {
    if ! git rev-parse --git-dir > /dev/null 2>&1; then
        log_error "Not in a git repository"
        exit 1
    fi
    
    if ! git rev-parse HEAD > /dev/null 2>&1; then
        log_error "No commits found in repository"
        exit 1
    fi
    
    log_info "Git repository validation passed"
}

# Extract version information from git
extract_version_info() {
    local version=""
    local short_version=""
    local git_commit=""
    local git_branch=""
    local is_tagged_release=false
    local commits_ahead=0
    
    # Get current commit and branch
    git_commit=$(git rev-parse HEAD)
    git_branch=$(git branch --show-current 2>/dev/null || echo "detached")
    
    log_info "Current commit: $git_commit"
    log_info "Current branch: $git_branch"
    
    # Check for tag on current commit
    local current_tag
    current_tag=$(git tag --points-at HEAD 2>/dev/null | grep -E '^v[0-9]+\.[0-9]+\.[0-9]+$' | head -n1 || true)
    
    if [[ -n "$current_tag" ]]; then
        # Tagged release - use tag as version
        log_info "Found tag on current commit: $current_tag"
        is_tagged_release=true
        commits_ahead=0
        version="$current_tag"
        short_version="${current_tag#v}"  # Remove 'v' prefix for short version
    else
        # No tag on current commit - check for latest tag
        local latest_tag
        latest_tag=$(git describe --tags --abbrev=0 2>/dev/null | grep -E '^v[0-9]+\.[0-9]+\.[0-9]+$' | head -n1 || true)
        
        if [[ -n "$latest_tag" ]]; then
            # Found latest tag - calculate commits ahead
            commits_ahead=$(git rev-list "$latest_tag"..HEAD --count 2>/dev/null || echo "0")
            log_info "Latest tag: $latest_tag, commits ahead: $commits_ahead"
            
            if [[ $commits_ahead -gt 0 ]]; then
                local short_commit=$(git rev-parse --short HEAD)
                version="$latest_tag+$commits_ahead.$short_commit"
                short_version="${latest_tag#v}-dev"  # Remove 'v' prefix and add -dev suffix
            else
                version="$latest_tag"
                short_version="${latest_tag#v}"
                is_tagged_release=true
            fi
        else
            # No tags found - use branch + commit
            log_info "No suitable tags found, using branch + commit"
            local short_commit=$(git rev-parse --short HEAD)
            version="$git_branch-$short_commit"
            short_version="0.0.0-dev"
        fi
    fi
    
    # Environment variable overrides
    if [[ -n "${OVERRIDE_VERSION:-}" ]]; then
        log_info "Overriding version with environment variable: $OVERRIDE_VERSION"
        version="$OVERRIDE_VERSION"
        # Parse overridden version - ensure it follows v{digit}.{digit}.{digit} format
        if [[ "$version" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
            short_version="${version#v}"
        else
            log_warn "Override version doesn't match expected format v{digit}.{digit}.{digit}"
            short_version="$version"
        fi
    fi
    
    # Export variables for use in other functions
    export VERSION="$version"
    export SHORT_VERSION="$short_version"
    export GIT_COMMIT="$git_commit"
    export GIT_BRANCH="$git_branch"
    export IS_TAGGED_RELEASE="$is_tagged_release"
    export COMMITS_AHEAD="$commits_ahead"
    
    log_success "Version extraction completed: $version"
}

# Generate release notes from git history
generate_release_notes() {
    local release_notes=""
    local max_length="${MAX_RELEASE_NOTES_LENGTH:-1000}"
    
    # Use custom release notes if provided
    if [[ -n "${CUSTOM_RELEASE_NOTES:-}" ]]; then
        release_notes="$CUSTOM_RELEASE_NOTES"
        log_info "Using custom release notes from environment variable"
    else
        log_info "Generating release notes from git history"
        
        # Determine commit range for release notes
        local commit_range="HEAD~10..HEAD"  # Default: last 10 commits
        
        if [[ "$IS_TAGGED_RELEASE" == "true" ]]; then
            # For tagged releases, use commits since previous tag
            local previous_tag
            previous_tag=$(git describe --tags --abbrev=0 HEAD^ 2>/dev/null || echo "")
            if [[ -n "$previous_tag" ]]; then
                commit_range="$previous_tag..HEAD"
                log_info "Using commit range for tagged release: $commit_range"
            fi
        elif [[ "$COMMITS_AHEAD" -gt 0 ]]; then
            # For development builds, use commits since last tag
            local latest_tag
            latest_tag=$(git describe --tags --abbrev=0 2>/dev/null || echo "")
            if [[ -n "$latest_tag" ]]; then
                commit_range="$latest_tag..HEAD"
                log_info "Using commit range for development build: $commit_range"
            fi
        fi
        
        # Extract commit messages and categorize
        local feat_commits=""
        local fix_commits=""
        local other_commits=""
        
        while IFS= read -r commit; do
            if [[ -z "$commit" ]]; then continue; fi
            
            # Skip merge commits
            if [[ "$commit" == *"Merge"* ]] || [[ "$commit" == *"merge"* ]]; then
                continue
            fi
            
            # Categorize commits
            if [[ "$commit" == *"feat:"* ]] || [[ "$commit" == *"feature:"* ]] || [[ "$commit" == *"add:"* ]]; then
                feat_commits="${feat_commits}• $commit\n"
            elif [[ "$commit" == *"fix:"* ]] || [[ "$commit" == *"bug:"* ]] || [[ "$commit" == *"Fix"* ]]; then
                fix_commits="${fix_commits}• $commit\n"
            else
                other_commits="${other_commits}• $commit\n"
            fi
        done < <(git log --format="%s" "$commit_range" 2>/dev/null || true)
        
        # Build release notes
        if [[ -n "$feat_commits" ]]; then
            release_notes="${release_notes}**New Features:**\n$feat_commits\n"
        fi
        
        if [[ -n "$fix_commits" ]]; then
            release_notes="${release_notes}**Bug Fixes:**\n$fix_commits\n"
        fi
        
        if [[ -n "$other_commits" ]]; then
            release_notes="${release_notes}**Other Changes:**\n$other_commits\n"
        fi
        
        # Add contributor information if available
        local contributors
        contributors=$(git log --format="%aN" "$commit_range" 2>/dev/null | sort -u | tr '\n' ',' | sed 's/,$//' || true)
        if [[ -n "$contributors" ]]; then
            release_notes="${release_notes}**Contributors:** $contributors\n"
        fi
        
        # Default release notes if none generated
        if [[ -z "$release_notes" ]]; then
            release_notes="Updates and improvements"
        fi
    fi
    
    # Truncate to maximum length
    if [[ ${#release_notes} -gt $max_length ]]; then
        release_notes="${release_notes:0:$max_length}..."
        log_warn "Release notes truncated to $max_length characters"
    fi
    
    # Export for use in output
    export RELEASE_NOTES="$release_notes"
    
    log_success "Release notes generated (${#release_notes} characters)"
}

# Output version metadata
output_metadata() {
    local build_date
    build_date=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
    
    if [[ "$OUTPUT_JSON" == true ]]; then
        # JSON output for pipeline consumption
        cat << EOF
{
  "version": "$VERSION",
  "shortVersion": "$SHORT_VERSION",
  "buildDate": "$build_date",
  "gitCommit": "$GIT_COMMIT",
  "gitBranch": "$GIT_BRANCH",
  "releaseNotes": "$(echo -e "$RELEASE_NOTES" | sed 's/"/\\"/g' | tr '\n' ' ')",
  "isTaggedRelease": $IS_TAGGED_RELEASE,
  "commitsAhead": $COMMITS_AHEAD
}
EOF
    else
        # Human-readable output
        echo "AvesAID Version Metadata"
        echo "========================"
        echo "Version: $VERSION"
        echo "Short Version: $SHORT_VERSION"
        echo "Build Date: $build_date"
        echo "Git Commit: $GIT_COMMIT"
        echo "Git Branch: $GIT_BRANCH"
        echo "Tagged Release: $IS_TAGGED_RELEASE"
        echo "Commits Ahead: $COMMITS_AHEAD"
        echo ""
        echo "Release Notes:"
        echo -e "$RELEASE_NOTES"
    fi
}

# Main function
main() {
    log_info "Starting version metadata generation..."
    
    if [[ "$DRY_RUN" == true ]]; then
        log_info "🧪 DRY-RUN MODE: No changes will be made"
    fi
    
    # Parse arguments
    parse_args "$@"
    
    # Validate environment
    validate_git_repo
    
    # Extract version information
    extract_version_info
    
    # Generate release notes
    generate_release_notes
    
    # Output metadata
    if [[ "$DRY_RUN" == true ]]; then
        log_info "🧪 DRY-RUN: Would generate the following metadata:"
        echo "----------------------------------------"
    fi
    
    output_metadata
    
    if [[ "$DRY_RUN" == true ]]; then
        echo "----------------------------------------"
        log_success "🧪 DRY-RUN: Version metadata generation completed successfully"
    else
        log_success "Version metadata generation completed successfully"
    fi
}

# Execute main function with all arguments
main "$@"