#!/bin/bash
set -e

BUILD_DIR="${1:-build}"
PKG_DIR="$BUILD_DIR/package"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

mkdir -p "$PKG_DIR/addons/sourcemod/extensions"
mkdir -p "$PKG_DIR/addons/sourcemod/gamedata"
mkdir -p "$PKG_DIR/addons/sourcemod/scripting/include"
mkdir -p "$PKG_DIR/cfg/sourcemod"

find "$BUILD_DIR" -name "momsurffix_ext.ext.so" -exec cp {} "$PKG_DIR/addons/sourcemod/extensions/" \;
cp "$SCRIPT_DIR/gamedata/momsurffix_fix.games.txt" "$PKG_DIR/addons/sourcemod/gamedata/"
cp "$SCRIPT_DIR/momsurffix.cfg"                    "$PKG_DIR/cfg/sourcemod/"
cp "$SCRIPT_DIR/include/momsurffix_ext.inc"        "$PKG_DIR/addons/sourcemod/scripting/include/"

echo "Done. Package at: $PKG_DIR"
