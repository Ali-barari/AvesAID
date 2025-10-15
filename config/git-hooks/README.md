# AvesAID Git Hooks

This directory contains Git hooks that are automatically installed by the `setup/setup_git_hooks.sh` script.

## Available Hooks

### post-commit
- **Purpose**: Automatic AvesAID versioning system
- **Behavior**: Creates version tags in format `v{upstream_version}-{avesaid_major}.{avesaid_minor}.{avesaid_patch}`
- **Branch Exclusions**: Skips auto-versioning on `main` branch (reserved for stable releases)
- **Git Configuration**: Automatically configures:
  - `merge.ff only` - Forces fast-forward merges to prevent unwanted merge commits
  - `push.followTags true` - Automatically pushes tags with commits
  - `remote.origin.tagopt --tags` - Automatically fetches all tags

### pre-commit
- **Purpose**: Pre-commit validation and checks
- **Behavior**: Runs before each commit to validate code quality

### post-checkout
- **Purpose**: Post-checkout operations
- **Behavior**: Runs after branch checkout operations

## Installation

To install all hooks:

```bash
./setup/setup_git_hooks.sh
```

This will:
1. Copy all hooks from `config/git-hooks/` to `.git/hooks/`
2. Make them executable
3. Backup any existing hooks
4. Show installation status

## Version Tagging System

The post-commit hook implements automatic versioning:

### Tag Format
`v{upstream_version}-{avesaid_major}.{avesaid_minor}.{avesaid_patch}`

Examples:
- `v1.15.4-1.2.3` - Based on PX4 v1.15.4, AvesAID version 1.2.3
- `v1.15.4-2.0.0` - Major version increment

### Version Increment Rules
- `[major]` or `BREAKING CHANGE` → increment major, reset minor/patch to 0
- `[minor]` or `feat:` → increment minor, reset patch to 0
- Default → increment patch

### Branch Behavior
- **develop**: Auto-versioning enabled
- **feature branches**: Auto-versioning enabled
- **main**: Auto-versioning disabled (manual tagging only)

## Git Workflow Configuration

The hooks automatically configure Git to prevent merge commit issues:

### Fast-Forward Only Merges
```bash
git config merge.ff only
```
- Prevents unwanted merge commits
- Forces proper conflict resolution via rebase

### Automatic Tag Handling
```bash
git config push.followTags true
git config remote.origin.tagopt --tags
```
- Tags are automatically pushed/fetched with commits
- Ensures version tags stay synchronized

## Troubleshooting

### "Not possible to fast-forward, aborting"
This is expected behavior when branches have diverged. Resolve with:

```bash
# Option 1: Rebase develop onto main
git checkout develop
git rebase main
git checkout main
git merge develop  # Should fast-forward cleanly

# Option 2: Reset main to match remote
git checkout main
git reset --hard origin/main
git merge develop  # Should fast-forward cleanly
```

### Disable Hooks Temporarily
```bash
# Remove execute permission
chmod -x .git/hooks/post-commit

# Or rename the hook
mv .git/hooks/post-commit .git/hooks/post-commit.disabled
```

## Team Setup

For new team members or fresh clones:

1. Clone the repository
2. Run the setup script: `./setup/setup_git_hooks.sh`
3. Make first commit to trigger hook configuration

The hooks will ensure consistent Git behavior across all team members.
