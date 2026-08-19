#!/usr/bin/env bash

set -u
set -o pipefail

ROOT="${1:-$HOME/NeoBench}"
REPORT="$ROOT/bug-scan-report.txt"
BACKUP="$ROOT/.bugfix-backup-$(date +%Y%m%d-%H%M%S)"

ERRORS=0
WARNINGS=0
FIXES=0

mkdir -p "$BACKUP"

exec > >(tee "$REPORT") 2>&1

echo "============================================================"
echo " NeoBench Bug Scanner / Conservative Auto-Fixer"
echo "============================================================"
echo
echo "ROOT   : $ROOT"
echo "REPORT : $REPORT"
echo "BACKUP : $BACKUP"
echo
echo "Started: $(date)"
echo

if [ ! -d "$ROOT" ]; then
    echo "ERROR: NeoBench directory does not exist: $ROOT"
    exit 1
fi

cd "$ROOT" || exit 1

backup_file()
{
    local f="$1"

    mkdir -p "$BACKUP/$(dirname "$f")"

    cp -p "$f" "$BACKUP/$f"
}

fix_file()
{
    local f="$1"

    backup_file "$f"
    FIXES=$((FIXES + 1))
}

warning()
{
    WARNINGS=$((WARNINGS + 1))
    echo "WARNING: $*"
}

error()
{
    ERRORS=$((ERRORS + 1))
    echo "ERROR: $*"
}

section()
{
    echo
    echo "============================================================"
    echo " $1"
    echo "============================================================"
}

section "SOURCE FILE INVENTORY"

find . \
    -type f \
    \( \
        -name '*.c' \
        -o -name '*.h' \
        -o -name '*.S' \
        -o -name '*.s' \
    \) \
    -not -path './.git/*' \
    -not -path './build/*' \
    -not -path './obj/*' \
    | sort > /tmp/neobench-source-files.txt

SOURCE_COUNT=$(wc -l < /tmp/neobench-source-files.txt)

echo "Source files found: $SOURCE_COUNT"

section "MERGE CONFLICT MARKERS"

CONFLICTS=$(grep -R -nE \
    '^(<<<<<<<|=======|>>>>>>>)' \
    . \
    --include='*.c' \
    --include='*.h' \
    --include='*.S' \
    --include='*.s' \
    --exclude-dir=.git \
    --exclude-dir=build \
    --exclude-dir=obj \
    2>/dev/null || true)

if [ -n "$CONFLICTS" ]; then
    echo "$CONFLICTS"
    error "Merge conflict markers found."
else
    echo "PASS: no merge conflict markers."
fi

section "DUPLICATE STANDARD INCLUDES"

while IFS= read -r f; do

    for hdr in stdio.h stdint.h stddef.h stdlib.h string.h stdbool.h sys/types.h; do

        count=$(grep -cE \
            "^[[:space:]]*#include[[:space:]]+[<\"]${hdr}[>\"]" \
            "$f" 2>/dev/null || true)

        if [ "$count" -gt 1 ]; then
            echo "$f: duplicate <$hdr> include ($count occurrences)"
            warning "Duplicate <$hdr> include in $f"

            # Conservative fix: remove duplicate exact standard includes,
            # retaining the first occurrence.
            backup_file "$f"

            awk -v hdr="$hdr" '
                BEGIN { seen=0 }

                $0 ~ "^[[:space:]]*#include[[:space:]]+[<\"]" hdr "[>\"]" {
                    seen++
                    if (seen > 1)
                        next
                }

                { print }
            ' "$f" > "$f.tmp" && mv "$f.tmp" "$f"

            FIXES=$((FIXES + 1))
        fi

    done

done < /tmp/neobench-source-files.txt

section "PRINTF WITHOUT STDIO.H"

while IFS= read -r f; do

    case "$f" in
        *.c|*.h) ;;
        *) continue ;;
    esac

    if grep -qE '\bprintf[[:space:]]*\(' "$f" 2>/dev/null; then

        if ! grep -qE \
            '^[[:space:]]*#include[[:space:]]+[<"]stdio\.h[>"]' \
            "$f"; then

            echo "FIX: $f uses printf() without <stdio.h>"

            backup_file "$f"

            {
                echo '#include <stdio.h>'
                cat "$f"
            } > "$f.tmp"

            mv "$f.tmp" "$f"

            FIXES=$((FIXES + 1))
        fi
    fi

