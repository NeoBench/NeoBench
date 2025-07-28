#!/bin/bash
set -e

REPO_DIR="../neobench-git"

# Safety: don't run this script from inside the Git repo
if [ "$PWD" = "$(realpath "$REPO_DIR")" ]; then
  echo "❌ ERROR: You are running from inside the Git repo: $REPO_DIR"
  exit 1
fi

# Safety: check if it's a real Git repo
if [ ! -d "$REPO_DIR/.git" ]; then
  echo "❌ ERROR: $REPO_DIR is not a valid Git repository (missing .git)"
  exit 1
fi

echo "[NeoBench] Wiping repo contents except .git..."
cd "$REPO_DIR"
find . -mindepth 1 -not -path "./.git*" -exec rm -rf {} +

echo "[NeoBench-os] Copying NeoBench project files..."
cd ../neobench-os

# Use rsync to preserve permissions and avoid nested .git issues
rsync -a --exclude='.git/' --exclude='build/' --exclude='target/' ./ "$REPO_DIR"

echo "[NeoBench-os] Committing and pushing..."
cd "$REPO_DIR"
git add .
git commit -m "Full clean upload of NeoBench source"
git push -f

echo "[NeoBench-os] ✅ Git push complete."
