#!/usr/bin/env bash
# Prints breeze_version.hpp: FC_PLUGIN_VERSION from source/app.h, the version
# of the breeze.elf the app carries. $1 is the repository.
set -euo pipefail
version=$(sed -n 's/^#define FC_PLUGIN_VERSION "\(.*\)"$/\1/p' "$1/source/app.h")
[[ -n $version ]] || { echo "FC_PLUGIN_VERSION not found in source/app.h" >&2; exit 2; }
printf '// Generated from source/app.h: the version of the breeze.elf in assets/.\n'
printf '#pragma once\n#define BREEZE_BUNDLED_VERSION "%s"\n' "$version"
