#!/usr/bin/env bash
set -euo pipefail
[[ "$EVENT" == workflow_dispatch ]]
[[ "$SOURCE_REF" == refs/heads/main ]]
[[ "$SOURCE_SHA" =~ ^[0-9a-f]{40}$ ]]
[[ "$SOURCE_REPOSITORY" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]]
[[ "$WORKFLOW_REPOSITORY" == "$SOURCE_REPOSITORY" ]]
[[ "$WORKFLOW_SHA" == "$SOURCE_SHA" ]]
[[ "$WORKFLOW_PATH" == .github/workflows/release.yml ]]
[[ "$WORKFLOW_REF" == "$SOURCE_REPOSITORY/$WORKFLOW_PATH@refs/heads/main" ]]
[[ "$(git rev-parse HEAD)" == "$SOURCE_SHA" ]]
git merge-base --is-ancestor "$SOURCE_SHA" refs/remotes/origin/main