done < /tmp/neobench-source-files.txt

section "STDINT TYPES WITHOUT STDINT.H"

while IFS= read -r f; do

    case "$f" in
        *.c|*.h) ;;
        *) continue ;;
    esac

    if grep -qE \
        '\b(uint8_t|uint16_t|uint32_t|uint64_t|int8_t|int16_t|int32_t|int64_t)\b' \
        "$f" 2>/dev/null; then

        if ! grep -qE \
            '^[[:space:]]*#include[[:space:]]+[<"]stdint\.h[>"]' \
            "$f"; then

            echo "FIX: $f uses fixed-width integer types without <stdint.h>"

            backup_file "$f"

            {
                echo '#include <stdint.h>'
                cat "$f"
            } > "$f.tmp"

            mv "$f.tmp" "$f"

            FIXES=$((FIXES + 1))
        fi
    fi

done < /tmp/neobench-source-files.txt

section "SIZE_T WITHOUT STDDEF.H"

while IFS= read -r f; do

    case "$f" in
        *.c|*.h) ;;
        *) continue ;;
    esac

    if grep -qE '\bsize_t\b' "$f" 2>/dev/null; then

        has_stddef=0

        if grep -qE \
            '^[[:space:]]*#include[[:space:]]+[<"]stddef\.h[>"]' \
            "$f"; then
            has_stddef=1
        fi

        if grep -qE \
            '^[[:space:]]*#include[[:space:]]+[<"]stdio\.h[>"]' \
            "$f"; then
            has_stddef=1
        fi

        if [ "$has_stddef" -eq 0 ]; then

            echo "FIX: $f uses size_t without <stddef.h>"

            backup_file "$f"

            {
                echo '#include <stddef.h>'
                cat "$f"
            } > "$f.tmp"

            mv "$f.tmp" "$f"

            FIXES=$((FIXES + 1))
        fi
    fi

done < /tmp/neobench-source-files.txt

section "COMMON NULL / BOOL HEADER ISSUES"

while IFS= read -r f; do

    case "$f" in
        *.c|*.h) ;;
        *) continue ;;
    esac

    if grep -qE '\bNULL\b' "$f" 2>/dev/null; then

        if ! grep -qE \
            '^[[:space:]]*#include[[:space:]]+[<"]stddef\.h[>"]' \
            "$f" && \
           ! grep -qE \
            '^[[:space:]]*#include[[:space:]]+[<"]stdio\.h[>"]' \
            "$f"; then

            echo "WARNING: $f uses NULL but has no obvious NULL provider."
            warning "$f may need <stddef.h>."
        fi
    fi

    if grep -qE '\b(bool|true|false)\b' "$f" 2>/dev/null; then

        if ! grep -qE \
            '^[[:space:]]*#include[[:space:]]+[<"]stdbool\.h[>"]' \
            "$f"; then

            warning "$f uses bool/true/false without <stdbool.h>."
        fi
    fi

done < /tmp/neobench-source-files.txt

section "TODO / FIXME / XXX MARKERS"

TODO_COUNT=$(grep -R -nE \
    '\b(TODO|FIXME|XXX)\b' \
    . \
    --include='*.c' \
    --include='*.h' \
    --include='*.S' \
    --include='*.s' \
    --exclude-dir=.git \
    --exclude-dir=build \
    --exclude-dir=obj \
    2>/dev/null || true)

if [ -n "$TODO_COUNT" ]; then
    echo "$TODO_COUNT"
else
    echo "PASS: no TODO/FIXME/XXX markers."
fi

section "SUSPICIOUS DEBUG PRINTS"

DEBUG_PRINTS=$(grep -R -nE \
    'printf[[:space:]]*\([[:space:]]*"DEBUG' \
    kernel \
    --include='*.c' \
    --include='*.h' \
    2>/dev/null || true)

