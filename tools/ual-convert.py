#!/usr/bin/env python3
"""Rewrite 2013-era Apple ARM assembly into syntax modern LLVM accepts.

The darwin-on-arm XNU tree (and the HTC HD2 port built on it) was assembled
with Apple's LLVM 3.x, which still accepted pre-UAL "divided" ARM syntax.
Today's LLVM only takes UAL, so we rewrite the handful of constructs the tree
uses. Every rewrite is a pure respelling: the encoded instruction is the same.

  ldmeqfd sp!, {..}      -> ldmfdeq sp!, {..}      (condition after mode)
  strgeb  r2, [r1], #1   -> strbge  r2, [r1], #1   (condition after size)
  movnes  r0, r1         -> movsne  r0, r1         (S flag before condition)

Symbolic offsets such as [r10, TH_RECOVER] are left alone: genassym emits
those constants with their '#' already attached ("#define TH_RECOVER #12").

Usage: ual-convert.py FILE...   (rewrites in place, prints a change count)
"""
import re
import sys

COND = r"(?:eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al)"
# Rules apply to the mnemonic only (first token of an instruction).
MNEMONIC_RULES = [
    # ldm/stm<cond><mode> -> ldm/stm<mode><cond>
    (re.compile(rf"^(ldm|stm)({COND})(ia|ib|da|db|fd|fa|ed|ea)$", re.I), r"\1\3\2"),
    # ldr/str<cond><size> -> ldr/str<size><cond>
    (re.compile(rf"^(ldr|str)({COND})(bt|b|h|sb|sh|t|d)$", re.I), r"\1\3\2"),
    # <dataop><cond>s -> <dataop>s<cond>
    (re.compile(rf"^(mov|mvn|add|sub|rsb|and|orr|eor|bic|adc|sbc|rsc|mul|mla|lsl|lsr|asr|ror)({COND})s$", re.I),
     r"\1s\2"),
]

# Instruction line: optional label, whitespace, mnemonic, rest.
LINE = re.compile(r"^(\s*(?:[A-Za-z_.$][\w.$]*:\s*)?)([A-Za-z][A-Za-z0-9.]*)(\b.*)$")


def convert_line(line):
    code, comment = line, ""
    # Leave comments alone: '@' and '//' line comments, and C-style blocks.
    for marker in ("@", "//", "/*"):
        idx = code.find(marker)
        if idx != -1:
            code, comment = code[:idx], code[idx:]
            break
    m = LINE.match(code)
    if not m or code.lstrip().startswith(("#", ".")):
        return line
    prefix, mnem, rest = m.groups()
    new_mnem = mnem
    for rx, repl in MNEMONIC_RULES:
        if rx.match(mnem):
            new_mnem = rx.sub(repl, mnem)
            break
    return prefix + new_mnem + rest + comment


def main(paths):
    total = 0
    for path in paths:
        with open(path) as f:
            lines = f.read().split("\n")
        out = [convert_line(l) for l in lines]
        changed = sum(a != b for a, b in zip(lines, out))
        if changed:
            with open(path, "w") as f:
                f.write("\n".join(out))
        total += changed
        print(f"{changed:5d}  {path}")
    print(f"{total:5d}  total lines rewritten")


if __name__ == "__main__":
    main(sys.argv[1:])
