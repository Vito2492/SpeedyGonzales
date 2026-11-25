#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APPDIR="$ROOT/build/SpeedyArriba.AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/scalable/apps"

make -C "$ROOT" clean all
cp "$ROOT/speedy" "$APPDIR/usr/bin/speedy"
if [ -f "$ROOT/snes9x_libretro.so" ]; then
  cp "$ROOT/snes9x_libretro.so" "$APPDIR/usr/bin/snes9x_libretro.so"
fi

cp "$ROOT/packaging/linux/speedy-arriba.desktop" "$APPDIR/speedy-arriba.desktop"
cp "$ROOT/packaging/linux/speedy-arriba.desktop" "$APPDIR/usr/share/applications/speedy-arriba.desktop"
cp "$ROOT/packaging/linux/speedy-arriba.svg" "$APPDIR/speedy-arriba.svg"
cp "$ROOT/packaging/linux/speedy-arriba.svg" "$APPDIR/usr/share/icons/hicolor/scalable/apps/speedy-arriba.svg"

cat > "$APPDIR/AppRun" <<'EOF'
#!/usr/bin/env bash
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/lib:${LD_LIBRARY_PATH:-}"
exec "$HERE/usr/bin/speedy" "$@"
EOF
chmod +x "$APPDIR/AppRun"

if command -v appimagetool >/dev/null 2>&1; then
  ARCH=x86_64 appimagetool "$APPDIR" "$ROOT/SpeedyArriba-x86_64.AppImage"
else
  echo "AppDir prepared at $APPDIR (install appimagetool to pack .AppImage)"
fi
