# AvesAID Versioning Test

This file is used to test the automatic AvesAID versioning system.

## Expected Behavior

When commits are made to the `develop` branch, the post-commit hook should automatically:

1. Find the latest upstream version tag (e.g., `v1.15.4`)
2. Find the latest AvesAID version for that upstream base (e.g., `v1.15.4-1.2.3`)
3. Increment the AvesAID version based on commit message:
   - `[major]` or `BREAKING CHANGE` → increment major, reset minor/patch to 0
   - `[minor]` or `feat:` → increment minor, reset patch to 0
   - Default → increment patch
4. Create new tag (e.g., `v1.15.4-1.2.4`)

## Test Commit

This commit should trigger automatic versioning.
