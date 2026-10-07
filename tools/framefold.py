#!/usr/bin/env python3
"""Frame-fold gate: no stack slot may be reached with a variable index.

The m68k backend folds `alloca + constant + variable' into a fixed
displacement and drops the variable (it hung a real boot on that);
`register + variable' keeps it.  Any IR access whose address walks
into an alloca through GEPs carrying a non-constant index is the
shape that miscompiles, whatever the source looks like.
"""
import re
import sys

path = sys.argv[1]
src = open(path).read()
funcs = re.split(r"\n(?=define )", src)
hits = 0

for f in funcs:
    m = re.match(r"define [^\n]*?@([^(]+)\(", f)
    if not m:
        continue
    name = m.group(1)
    allocas = set(re.findall(r"%([\w.]+) = alloca", f))
    defs = dict(re.findall(r"%([\w.]+) = (getelementptr[^\n]+)", f))
    loads = re.findall(r"%([\w.]+) = load [^\n]+, ptr (%[\w.]+),", f)
    stores = re.findall(r"store [^\n]+, ptr (%[\w.]+),", f)

    def chase(v, seen=()):
        """Walk the GEP chain from %v; return (base_is_alloca, var_index)."""
        if v in seen:
            return (False, False)
        d = defs.get(v)
        if d is None:
            return (v in allocas, False)
        base = re.search(r"ptr %([\w.]+)", d)
        idx = re.findall(r", i32 (%[\w.]+|\d+)", d)
        var = any(not i.isdigit() for i in idx)
        if base is None:
            return (False, var)
        is_alloca, deeper_var = chase(base.group(1), seen + (v,))
        return (is_alloca, var or deeper_var)

    for _, ptr in loads:
        a, v = chase(ptr.lstrip("%"))
        if a and v:
            print(f"FRAME-VAR LOAD  {name}: %{ptr}")
            hits += 1
    for ptr in stores:
        a, v = chase(ptr.lstrip("%"))
        if a and v:
            print(f"FRAME-VAR STORE {name}: %{ptr}")
            hits += 1

print(f"frame-fold gate: {hits} hit(s)")
sys.exit(1 if hits else 0)
