#!/usr/bin/env python3
"""Build a coverage checklist from a KiCad symbol library checkout.

Reads only each symbol's name, library, base symbol (`extends`), footprint
package name and datasheet link -- never pins or graphics. designElectronics
parts are written from manufacturer datasheets; this list only tracks which
parts KiCad users expect to find.

    git clone --depth 1 https://gitlab.com/kicad/libraries/kicad-symbols.git
    python3 tools/kicad_checklist.py kicad-symbols > checklist.tsv
    python3 tools/kicad_checklist.py kicad-symbols --coverage   # what's missing
"""
import fnmatch
import os
import re
import sys

SYM = re.compile(r'^\t\(symbol "([^"]+)"', re.M)
EXT = re.compile(r'^\t\t\(extends "([^"]+)"\)', re.M)
PROP = re.compile(r'^\t\t\(property "(Footprint|Datasheet)" "([^"]*)"', re.M)


def symbols(root):
    for lib in sorted(os.listdir(root)):
        if not lib.endswith(".kicad_symdir"):
            continue
        name = lib[: -len(".kicad_symdir")]
        d = os.path.join(root, lib)
        for f in sorted(os.listdir(d)):
            if not f.endswith(".kicad_sym"):
                continue
            text = open(os.path.join(d, f), encoding="utf-8").read()
            m = SYM.search(text)
            if not m:
                continue
            e = EXT.search(text)
            props = dict(PROP.findall(text))
            fp = props.get("Footprint", "")
            yield {
                "library": name,
                "symbol": m.group(1),
                "base": e.group(1) if e else "",
                "package": fp.split(":", 1)[-1] if fp else "",
                "datasheet": props.get("Datasheet", "").strip("~"),
            }


def covered_names():
    """Part numbers designElectronics provides (block names and aliases) and
    `covers` patterns (shell globs) for generic symbols."""
    here = os.path.dirname(os.path.abspath(__file__))
    names, patterns = set(), []
    pdir = os.path.join(here, "..", "parts")
    for f in sorted(os.listdir(pdir)) if os.path.isdir(pdir) else []:
        if not f.endswith(".txt"):
            continue
        key = None
        for raw in open(os.path.join(pdir, f), encoding="utf-8"):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if raw[0] in " \t" and key in ("aliases", "covers"):
                vals = line
            elif line.startswith("[") and line.endswith("]"):
                names.add(norm(line[1:-1]))
                key = None
                continue
            elif "=" in line:
                key, vals = (s.strip() for s in line.split("=", 1))
                if key not in ("aliases", "covers"):
                    continue
            else:
                continue
            for a in (v.strip() for v in vals.split(",")):
                if not a:
                    continue
                if any(c in a for c in "*?["):
                    patterns.append(a.upper())
                else:
                    names.add(norm(a))
    return names, patterns


def norm(s):
    return re.sub(r"[^A-Z0-9]", "", s.upper())


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    rows = list(symbols(sys.argv[1]))
    if "--coverage" in sys.argv:
        have, patterns = covered_names()
        by_lib = {}
        for r in rows:
            sym = r["symbol"].upper()
            ok = norm(sym) in have or any(fnmatch.fnmatchcase(sym, p) for p in patterns)
            t = by_lib.setdefault(r["library"], [0, 0, []])
            t[0] += ok
            t[1] += 1
            if not ok:
                t[2].append(r["symbol"])
        done = sum(t[0] for t in by_lib.values())
        print(f"covered {done} / {len(rows)} KiCad symbols")
        for lib, (ok, n, miss) in sorted(by_lib.items(), key=lambda kv: kv[1][1] - kv[1][0], reverse=True):
            print(f"{ok:5d}/{n:<5d} {lib}" + ("" if "--missing" not in sys.argv else "  missing: " + " ".join(miss[:40])))
        return 0
    print("library\tsymbol\tbase\tpackage\tdatasheet")
    for r in rows:
        print("\t".join(r[k] for k in ("library", "symbol", "base", "package", "datasheet")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
