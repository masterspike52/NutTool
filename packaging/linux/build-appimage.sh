#!/usr/bin/env bash
# Builds NutTool for Linux (linux-release preset) and packs it as out/NutTool-<version>-x86_64.AppImage.
# Needs VCPKG_ROOT, cmake, ninja, a C++20 compiler, nasm and the X11/Wayland development packages
# listed in README.md. appimagetool is downloaded to out/tools on first use.
set -euo pipefail
trap 'echo "build-appimage.sh: failed at line $LINENO (see the messages above)" >&2' ERR

root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
# Defaults: tools installed without root in ~/.local/bin, and vcpkg checked out in the home folder.
export PATH="$HOME/.local/bin:$PATH"
if [ -z "${VCPKG_ROOT:-}" ] && [ -x "$HOME/vcpkg/vcpkg" ]; then
    export VCPKG_ROOT="$HOME/vcpkg"
fi
: "${VCPKG_ROOT:?set VCPKG_ROOT to your vcpkg checkout}"

echo "==> Building (linux-release)"
cmake --preset linux-release
cmake --build --preset linux-release

echo "==> Assembling out/AppDir"
appdir=out/AppDir
rm -rf "$appdir"
cmake --install out/build/linux-release --prefix "$appdir/usr" --strip
install -m 755 packaging/linux/AppRun "$appdir/AppRun"
cp packaging/linux/nuttool.desktop packaging/linux/nuttool.png "$appdir/"
ln -s nuttool.png "$appdir/.DirIcon"

tool=out/tools/appimagetool-x86_64.AppImage
if [ ! -x "$tool" ]; then
    echo "==> Downloading appimagetool"
    mkdir -p out/tools
    curl -fL -o "$tool.part" https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
    chmod +x "$tool.part"
    mv "$tool.part" "$tool"
fi

version=$(sed -n 's/.*"version-string": *"\([^"]*\)".*/\1/p' vcpkg.json)
out=out/NutTool-$version-x86_64.AppImage
echo "==> Packing $out"
# Extract-and-run lets appimagetool work without FUSE (containers, WSL, CI).
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$tool" --no-appstream "$appdir" "$out"
echo "Built $out"
