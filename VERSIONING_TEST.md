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

## Update

Updated to test the improved version detection logic.
