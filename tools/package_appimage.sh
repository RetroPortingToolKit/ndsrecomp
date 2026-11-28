#!/usr/bin/env bash
# Source after a title has fully staged and audited its AppDir.
nds_package_appimage() {
  local output="$1"; shift
  rm -f -- "$output"
  ARCH=x86_64 "$APPIMAGETOOL_BIN" --appimage-extract-and-run "$@" "$APPDIR" "$output" >/dev/null
  chmod +x "$output"
}
