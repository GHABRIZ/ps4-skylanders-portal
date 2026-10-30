#!/bin/sh
# Runs the plugin on a Linux PC against a fake "game" and checks it end to end.
# Usage: test/run_host_sim.sh /path/to/folder/with/bins   (needs cc, python3, curl)
set -e
cd "$(dirname "$0")/.."
SRC="$1"
[ -d "$SRC" ] || { echo "usage: $0 <folder with .bin dumps>"; exit 2; }
python3 tools/gen_web.py >/dev/null
ROOT=$(mktemp -d)
FIGS="$ROOT/data/GoldHEN/skylanders/figures"
mkdir -p "$FIGS/0 Test" "$FIGS/More"
FIRST=$(find "$SRC" -name '*.bin' -size 1024c | head -1)
cp "$FIRST" "$FIGS/0 Test/First.bin"
find "$SRC" -name '*.bin' -size 1024c | head -12 | while read -r f; do cp "$f" "$FIGS/More/"; done
cc -O1 -g -Wall -Wno-unused-function -Wno-unused-variable -pthread -Itest/fake -Isource \
   -o build_host_sim test/host_sim.c source/portal.c
./build_host_sim "$ROOT" "$FIGS/0 Test/First.bin"
