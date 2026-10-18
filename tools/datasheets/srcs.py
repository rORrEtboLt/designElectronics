#!/usr/bin/env python3
"""srcs.py NAME... -- print a working datasheet source per name (TI symlink, else checklist URL)."""
import csv, os, sys, importlib.util
HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("ds", os.path.join(HERE, "ds.py")); ds = importlib.util.module_from_spec(spec); spec.loader.exec_module(ds)
rows = list(csv.DictReader(open(os.environ.get("KICAD_CHECKLIST", os.path.join(HERE, "..", "..", "build", "kicad_checklist.tsv")), encoding="utf-8"), delimiter="\t"))
url_of = {}
for r in rows:
    if r["datasheet"].startswith("http") and r["symbol"] not in url_of: url_of[r["symbol"]] = r["datasheet"]
for n in sys.argv[1:]:
    cands = [f"ti:sn{n.lower()}", f"ti:sn{n.lower()}a", f"ti:cd{n.lower()}", f"ti:sn74{n[4:].lower()}" if n.startswith("74LS") else None]
    if n in url_of: cands.append(url_of[n])
    ok = None
    for c in [c for c in cands if c]:
        if ds.fetch(c) is not None: ok = c; break
    print(ok or "NONE:" + n)
