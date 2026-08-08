#!/usr/bin/env bash
set -Eeuo pipefail
make -j"$(nproc)"