if [ -n "$DEBUG_PRINTS" ]; then
    echo "$DEBUG_PRINTS"
    warning "Debug printf() calls remain in kernel code."
else
    echo "PASS: no DEBUG printf() calls in kernel."
fi

section "VFS NBFS LOOKUP DECLARATION / DEFINITION"

NBFS_DEF=$(grep -R -n \
    'int[[:space:]]\+vfs_nbfs_lookup[[:space:]]*(' \
    kernel/fs/vfs \
    --include='*.c' \
    --include='*.h' \
    2>/dev/null || true)

echo "$NBFS_DEF"

NBFS_C=$(grep -R -l \
    'vfs_nbfs_lookup' \
    kernel/fs/vfs \
    --include='*.c' \
    2>/dev/null || true)

NBFS_H=$(grep -R -l \
    'vfs_nbfs_lookup' \
    kernel/include \
    kernel/fs/vfs \
    --include='*.h' \
    2>/dev/null || true)

if [ -z "$NBFS_C" ]; then
    error "vfs_nbfs_lookup implementation not found."
fi

if [ -z "$NBFS_H" ]; then
    warning "No vfs_nbfs_lookup declaration found in headers."
fi

section "VFS TEST WIRING"

if [ -f kernel/fs/vfs/tests/test_vfs_open.c ]; then

    if grep -q 'fs\.private_data[[:space:]]*=[[:space:]]*ctx' \
        kernel/fs/vfs/tests/test_vfs_open.c; then

        echo "PASS: test_vfs_open connects NBFS context."
    else
        warning "test_vfs_open does not assign fs.private_data."
    fi

    if grep -q 'fs\.lookup[[:space:]]*=[[:space:]]*vfs_nbfs_lookup' \
        kernel/fs/vfs/tests/test_vfs_open.c; then

        echo "PASS: test_vfs_open assigns vfs_nbfs_lookup."
    else

        echo "FIX: test_vfs_open does not assign fs.lookup."

        backup_file kernel/fs/vfs/tests/test_vfs_open.c

        sed -i \
            '/fs\.private_data[[:space:]]*=[[:space:]]*ctx;/a\
    fs.lookup = vfs_nbfs_lookup;' \
            kernel/fs/vfs/tests/test_vfs_open.c

        FIXES=$((FIXES + 1))
    fi

    if ! grep -q '#include[[:space:]]*"nbfs_vfs.h"' \
        kernel/fs/vfs/tests/test_vfs_open.c; then

        if [ -f kernel/fs/vfs/nbfs_vfs.h ]; then

            echo "FIX: adding nbfs_vfs.h to test_vfs_open.c"

            backup_file kernel/fs/vfs/tests/test_vfs_open.c

            sed -i \
                '1i#include "nbfs_vfs.h"' \
                kernel/fs/vfs/tests/test_vfs_open.c

            FIXES=$((FIXES + 1))
        fi
    fi

fi

section "VFS SOURCE / HEADER CONSISTENCY"

if [ -f kernel/fs/vfs/nbfs_vfs.c ]; then

    if grep -q 'vfs_nbfs_lookup' kernel/fs/vfs/nbfs_vfs.c; then
        echo "PASS: nbfs_vfs.c defines vfs_nbfs_lookup."
    else
        error "nbfs_vfs.c does not contain vfs_nbfs_lookup."
    fi
fi

if [ -f kernel/fs/vfs/nbfs_vfs.h ]; then

    if grep -q 'vfs_nbfs_lookup' kernel/fs/vfs/nbfs_vfs.h; then
        echo "PASS: nbfs_vfs.h declares vfs_nbfs_lookup."
    else
        error "nbfs_vfs.h exists but does not declare vfs_nbfs_lookup."
    fi
else
    warning "kernel/fs/vfs/nbfs_vfs.h does not exist."
fi

section "BRACE BALANCE CHECK"

while IFS= read -r f; do

    opens=$(grep -o '{' "$f" 2>/dev/null | wc -l)
    closes=$(grep -o '}' "$f" 2>/dev/null | wc -l)

    if [ "$opens" -ne "$closes" ]; then
        error "$f: braces differ: {=$opens }=$closes"
    fi

