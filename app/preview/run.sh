#!/usr/bin/env bash
# Renders the app's screens on a PC (Linux/WSL, clang++) with sample data, as
# PNGs in $1 (default build/app-preview): the same drawing code as on the PS5.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
app=$(dirname -- "$here")
repo=$(dirname -- "$app")
out=${1:-$repo/build/app-preview}
mkdir -p "$out"
obj=$(mktemp -d)
trap 'rm -rf "$obj"' EXIT

bash "$here/version.sh" "$repo" > "$obj/breeze_version.hpp"
flags=(-DBREEZE_PREVIEW -O2 -Wall -Wextra -I"$app/src" -I"$repo/source" -I"$obj")
clang -std=c11 "${flags[@]}" -c "$app/src/stb_impl.c" -o "$obj/stb.o"
clang -std=c11 "${flags[@]}" -c "$repo/source/curve.c" -o "$obj/curve.o"
for f in font screen ui views main link; do
    clang++ -std=c++20 -fno-exceptions -fno-rtti "${flags[@]}" -c "$app/src/$f.cpp" -o "$obj/$f.o"
done
clang++ -std=c++20 -fno-exceptions -fno-rtti "${flags[@]}" -c "$here/preview.cpp" -o "$obj/preview.o"
clang++ "$obj"/*.o -lm -o "$obj/preview"
"$obj/preview" "$app/fonts/" "$out"
python3 - "$out" <<'PY'
import glob, os, sys
from PIL import Image
for p in sorted(glob.glob(os.path.join(sys.argv[1], "*.ppm"))):
    Image.open(p).save(p[:-4] + ".png")
    os.remove(p)
PY
ls "$out"
