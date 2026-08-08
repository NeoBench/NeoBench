#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"

echo "========================================"
echo " Creating NeoBench CI Framework"
echo "========================================"

###############################################################################
# Directories
###############################################################################

mkdir -p .github/workflows
mkdir -p ci
mkdir -p ci/scripts
mkdir -p ci/tests
mkdir -p ci/config
mkdir -p ci/reports
mkdir -p ci/artifacts
mkdir -p ci/docs

###############################################################################
# GitHub Actions
###############################################################################

cat > .github/workflows/build.yml <<'EOF'
name: Build

on:
  push:
  pull_request:

jobs:
  build:
    runs-on: ubuntu-latest

    steps:
      - uses: actions/checkout@v4

      - name: Build
        run: echo "Build pipeline placeholder"
EOF

cat > .github/workflows/test.yml <<'EOF'
name: Test

on:
  push:
  pull_request:

jobs:
  test:
    runs-on: ubuntu-latest

    steps:
      - uses: actions/checkout@v4

      - name: Run tests
        run: echo "Test pipeline placeholder"
EOF

###############################################################################
# CI Scripts
###############################################################################

SCRIPTS=(
build-all.sh
run-tests.sh
check-format.sh
lint.sh
package.sh
release.sh
)

for f in "${SCRIPTS[@]}"; do
cat > "ci/scripts/$f" <<EOF
#!/usr/bin/env bash
set -Eeuo pipefail

echo "$f not implemented yet."
EOF
chmod +x "ci/scripts/$f"
done

###############################################################################
# Test Suites
###############################################################################

TESTS=(
kernel.test
boot.test
nbfs.test
vfs.test
drivers.test
userspace.test
)

for t in "${TESTS[@]}"; do
touch "ci/tests/$t"
done

###############################################################################
# Configuration
###############################################################################

touch ci/config/ci.conf

###############################################################################
# Documentation
###############################################################################

touch ci/docs/README.md
touch ci/docs/PIPELINE.md
touch ci/docs/TESTING.md

echo
echo "CI framework created."

find ci .github | sort
