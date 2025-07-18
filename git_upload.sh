#!/bin/bash
set -e

cd /home/adolf/neobench

# Setup Git if not already initialized
if [ ! -d .git ]; then
    git init
    git config user.name "adolf"
    git config user.email "adolf@neobench.org"
    git branch -M main
    git remote add origin https://github.com/NeoBench/NeoBench.git
else
    git remote set-url origin https://github.com/NeoBench/NeoBench.git
    git branch -M main
fi

git add -A
git commit -am "Update $(date)" || echo "Nothing to commit."
git push -u --force origin main

echo "NeoBench GitHub sync complete: https://github.com/NeoBench/NeoBench"
