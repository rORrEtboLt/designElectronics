#!/usr/bin/env python3
"""ds.py URL|ti:PART [--lines N] [--grep REGEX] -- fetch a datasheet (cached) and print its pin section."""
import hashlib, os, re, subprocess, sys
CACHE = os.environ.get("DS_CACHE", os.path.expanduser("~/.cache/designElectronics-datasheets"))
def fetch(url):
    os.makedirs(CACHE, exist_ok=True)
    if url.startswith("ti:"):
        url = "https://www.ti.com/lit/ds/symlink/%s.pdf" % url[3:].lower()
    h = hashlib.sha1(url.encode()).hexdigest()[:16]
    pdf, txt = os.path.join(CACHE, h + ".pdf"), os.path.join(CACHE, h + ".txt")
    if not os.path.exists(txt):
        subprocess.run(["curl", "-sL", "-m", "60", "-A", "Mozilla/5.0", "-o", pdf, url], check=False)
        if not os.path.exists(pdf) or open(pdf, "rb").read(4) != b"%PDF":
            return None
        subprocess.run(["pdftotext", "-layout", pdf, txt], check=False)
    return open(txt, encoding="utf-8", errors="replace").read() if os.path.exists(txt) else None
def main():
    args = sys.argv[1:]
    n = 45
    pat = r"(Pin Functions|PIN FUNCTIONS|Pin Configuration|PIN CONFIGURATION|Pin Descriptions|PIN DESCRIPTION|Pin Assignment|PINOUT|Pinout)"
    if "--lines" in args: i = args.index("--lines"); n = int(args[i + 1]); del args[i:i + 2]
    if "--grep" in args: i = args.index("--grep"); pat = args[i + 1]; del args[i:i + 2]
    for url in args:
        t = fetch(url)
        print("=====", url)
        if t is None: print("  (no PDF)"); continue
        lines = t.splitlines()
        shown = 0
        for k, l in enumerate(lines):
            if re.search(pat, l) and shown < 2:
                block = [x.rstrip() for x in lines[k:k + n] if x.strip()]
                print("\n".join(re.sub(r"\s{3,}", "   ", x) for x in block))
                print("  ...")
                shown += 1
if __name__ == "__main__": main()
