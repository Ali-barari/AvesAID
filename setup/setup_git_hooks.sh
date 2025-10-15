#!/bin/bash

# Git Hooks Setup Script
# This script installs the custom git hooks from config/git-hooks to .git/hooks

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Setting up Git Hooks ===${NC}"
echo

# Get the repository root directory
REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null)"

if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Not in a git repository${NC}"
    exit 1
fi

# Define paths
HOOKS_SOURCE_DIR="$REPO_ROOT/config/git-hooks"
HOOKS_TARGET_DIR="$REPO_ROOT/.git/hooks"

# Check if source hooks directory exists
if [ ! -d "$HOOKS_SOURCE_DIR" ]; then
    echo -e "${RED}Error: Git hooks source directory not found: $HOOKS_SOURCE_DIR${NC}"
    exit 1
fi

# Check if .git/hooks directory exists
if [ ! -d "$HOOKS_TARGET_DIR" ]; then
    echo -e "${RED}Error: Git hooks target directory not found: $HOOKS_TARGET_DIR${NC}"
    exit 1
fi

# List of available hooks
AVAILABLE_HOOKS=("pre-commit" "post-commit" "post-checkout")

# Install each hook
for hook in "${AVAILABLE_HOOKS[@]}"; do
    source_file="$HOOKS_SOURCE_DIR/$hook"
    target_file="$HOOKS_TARGET_DIR/$hook"
    
    if [ -f "$source_file" ]; then
        echo -e "${YELLOW}Installing $hook hook...${NC}"
        
        # Backup existing hook if it exists and is not the same
        if [ -f "$target_file" ] && ! cmp -s "$source_file" "$target_file"; then
            echo -e "  Backing up existing $hook hook to $hook.backup"
            cp "$target_file" "$target_file.backup"
        fi
        
        # Copy the hook
        cp "$source_file" "$target_file"
        
        # Make it executable
        chmod +x "$target_file"
        
        echo -e "${GREEN}  ✓ $hook hook installed successfully${NC}"
    else
        echo -e "  • $hook hook not found in source directory (skipping)"
    fi
done

echo
echo -e "${GREEN}=== Git Hooks Setup Complete ===${NC}"
echo
echo "Installed hooks:"
for hook in "${AVAILABLE_HOOKS[@]}"; do
    target_file="$HOOKS_TARGET_DIR/$hook"
    if [ -f "$target_file" ] && [ -x "$target_file" ]; then
        echo -e "${GREEN}  ✓ $hook${NC}"
    else
        echo -e "${RED}  ✗ $hook${NC}"
    fi
done

echo
echo "Note: These hooks will now run automatically when you perform git operations."
echo "To disable a hook temporarily, remove execute permissions or rename the file."
echo