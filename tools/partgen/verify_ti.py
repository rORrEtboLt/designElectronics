#!/usr/bin/env python3
"""verify_ti.py PARTS.txt BLOCK=TIPART ... -- compare a block's pin list with the
"Pin Functions" table of a TI datasheet (text PDFs). Prints mismatches."""
import os, re, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "datasheets"))
import importlib.util
spec = importlib.util.spec_from_file_location("ds", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "datasheets", "ds.py"))
ds = importlib.util.module_from_spec(spec); spec.loader.exec_module(ds)

def blocks(path):
    out, cur = {}, None
    key = None
    for raw in open(path, encoding="utf-8"):
        l = raw.strip()
        if not l or l.startswith("#"): continue
        if l.startswith("[") and l.endswith("]"):
            cur = out.setdefault(l[1:-1], {}); key = None; continue
        if raw[0] in " \t" and key: cur[key] += " " + l; continue
        if "=" in l and cur is not None:
            key, v = (x.strip() for x in l.split("=", 1)); cur[key] = v
    return out

def norm(n):
    n = n.upper().replace("~", "").replace("/", "").replace("‾", "").replace(" ", "")
    n = re.sub(r"[^A-Z0-9+\-]", "", n)
    return n

def table(text):
    """NAME -> set(pin numbers) from the first package column of Pin Functions tables."""
    res = {}
    lines = text.splitlines()
    for k, l in enumerate(lines):
        if not re.search(r"Pin Functions", l): continue
        for m in lines[k + 1:k + 120]:
            if re.search(r"(Specifications|Absolute Maximum|^\s*\d+\s+Specifications)", m): break
            mm = re.match(r"\s*([0-9A-Za-z~][\w/+\-~=<>()]*(?: [A-Za-z~][\w/+\-~=<>()]*)?)\s{2,}(\d+(?:\s*,\s*\d+)*)\b", m)
            if mm and re.search("[A-Za-z]", mm.group(1)):
                for n in re.split(r"\s*,\s*", mm.group(2)):
                    res.setdefault(norm(mm.group(1)), set()).add(int(n))
        if res: break
    return res

def topview(text):
    """NAME -> pins from 'NAME n  m NAME' rows of a DIP top-view drawing."""
    res = {}
    lines = text.splitlines()
    for k, l in enumerate(lines):
        if "TOP VIEW" not in l: continue
        got = {}
        for m in lines[k + 1:k + 40]:
            mm = re.match(r"\s*(\S+(?: \S+)?)\s+(\d{1,2})\s+(\d{1,2})\s+(\S+(?: \S+)?)\s*$", m)
            if mm:
                got.setdefault(norm(mm.group(1)), set()).add(int(mm.group(2)))
                got.setdefault(norm(mm.group(4)), set()).add(int(mm.group(3)))
        if len(got) >= 6:
            return got
    return res

def main():
    path = sys.argv[1]
    bl = blocks(path)
    names = sys.argv[2:] or list(bl)
    for arg in names:
        name, ti = arg.split("=") if "=" in arg else (arg, None)
        b = bl.get(name)
        if not b or "pins" not in b: print(name, "no block"); continue
        pins = b["pins"].split()
        src = ("ti:" + ti) if ti else b.get("datasheet", "")
        t = ds.fetch(src)
        if not t: print(f"{name}: NODS {src}"); continue
        tab = table(t) or topview(t)
        if not tab: print(f"{name}: NOTABLE {src}"); continue
        bad = []
        for i, p in enumerate(pins, 1):
            if p.upper() in ("NC",): continue
            n = norm(p)
            hits = [k for k, v in tab.items() if i in v]
            if not hits: bad.append(f"{i}:{p}?(none)")
            elif n not in hits and not any(n.endswith(h) or h.endswith(n) for h in hits): bad.append(f"{i}:{p}!={'/'.join(hits)}")
        print(f"{name}: " + ("OK" if not bad else "MISMATCH " + " ".join(bad)) + f"  [{src}]")
main()
