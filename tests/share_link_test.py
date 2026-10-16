#!/usr/bin/env python3
"""Checks that share/embed links ("#d=…") decode back to the exact design:
the app's built-in deflate encoder must produce standard raw DEFLATE.

    python3 tests/share_link_test.py path/to/designElectronics
"""
import base64, subprocess, sys, zlib

exe = sys.argv[1]
ok = True
for i in range(7):
    frag = subprocess.run([exe, "--share-fragment", str(i)], capture_output=True, text=True).stdout.strip()
    js = subprocess.run([exe, "--share-fragment", str(i), "--json"], capture_output=True, text=True).stdout.strip()
    data = frag[2:] + "=" * (-len(frag[2:]) % 4)
    good = frag.startswith("d=") and zlib.decompress(base64.urlsafe_b64decode(data), -15).decode() == js
    ok &= good
    print(f"  [{' ok ' if good else 'FAIL'}] example {i}: {len(js)} B JSON -> {len(frag)} B link")
print("Share link test", "passed" if ok else "FAILED")
sys.exit(0 if ok else 1)
