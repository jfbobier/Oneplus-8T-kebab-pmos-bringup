#!/usr/bin/env bash
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
APORT="$ROOT/kernel-aport"

python3 - "$APORT" <<'PY'
from pathlib import Path
import hashlib
import re
import sys

root = Path(sys.argv[1])
text = (root / 'APKBUILD').read_text()
try:
    source = text.split('source="', 1)[1].split('"\nbuilddir=', 1)[0]
except IndexError:
    raise SystemExit('Could not parse source= block')

refs = re.findall(r'\b\d{4}[^\s]+\.patch\b', source)
actual = sorted(p.name for p in root.glob('*.patch'))
missing = [x for x in refs if x not in actual]
extra = [x for x in actual if x not in refs]
config = 'config-postmarketos-qcom-sm8250.aarch64'

print(f'APKBUILD patch references: {len(refs)}')
print(f'Patch files present:       {len(actual)}')
print(f'Active patches missing:    {len(missing)}')
for x in missing:
    print('  -', x)

print('\nPresent but not active in source=:')
for x in extra:
    print('  -', x)

print('\nKernel config:')
print('  present' if (root / config).exists() else f'  MISSING: {config}')

try:
    sums = text.split('sha512sums="', 1)[1].split('\n"', 1)[0]
except IndexError:
    raise SystemExit('Could not parse sha512sums block')
expected = {}
for line in sums.splitlines():
    m = re.fullmatch(r'([0-9a-f]{128})  (.+)', line)
    if m:
        expected[m.group(2)] = m.group(1)

local_files = [config] + refs
checksum_missing = []
checksum_bad = []
checksum_noentry = []
for name in local_files:
    p = root / name
    if not p.exists():
        checksum_missing.append(name)
        continue
    exp = expected.get(name)
    if exp is None:
        checksum_noentry.append(name)
        continue
    got = hashlib.sha512(p.read_bytes()).hexdigest()
    if got != exp:
        checksum_bad.append((name, exp, got))

print('\nSHA-512 verification of local active sources:')
if not checksum_missing and not checksum_noentry and not checksum_bad:
    print(f'  OK: {len(local_files)} local files match APKBUILD')
else:
    for name in checksum_missing:
        print('  MISSING:', name)
    for name in checksum_noentry:
        print('  NO CHECKSUM ENTRY:', name)
    for name, exp, got in checksum_bad:
        print('  CHECKSUM MISMATCH:', name)
        print('    expected:', exp)
        print('    actual:  ', got)

if missing or not (root / config).exists() or checksum_missing or checksum_noentry or checksum_bad:
    raise SystemExit(1)
PY
