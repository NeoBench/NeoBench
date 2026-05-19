# =========================
# FIX STANDARD TYPE CONFLICTS
# =========================

s/\btypedef signed long long[[:space:]]\+int64_t;/\/\/ removed shadow int64_t/g
s/\btypedef uint32_t[[:space:]]\+size_t;/\/\/ removed shadow size_t/g
s/\btypedef int32_t[[:space:]]\+ssize_t;/\/\/ removed shadow ssize_t/g

# =========================
# FIX NON-STANDARD TYPES USED IN CODE
# =========================

s/\buint8\b/uint8_t/g
s/\buint32\b/uint32_t/g
s/\bint32\b/int32_t/g

# =========================
# FIX MISSING MACROS
# =========================

s/\bMIN\b/std::min/g
s/\bMAX\b/std::max/g
