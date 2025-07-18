#!/bin/bash

# NeoBench automatic GitHub upload script
# Set working directory
cd /home/adolf/neobench || { echo "Directory /home/adolf/neobench not found!"; exit 1; }

# Optional: Uncomment to force clone fresh copy each time (dangerous!)
# rm -rf NeoBench
git clone https://github.com/NeoBench.git
# cd NeoBench

# Set up git if not already
if [ ! -d ".git" ]; then
  git init
  git remote add origin https://github.com/NeoBench.git
fi

# Add everything (including deletions)
git add -A

# Commit with timestamp or user message
msg="Auto-update: $(date '+%Y-%m-%d %H:%M:%S')"
if [ -n "$1" ]; then
  msg="$1"
fi
git commit -m "$msg" --allow-empty

# Set main as default branch if missing
if ! git branch | grep -q "main"; then
  git branch -M main
fi

# Pull first to resolve possible remote changes (overwrites local if needed)
git pull --rebase --autostash origin main

# Push to GitHub, overwrite remote if needed (force-with-lease is safer than --force)
git push origin main --force-with-lease

echo "Repo uploaded and synchronized to https://github.com/NeoBench.git"
