# AvesAID Versioning Test

This file is used to test the automatic AvesAID versioning system.

## Expected Behavior

When commits are made to the `develop` branch, the post-commit hook should automatically:
```
git config --global push.followTags true
git config --get push.followTags
```
1. Find the current upstream version tag that the code is based on (e.g., `v1.15.4`)
2. Find the latest AvesAID version for that upstream base (e.g., `v1.15.4-1.2.3`)
3. Increment the AvesAID version based on commit message:
   - `[major]` or `BREAKING CHANGE` → increment major, reset minor/patch to 0
   - `[minor]` or `feat:` → increment minor, reset patch to 0
   - Default → increment patch
4. Create new tag (e.g., `v1.15.4-1.2.4`)

## Test Commit

This commit should trigger automatic versioning and create v1.15.4-1.2.4


# AvesAID Auto-Versioning Update

The post-commit hook has been updated to auto-version on all branches except 'main'.
This ensures main branch stays stable for releases while all development branches get automatic versioning.

Updated: Tue 16 Sep 2025 04:05:25 PM PDT

## Git Workflow Best Practices

### To Prevent Unnecessary Merge Commits:

1. **Never commit directly to `main`**
   - All development work should happen on `develop` branch
   - `main` should only receive merges from `develop`

2. **Proper Development Flow:**
   ```bash
   # Always work on develop
   git checkout develop
   git pull origin develop

   # Make your changes
   git add .
   git commit -m "feat: your changes"
   git push origin develop

   # When ready to release
   git checkout main
   git pull origin main
   git merge develop  # This will fast-forward if main hasn't changed
   git push origin main
   ```

3. **If Main Must Be Updated:**
   ```bash
   # Option 1: Cherry-pick to develop first
   git checkout develop
   git cherry-pick <commit-from-main>
   git push origin develop

   # Option 2: Merge main into develop first
   git checkout develop
   git merge main
   git push origin develop
   ```

### When Merge Commits Are Expected

Merge commits are **normal and correct** when:
- Both branches have unique commits (legitimate divergence)
- No merge conflicts occurred
- Both development paths need to be preserved

### When to Investigate

Only investigate merge commits if:
- Merge conflicts occurred during merge
- Unexpected files were modified
- Build/tests fail after merge
- Submodule pointers are inconsistent

````
