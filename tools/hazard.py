#!/usr/bin/env python3
"""M68k flag-hazard scanner.

The hazard: a compare sets CCR, some other flag-setting instruction
(a MOVE materialising a MIR COPY, usually) lands between it and the
branch, and the branch then reads the wrong flags.  The compiler's
model says the interleaved COPY is flag-neutral; the hardware says
MOVE sets CCR.  Nothing revalidates, so the only way to catch it is
to read the final bytes.

Class rules
  * walk backwards from each conditional branch over flag-neutral
    instructions (lea/movem/movea/adda/suba/cmpa/exg/nop/jsr/pea/...);
  * the first flag-setter found is what the hardware will show the
    branch  -- the provider;
  * walk further back: if another flag-setter (S2) exists and the
    provider is value movement (move/moveq/clr), the branch was in
    all likelihood meant for S2  -- report it;
  * benign exception: Z-only branch (beq/bne) where S2 is a pure
    value test (tst R / cmp #0,R) and the provider moves that same
    register -- MOVE preserves Z from the same value, so the branch
    still reads what it should.

Only point this at OUR objects: gcc mixes intentional `move; bne'
shapes and will produce false positives here.  The boot is the
oracle; this is the per-build gate.

Env: HAZARD_OBJDUMP  (default m68k-linux-gnu-objdump)
     HAZARD_FN       (only report this function)
     HAZARD_QUIET=1  (one line per function, no instruction windows)
"""
import os
import re
import subprocess
import sys

OBJDUMP = os.environ.get("HAZARD_OBJDUMP", "m68k-linux-gnu-objdump")

NEUTRAL = re.compile(
    r"^(lea|movea|movem|adda|suba|cmpa|exg|nop|rts|rte|jmp|jsr|bsr|pea|"
    r"link|unlk|trap|illegal|reset|stop|tas|chk|cas|moves)$"
)
# value movement: sets CCR but is never meant to branch on it
MOVE_LIKE = re.compile(r"^(move|moveq|moveb|movew|movel|clr|clrb|clrw|clrl)$")
# a dead-compare lookalike behind the provider
CMP_LIKE = re.compile(r"^(cmp|cmpb|cmpw|cmpl|cmpi|cmpib|cmpiw|cmpil|tst|tstb|tstw|tstl)$")
BRANCH = re.compile(
    r"^b(eq|ne|cs|cc|hs|lo|mi|pl|ge|gt|le|ls|hi|lt|vc|vs)[sl]?$"
)
DBRANCH = re.compile(r"^db(eq|ne|cs|cc|hs|lo|mi|pl|ge|gt|le|ls|hi|lt|vc|vs)[sl]?$")
SETCC = re.compile(r"^s(eq|ne|cs|cc|hs|lo|mi|pl|ge|gt|le|ls|hi|lt|vc|vs)$")
# everything else that touches CCR
SETS = re.compile(
    r"^(add|addb|addw|addl|addi|addq|sub|subb|subw|subl|subi|subq|"
    r"and|andb|andw|andl|andi|or|orb|orw|orl|ori|eor|eorb|eorw|eorl|eori|"
    r"neg|negb|negw|negl|negx|not|notb|notw|notl|"
    r"lsl|lsr|asl|asr|rol|ror|roxl|roxr|swap|ext|extb|"
    r"mul|muls|mulu|mull|div|divs|divu|divl|"
    r"btst|bset|bclr|bchg|cmpm|abcd|sbcd|nbcd)$"
)


def sets_flags(mn):
    return bool(
        SETCC.match(mn)
        or SETS.match(mn)
        or CMP_LIKE.match(mn)
        or mn in ("move", "moveq", "moveb", "movew", "movel", "clr", "clrb", "clrw", "clrl")
    )


def neutral(mn):
    return bool(NEUTRAL.match(mn))


