#!/usr/bin/env bash
set -Eeuo pipefail
make clean
make -j"$(nproc)"
