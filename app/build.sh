#!/usr/bin/env bash
# Builds the Breeze app, a PS5 title folder, with ps5-native-app-boilerplate
# (fetched at a pinned commit). Run in Linux or WSL after ./build.sh has made
# build/breeze.elf, which the app packages to start Breeze when it isn't running.
#
#   app/build.sh                    -> build/app/PPSA92878 and its ZIP
#   app/build.sh deploy <PS5 IP>    -> also copies it to /data/homebrew over FTP
#
# The kit's scripts don't take spaces in paths, so it is built in
# $BREEZE_APP_WORK (default ~/.cache/breeze-app), not in the repository.
set -euo pipefail

kit_url=https://github.com/blackbearreloaded/ps5-native-app-boilerplate.git
kit_rev=44ae1dab529654eac795d2a7c4f5a0425102b8d7

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(dirname -- "$here")
work=${BREEZE_APP_WORK:-$HOME/.cache/breeze-app}
kit=$work/kit

[[ -f $repo/build/breeze.elf ]] || {
    echo "build/breeze.elf is missing: run ./build.sh first" >&2
    exit 2
}

if [[ ! -d $kit/.git ]]; then
    mkdir -p "$work"
    git -c core.autocrlf=false -c core.eol=lf clone -q "$kit_url" "$kit"
fi
if [[ $(git -C "$kit" rev-parse HEAD) != "$kit_rev" ]]; then
    git -C "$kit" fetch -q origin
    git -C "$kit" checkout -q --detach "$kit_rev"
fi

# Breeze's files beside the kit's own (no snd0.at9: the app plays no music).
rm -rf "$kit/breeze"
mkdir -p "$kit/breeze/assets"
cp -r "$here/src" "$here/sce_sys" "$kit/breeze/"
# breeze.elf's own curve maths, so the app shows exactly what it steers to.
cp "$repo/source/curve.c" "$repo/source/curve.h" "$kit/breeze/src/"
cp "$repo/build/breeze.elf" "$kit/breeze/assets/breeze.elf"
# The web panel's fonts (SIL Open Font License, the licences go with them).
mkdir -p "$kit/breeze/assets/fonts"
cp "$here/fonts/"* "$kit/breeze/assets/fonts/"
bash "$kit/tools/validate-assets.sh" "$kit/breeze/sce_sys"

vars=(APP_SOURCE_DIR=breeze/src APP_PARAM=breeze/sce_sys/param.json
      APP_SCE_SYS=breeze/sce_sys APP_ASSETS=breeze/assets)
# One build: the kit's deploy target builds before it uploads, and two builds
# aren't byte-identical, so the copy below is always the one that was sent.
if [[ ${1:-} == deploy ]]; then
    [[ -n ${2:-} ]] || { echo "usage: app/build.sh deploy <PS5 IP>" >&2; exit 2; }
    make -C "$kit" "${vars[@]}" deploy PS5_HOST="$2"
else
    make -C "$kit" "${vars[@]}"
fi

title=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' \
    "$here/sce_sys/param.json")
rm -rf "$repo/build/app"
mkdir -p "$repo/build/app"
cp -r "$kit/dist/$title" "$kit/dist/$title.zip" "$repo/build/app/"
echo "App folder: build/app/$title"
