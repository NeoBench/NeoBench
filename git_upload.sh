#!/bin/bash
set -e

cd /home/adolf/neobench

if [ ! -d .git ]; then
    git init
    git branch -M main
    git remote add origin https://github.com/NeoBench/NeoBench.git
    git config user.name "ArchLinux Dev"
    git config user.email "your@email.com"
fi

git add -A
git commit -m "Update $(date)" || echo "Nothing to commit."
git push -u --force origin main
