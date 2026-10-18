#!/usr/bin/env python3
"""pinimg.py OUT.png URL|ti:PART[@page] ... -- crop the pinout drawing ("TOP VIEW") of each datasheet
and stack the crops into one PNG (labelled by order). Falls back to the top half of page 1."""
import hashlib, os, re, subprocess, sys, html
HERE = os.path.dirname(os.path.abspath(__file__))
CACHE = os.environ.get("DS_CACHE", os.path.expanduser("~/.cache/designElectronics-datasheets"))
sys.path.insert(0, HERE)
import importlib.util
spec = importlib.util.spec_from_file_location("ds", os.path.join(HERE, "ds.py")); ds = importlib.util.module_from_spec(spec); spec.loader.exec_module(ds)

def pdf_of(url):
    if url.startswith("ti:"): url = "https://www.ti.com/lit/ds/symlink/%s.pdf" % url[3:].lower()
    ds.fetch(url)
    return os.path.join(CACHE, hashlib.sha1(url.encode()).hexdigest()[:16] + ".pdf")

def find_caption(pdf, want=None):
    args = ["pdftotext", "-bbox", "-l", "8", pdf, "-"] if not want else ["pdftotext", "-bbox", "-f", str(want), "-l", str(want), pdf, "-"]
    out = subprocess.run(args, capture_output=True, text=True).stdout
    page = 0; prev = None
    for line in out.splitlines():
        if "<page " in line:
            page = (want - 1 if want else page) + 1 if not want else want; m = re.search(r'width="([\d.]+)" height="([\d.]+)"', line); W, H = float(m.group(1)), float(m.group(2)); prev = None; continue
        m = re.search(r'xMin="([\d.]+)" yMin="([\d.]+)" xMax="([\d.]+)" yMax="([\d.]+)">([^<]*)<', line)
        if not m: continue
        w = html.unescape(m.group(5)).strip("()")
        if w.upper() == "VIEW" and prev and prev[4].upper() == "TOP":
            return page, float(prev[0]), float(m.group(2)), W, H
        prev = (m.group(1), m.group(2), m.group(3), m.group(4), w)
    return None

def crop(pdf, idx, tag, page=None):
    c = find_caption(pdf, page)
    dpi = 100
    if c:
        page, x, y, W, H = c
    else:
        page, x, y, W, H = 1, 0, 0, 612, 792
    base = os.path.join(CACHE, f"crop{idx}")
    subprocess.run(["pdftoppm", "-r", str(dpi), "-f", str(page), "-l", str(page), "-png", "-singlefile", pdf, base], check=False)
    png = base + ".png"
    s = dpi / 72.0
    if c:
        # drawings sit either just above (new TI) or just below (old TI) the caption: keep a band around it
        top = max(0, int((y - 215) * s)); h = int(430 * s)
        left = max(0, int((x - 150) * s)); w = int(min(W * s - left, 300 * s))
    else:
        top, h, left, w = 0, int(H * s / 2), 0, int(W * s)
    subprocess.run(["sips", "-c", str(h), str(w), "--cropOffset", str(top), str(left), png, "--out", png], capture_output=True)
    return png

def main():
    out = sys.argv[1]; pngs = []
    for i, a in enumerate(sys.argv[2:]):
        page = None
        if "@" in a: a, page = a.rsplit("@", 1); page = int(page)
        pngs.append(crop(pdf_of(a), i, a, page))
    # stack vertically with ImageMagick-free approach: use python zlib PNG writer via sips? simplest: html->no. Use 'sips' can't stack; use Swift.
    subprocess.run([os.path.join(CACHE, "stack"), out] + pngs, check=False)
if __name__ == "__main__": main()
