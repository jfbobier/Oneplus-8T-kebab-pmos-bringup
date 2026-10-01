#!/usr/bin/env python3
"""Compare two decompiled devicetrees, ignoring phandle renumbering.

Removing a node shifts every phandle after it, so a plain diff of two
`dtc -I dtb -O dts` outputs is unreadable. This flattens both trees into
path::property maps, resolves every phandle-looking cell to the path of the
node it points at, and reports only what actually differs.

Cells inside `interrupts`, `reg`, and similar properties are small integers
that can collide with phandle values, so the raw values are reported too:
a difference that disappears once phandles are normalised, and whose raw
form differs only in hex numbers, is renumbering and nothing else.
"""
import re
import sys

PHANDLE_SAFE = re.compile(r'0x[0-9a-f]+')


def parse(path):
    props, stack, phandles, buf = {}, [], {}, ""
    with open(path) as f:
        for raw in f:
            s = raw.split('//')[0].strip()
            if not s:
                continue
            buf += (" " if buf else "") + s
            if not buf.endswith((';', '{', '}')):
                continue
            s, buf = buf, ""
            if s.endswith('{'):
                stack.append(s[:-1].strip())
            elif s in ('};', '}'):
                stack.pop()
            else:
                body = s[:-1]
                if '=' in body:
                    k, v = body.split('=', 1)
                    k, v = k.strip(), v.strip()
                else:
                    k, v = body.strip(), True
                props['/'.join(stack) + '::' + k] = v
                if k == 'phandle':
                    phandles[int(v.strip('<>'), 16)] = '/'.join(stack)
    return props, phandles


def norm(v, ph):
    if v is True:
        return True
    return PHANDLE_SAFE.sub(
        lambda m: '@' + ph[int(m.group(0), 16)]
        if int(m.group(0), 16) in ph else m.group(0), v)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    a, pa = parse(sys.argv[1])
    b, pb = parse(sys.argv[2])

    only_a = sorted(set(a) - set(b))
    only_b = sorted(set(b) - set(a))
    changed = []
    numeric = []
    renumbered = 0
    for k in sorted(set(a) & set(b)):
        if k.endswith('::phandle'):
            continue
        if a[k] == b[k]:
            continue
        if norm(a[k], pa) == norm(b[k], pb):
            renumbered += 1
            continue
        # Differs even after resolving phandles. If the only textual
        # difference is hex numbers, it is usually renumbering leaking into
        # cells that are not phandles (interrupt specifiers and the like) --
        # but it can also be a real value change, so list it separately
        # instead of hiding it.
        if PHANDLE_SAFE.sub('#', a[k]) == PHANDLE_SAFE.sub('#', b[k]):
            numeric.append((k, a[k], b[k]))
            continue
        changed.append((k, a[k], b[k]))

    print(f"=== only in {sys.argv[1]} ({len(only_a)}) ===")
    for k in only_a:
        print("  -", k, "=", a[k])
    print(f"=== only in {sys.argv[2]} ({len(only_b)}) ===")
    for k in only_b:
        print("  +", k, "=", b[k])
    print(f"=== value changed ({len(changed)}) ===")
    for k, va, vb in changed:
        print("  ~", k)
        print("      ref :", va)
        print("      cand:", vb)
    print(f"=== numeric-only differences ({len(numeric)}) "
          f"-- phandle renumbering leaking into non-phandle cells, "
          f"or a real change; check each ===")
    for k, va, vb in numeric:
        print(f"  ? {k}: {va} -> {vb}")
    print(f"=== resolved phandle renumbering: {renumbered} properties ===")
    return 1 if (only_a or only_b or changed or numeric) else 0


if __name__ == '__main__':
    sys.exit(main())