def operands(text):
    # text = "movel %d2,%d6" / "subl #8,%d1" / "cmpl #0,%d2"
    if not text:
        return ""
    parts = text.split(None, 1)
    return parts[1] if len(parts) > 1 else ""


def norm_reg(op):
    m = re.search(r"%[ad]\d+", op)
    return m.group(0) if m else None


def is_zero_test(mn, op):
    if mn.startswith("tst"):
        return True
    if mn.startswith("cmp"):
        return re.match(r"^#0\b|^#0x0\b", op.split(",")[0]) is not None
    return False


def scan(text, only_fn=None, quiet=False):
    findings = {}
    fn = None
    insns = []  # (offset, mn, ops, line)

    def flush():
        nonlocal insns
        if not insns:
            return
        hits = []
        for i, (off, mn, ops, line) in enumerate(insns):
            if not (BRANCH.match(mn) or DBRANCH.match(mn)):
                continue
            # provider: walk back over neutral instructions
            j = i - 1
            while j >= 0 and neutral(insns[j][1]):
                j -= 1
            if j < 0:
                continue
            poff, pmn, pops, pline = insns[j]
            if not sets_flags(pmn):
                continue
            # S2 behind the provider
            k = j - 1
            while k >= 0 and neutral(insns[k][1]):
                k -= 1
            if k < 0:
                continue
            soff, smn, sops, sline = insns[k]
            if not sets_flags(smn):
                continue
            if not (MOVE_LIKE.match(pmn) or pmn == "clr"):
                continue  # provider is a real ALU: branch on it is intended
            # benign: Z-only branch, S2 a pure value test of the register
            # the provider is copying -- MOVE preserves that Z.
            benign = False
            if mn in ("beq", "bne", "beqs", "bnes") and is_zero_test(smn, sops):
                src = norm_reg(pops.split(",")[0]) if pops else None
                tested = norm_reg(sops.split(",")[-1]) if sops else None
                if src and tested and src == tested:
                    benign = True
            hits.append((benign, soff, smn + " " + sops, poff, pmn + " " + pops,
                         off, mn, sline, pline, line))
        if hits:
            findings[fn] = hits
        insns = []

    for raw in text.splitlines():
        m = re.match(r"^[0-9a-f]+ <([^>]+)>:$", raw)
        if m:
            flush()
            fn = m.group(1)
            if only_fn and fn != only_fn:
                fn = None if only_fn else fn
            continue
        m = re.match(r"^\s*([0-9a-f]+):\t[0-9a-f ]+\s*(\S+)(?:\s+(.*))?$", raw)
        if m and fn and not (only_fn and fn != only_fn):
            off = int(m.group(1), 16)
            mn = m.group(2)
            ops = m.group(3) or ""
            insns.append((off, mn, ops, raw.strip()))
    flush()

    total_true = total_benign = 0
    for name, hits in sorted(findings.items()):
        true = [h for h in hits if not h[0]]
        benign = [h for h in hits if h[0]]
        total_true += len(true)
        total_benign += len(benign)
        print(f"{name}: {len(true)} hazard(s), {len(benign)} benign")
        if not quiet:
            for (isb, soff, s, poff, p, off, b, *_rest) in hits:
                tag = "BENIGN" if isb else "HAZARD"
                print(f"  [{tag}] {soff:04x}: {s}")
                print(f"          {poff:04x}: {p}")
                print(f"          {off:04x}: {b}")
    print(f"TOTAL: {total_true} hazard(s), {total_benign} benign")
    return total_true


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    only = None
    for a in sys.argv[1:]:
        if a.startswith("--fn="):
            only = a.split("=", 1)[1]
    if not args:
        text = sys.stdin.read()
    else:
        text = subprocess.run(
            [OBJDUMP, "-d"] + args, capture_output=True, text=True, check=True
        ).stdout
    raise SystemExit(scan(text, only, os.environ.get("HAZARD_QUIET") == "1"))


if __name__ == "__main__":
    main()
