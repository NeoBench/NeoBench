#!/bin/bash
set -e

cd /home/adolf/neobench

if [ ! -d .git ]; then
    git init
    git branch -M main
    git remote add origin https://github.com/NeoBench/NeoBench.git
    git config user.name "ArchLinux Dev"
    git config user.email "SHA256:4JJ1zJKpde+c8kyfyqKHV9Oc5VUdIHE0/c1N5iil/8A"
fi

git add -A
git commit -m "Update $(date)" || echo "Nothing to commit."
git push -u --force origin main