done < <(
    find kernel libs tools \
        -type f \
        \( -name '*.c' -o -name '*.h' \) \
        2>/dev/null
)

section "VFS BUILD"

VFS="$ROOT/kernel/fs/vfs"

if [ -d "$VFS" ]; then

    cd "$VFS" || exit 1

    echo "Compiling VFS objects..."

    CC=${CC:-cc}

    CFLAGS=(
        -Wall
        -Wextra
        -std=c11
        -O2
        -I.
        -I../../include
        -I../../include/vfs
        -I../../../libs/libnbfs/include
        -I../../../include
        -I../../../shared/include
    )

    VFS_OBJECTS=(
        dentry.o
        file.o
        filesystem.o
        mount.o
        nbfs_vfs.o
        path.o
        vfs.o
        vnode.o
    )

    BUILD_FAILED=0

    for src in dentry file filesystem mount nbfs_vfs path vfs vnode; do

        if [ -f "$src.c" ]; then

            echo
            echo "CC $src.c"

            if ! "$CC" "${CFLAGS[@]}" -c "$src.c" -o "$src.o"; then
                BUILD_FAILED=1
                error "VFS compilation failed: $src.c"
            fi
        fi

    done

    if [ "$BUILD_FAILED" -eq 0 ]; then
        echo
        echo "PASS: VFS objects compiled."
    fi

    section "VFS OPEN TEST"

    if [ -f tests/test_vfs_open.c ] && \
       [ -f ../../../libs/libnbfs/build/libnbfs.a ]; then

        if "$CC" "${CFLAGS[@]}" \
            tests/test_vfs_open.c \
            "${VFS_OBJECTS[@]}" \
            tests/console_stub.o \
            ../../../libs/libnbfs/build/libnbfs.a \
            -o tests/test_vfs_open; then

            echo
            echo "PASS: test_vfs_open linked."

            echo
            echo "----- RUNNING test_vfs_open -----"

            if ./tests/test_vfs_open; then
                echo
                echo "PASS: test_vfs_open"
            else
                warning "test_vfs_open returned failure."
            fi

        else
            error "test_vfs_open failed to link."
        fi
    else
        warning "test_vfs_open prerequisites not present."
    fi

    section "AVAILABLE VFS TESTS"

    find tests \
        -maxdepth 1 \
        -type f \
        -executable \
        -printf '%f\n' \
        2>/dev/null | sort

else
    warning "VFS directory not found."
fi

cd "$ROOT" || exit 1

section "NBFS LIBRARY"

if [ -f libs/libnbfs/build/libnbfs.a ]; then

    echo "PASS: libnbfs.a exists."

    echo
    echo "Exported lookup/read symbols:"

    nm -g libs/libnbfs/build/libnbfs.a 2>/dev/null |
        grep -E \
        'nbfs_(lookup|read_file|read_inode|open|close)' \
        || true

else
    warning "libnbfs.a does not exist."
fi

section "UNDEFINED SYMBOL SCAN"

if command -v nm >/dev/null 2>&1; then

    find kernel/fs/vfs \
        -type f \
        -name '*.o' \
        -print \
        2>/dev/null |
    while IFS= read -r obj; do

        undef=$(nm -u "$obj" 2>/dev/null || true)

        if [ -n "$undef" ]; then
            echo
            echo "OBJECT: $obj"
            echo "$undef"
        fi

    done

else
    warning "nm is not installed."
fi

section "GIT STATUS"

if [ -d .git ]; then
    git status --short
fi

section "SUMMARY"

echo
echo "Automatic fixes : $FIXES"
echo "Warnings        : $WARNINGS"
echo "Errors          : $ERRORS"
echo
echo "Backup directory:"
echo "  $BACKUP"
echo
echo "Report:"
echo "  $REPORT"
echo

if [ "$ERRORS" -eq 0 ]; then
    echo "RESULT: NO CRITICAL STATIC ERRORS FOUND"
else
    echo "RESULT: CRITICAL ERRORS FOUND"
fi

echo
echo "Finished: $(date)"

exit "$ERRORS"
