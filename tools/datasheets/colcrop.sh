#!/bin/bash
# colcrop.sh OUT.png PAGE X0 Y0 W H URL... : crop the same page region (fractions of the page) from each datasheet at 150 dpi and stack them
out=$1; page=$2; fx=$3; fy=$4; fw=$5; fh=$6; shift 6
D=${DS_CACHE:-$HOME/.cache/designElectronics-datasheets}; mkdir -p "$D"; files=()
i=0
for u in "$@"; do
  [[ $u == ti:* ]] && u="https://www.ti.com/lit/ds/symlink/${u#ti:}.pdf"
  python3 -I -c "
import importlib.util;spec=importlib.util.spec_from_file_location('ds','$(dirname "$0")/ds.py');ds=importlib.util.module_from_spec(spec);spec.loader.exec_module(ds);ds.fetch('$u')"
  h=$(python3 -c "import hashlib;print(hashlib.sha1(b'$u').hexdigest()[:16])")
  pdftoppm -r 150 -f $page -l $page -png -singlefile "$D/$h.pdf" "$D/col$i" 2>/dev/null || continue
  W=$(sips -g pixelWidth "$D/col$i.png" | awk '/pixelWidth/{print $2}'); H=$(sips -g pixelHeight "$D/col$i.png" | awk '/pixelHeight/{print $2}')
  sips -c $(python3 -c "print(int($H*$fh))") $(python3 -c "print(int($W*$fw))") --cropOffset $(python3 -c "print(int($H*$fy))") $(python3 -c "print(int($W*$fx))") "$D/col$i.png" --out "$D/col$i.png" >/dev/null
  files+=("$D/col$i.png"); i=$((i+1))
done
COLS=${COLS:-4} "$D/stack" "$out" "${files[@]}"
