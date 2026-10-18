# Tools

| path | what it does |
| --- | --- |
| `kicad_checklist.py` | Builds the KiCad coverage checklist (names, packages and datasheet links only) and reports which KiCad symbols still have no part here. |
| `partgen/` | Scripts that generate some `parts/*.txt` files (logic families) and `verify_ti.py`, which compares a parts file against the pin tables in TI datasheets. |
| `datasheets/` | Research helpers: `ds.py` downloads a datasheet (cached) and prints its pin section; `pinimg.py` / `colcrop.sh` crop pinout drawings from several datasheets into one image for checking by eye; `srcs.py` finds a working datasheet link. |

Every pinout in `parts/` is checked against the manufacturer's datasheet, never copied from another EDA library.

Setup (macOS; needs poppler for `pdftotext` / `pdftoppm`):

```sh
git clone --depth 1 https://gitlab.com/kicad/libraries/kicad-symbols.git /tmp/kicad-symbols
python3 tools/kicad_checklist.py /tmp/kicad-symbols > build/kicad_checklist.tsv
python3 tools/kicad_checklist.py /tmp/kicad-symbols --coverage
mkdir -p ~/.cache/designElectronics-datasheets
swiftc -O tools/datasheets/stack.swift -o ~/.cache/designElectronics-datasheets/stack
python3 tools/partgen/logic74.py parts/logic_74xx.txt
python3 tools/partgen/verify_ti.py parts/logic_74xx.txt
```

`KICAD_CHECKLIST` and `DS_CACHE` override the checklist and datasheet-cache locations.
