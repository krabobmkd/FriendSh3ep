#!/bin/sh
# make_installer_pkg.sh
#
# Assembles the FriendSh3ep installer package directory from the built
# binary and project sources. Run this on Linux after a successful
# cmake --build build.
#
# Usage:
#   ./make_installer_pkg.sh [output_dir]
#
# Default output: ./FriendSh3ep_pkg  (deleted and recreated each run)
#
# The resulting directory can be archived as an LHA for distribution:
#   cd FriendSh3ep_pkg && lha a ../FriendSh3ep_0.8.lha *
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PKG="${1:-$SCRIPT_DIR/FriendSh3ep_pkg}"

BUILD="$SCRIPT_DIR/build"
INSTALLER_SRC="$SCRIPT_DIR/Installer"
THEMES_SRC="$SCRIPT_DIR/themes"

# ---------------------------------------------------------------------------
# Sanity checks
# ---------------------------------------------------------------------------
check_file() {
    if [ ! -f "$1" ]; then
        echo "ERROR: required file not found: $1" >&2
        echo "       Run 'cmake --build build' first." >&2
        exit 1
    fi
}

check_file "$BUILD/FriendSh3ep"
check_file "$SCRIPT_DIR/FriendSh3ep.info"
check_file "$SCRIPT_DIR/FriendSh3ep.guide"
check_file "$SCRIPT_DIR/FriendSh3ep.readme"
check_file "$SCRIPT_DIR/LICENSE"
check_file "$INSTALLER_SRC/Install"
check_file "$INSTALLER_SRC/Install.info"
check_file "$INSTALLER_SRC/FriendSh3ep.guide.info"
check_file "$INSTALLER_SRC/FriendSh3ep.readme.info"

if [ ! -d "$THEMES_SRC" ]; then
    echo "ERROR: themes directory not found: $THEMES_SRC" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# (Re)create package directory tree
# ---------------------------------------------------------------------------
echo "Creating package directory: $PKG"
rm -rf "$PKG"
mkdir -p "$PKG/bin"

# ---------------------------------------------------------------------------
# Installer script and icon
# ---------------------------------------------------------------------------
echo "Copying installer..."
cp "$INSTALLER_SRC/Install"      "$PKG/Install"
cp "$INSTALLER_SRC/Install.info" "$PKG/Install.info"

# ---------------------------------------------------------------------------
# Application binary and icon
# ---------------------------------------------------------------------------
echo "Copying FriendSh3ep binary and icon..."
cp "$BUILD/FriendSh3ep"          "$PKG/bin/FriendSh3ep"
cp "$SCRIPT_DIR/FriendSh3ep.info" "$PKG/bin/FriendSh3ep.info"

# ---------------------------------------------------------------------------
# Themes (skip Amiga-FS metadata *.uaem sidecar files)
# ---------------------------------------------------------------------------
echo "Copying themes..."
(cd "$THEMES_SRC" && find . -not -name "*.uaem") | while IFS= read -r f; do
    src="$THEMES_SRC/$f"
    dst="$PKG/themes/$f"
    if [ -d "$src" ]; then
        mkdir -p "$dst"
    else
        mkdir -p "$(dirname "$dst")"
        cp "$src" "$dst"
    fi
done

# ---------------------------------------------------------------------------
# Documentation (guide/readme + their icons, side by side at package root --
# the Install script's (infos) copyfiles option picks up the matching
# "<name>.info" automatically from here, same convention as EmojiGear's own
# Install script)
# ---------------------------------------------------------------------------
echo "Copying documentation..."
cp "$SCRIPT_DIR/FriendSh3ep.guide"            "$PKG/FriendSh3ep.guide"
cp "$INSTALLER_SRC/FriendSh3ep.guide.info"   "$PKG/FriendSh3ep.guide.info"
cp "$SCRIPT_DIR/FriendSh3ep.readme"           "$PKG/FriendSh3ep.readme"
cp "$INSTALLER_SRC/FriendSh3ep.readme.info"  "$PKG/FriendSh3ep.readme.info"
cp "$SCRIPT_DIR/LICENSE"                      "$PKG/LICENSE"

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo ""
echo "Package ready in: $PKG"
echo ""
echo "Contents:"
find "$PKG" -not -type d | sort | sed "s|$PKG/||"
echo ""
echo "To create an LHA archive:"
echo "  cd \"$PKG\" && lha a ../FriendSh3ep_0.8.lha *"
