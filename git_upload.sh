#!/bin/bash
set -e

cd /home/adolf/neobench

if [ ! -d .git ]; then
    git init
    git branch -M main
    git remote add origin https://github.com/NeoBench/NeoBench
    git config user.name "ArchLinux Dev"
    git config user.email "dlester626@gmail.com"
fi

git add -A
git commit -m "Update $(date)" || echo "Nothing to commit."
git push -u --force origin main

echo "NeoBench GitHub sync complete: https://github.com/NeoBench/NeoBench"
