#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Full build: uxplay.exe + minimal FFmpeg/gst-libav + KagamiCast.exe + dist/
# + installer + release files. Run in the MSYS2 UCRT64 shell:  bash build-all.sh
#
# Output:  build/   intermediate build trees
#          dist/    portable program folder
#          build/release/  files to attach to a GitHub release
set -euo pipefail

SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SRC"
# shellcheck source=product.conf
source "$SRC/product.conf"
APP_VERSION="$(sed -n 's/^project(KagamiCast VERSION \([0-9.]*\).*/\1/p' app/CMakeLists.txt)"
echo ">> $APP_DISPLAY_NAME $APP_VERSION"

[ -f UxPlay/CMakeLists.txt ] || { echo "UxPlay/ is empty — run: git submodule update --init" >&2; exit 1; }

echo ">> Bonjour import library (generated from bonjour-sdk/Lib/x64/dnssd.def)"
# uxplay loads dnssd.dll at run time, but its CMake build expects the SDK layout.
dlltool -d bonjour-sdk/Lib/x64/dnssd.def -l bonjour-sdk/Lib/x64/dnssd.lib

echo ">> uxplay (UxPlay, upstream source)"
# No CMAKE_BUILD_TYPE on purpose — build it the way UxPlay does: its CMakeLists
# adds -O2 itself and expects assert() to stay active. A Release build adds
# -DNDEBUG, which removes every assert, including one that contains a required
# call (pairing_session_make_nonce in lib/raop_handlers.h, used by -pw).
#
# -Wno-alloc-size-larger-than: gcc flags utils_data_to_string() (lib/utils.c), where
# "int len = 3*datalen + 1" could overflow for datalen > ~715 million. Every caller
# passes a fixed size or a packet length capped at RAOP_PACKET_LEN (32768), almost
# all of them only under "-d" debug logging, so it cannot happen. Silenced here
# instead of patching the upstream source.
#
# -ffile-prefix-map: assert() and logging embed __FILE__; without the map that is
# the absolute source path, i.e. the builder's user name ends up in uxplay.exe.
export BONJOUR_SDK_HOME="$(cygpath -m "$SRC/bonjour-sdk")"
PREFIX_MAP="-ffile-prefix-map=$(cygpath -m "$SRC")/="
cmake -S UxPlay -B build/uxplay -G Ninja -DCMAKE_BUILD_TYPE= \
      -DCMAKE_C_FLAGS="-Wno-alloc-size-larger-than $PREFIX_MAP" -DCMAKE_CXX_FLAGS="$PREFIX_MAP"
cmake --build build/uxplay

echo ">> minimal FFmpeg + gst-libav (skipped when already built)"
bash "$SRC/build-deps.sh"

echo ">> $APP_DISPLAY_NAME GUI (Release, set by app/CMakeLists.txt)"
cmake -S app -B build/app -G Ninja -DAPP_DISPLAY_NAME="$APP_DISPLAY_NAME" -DCMAKE_CXX_FLAGS="$PREFIX_MAP"
cmake --build build/app

echo ">> OBS plugin (MSVC; skipped when Visual Studio with C++ is not installed)"
# OBS plugins must be built with MSVC against OBS's own SDK, so this step uses
# Visual Studio's CMake, not MSYS2's. package.sh copies the result to dist/obs-plugin.
OBS_STAGE="$SRC/build/obs-plugin-stage"
rm -rf "$OBS_STAGE"
VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
VS_DIR=""; VS_MAJOR=""
if [ -f "$VSWHERE" ]; then
  VS_QUERY=(-latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64)
  VS_DIR="$("$VSWHERE" "${VS_QUERY[@]}" -property installationPath | tr -d '\r')"
  VS_MAJOR="$("$VSWHERE" "${VS_QUERY[@]}" -property installationVersion | tr -d '\r' | cut -d. -f1)"
fi
VS_CMAKE="${VS_DIR:+$(cygpath -u "$VS_DIR")/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe}"
if [ -n "$VS_CMAKE" ] && [ -f "$VS_CMAKE" ]; then
  case "$VS_MAJOR" in
    18) OBS_PRESET=windows-x64-vs2026 ;;   # Visual Studio 2026
    *)  OBS_PRESET=windows-x64 ;;          # Visual Studio 2022 (the template's preset)
  esac
  # first run downloads the OBS sources/deps pinned in obs-plugin/buildspec.json
  ( cd "$SRC/obs-plugin"
    "$VS_CMAKE" --preset "$OBS_PRESET" >/dev/null
    "$VS_CMAKE" --build --preset "$OBS_PRESET"
    "$VS_CMAKE" --install build_x64 --config RelWithDebInfo --prefix "$(cygpath -m "$OBS_STAGE")" >/dev/null )
else
  echo "   Visual Studio with the C++ workload not found — dist/ will have no OBS plugin"
fi

bash "$SRC/package.sh"

echo ">> installer (Inno Setup)"
# set ISCC=/path/to/ISCC.exe to override; otherwise look in the per-user and
# per-machine install locations (cygpath -F 28 = the user's Local AppData folder)
for c in "${ISCC:-}" "$(cygpath -F 28)/Programs/Inno Setup 6/ISCC.exe" \
         "/c/Program Files (x86)/Inno Setup 6/ISCC.exe" "/c/Program Files/Inno Setup 6/ISCC.exe"; do
  [ -n "$c" ] && [ -f "$c" ] && { ISCC="$c"; break; }
  ISCC=""
done
REL="$SRC/build/release"
rm -rf "$REL"; mkdir -p "$REL"
if [ -n "$ISCC" ]; then
  # "-" switches, not "/": MSYS2 rewrites arguments that start with "/" as paths
  "$ISCC" -Q \
    "-DAppName=$APP_DISPLAY_NAME" "-DAppVersion=$APP_VERSION" \
    "-DAppPublisher=$APP_PUBLISHER" "-DAppURL=$APP_URL" "-DAppId=$APP_INSTALLER_ID" \
    "-DDistDir=$(cygpath -w "$SRC/dist")" "-DOutputDir=$(cygpath -w "$REL")" \
    "$(cygpath -w "$SRC/installer/setup.iss")"
else
  echo "   Inno Setup 6 not found (winget install JRSoftware.InnoSetup) — installer skipped"
fi

echo ">> release files"
# portable zip with a versioned top folder. .NET's ZipFile (via Windows PowerShell,
# present on every Windows 10/11) handles the Thai file name; Windows' tar.exe crashes on it.
STAGE="$SRC/build/zip-stage"
rm -rf "$STAGE"; mkdir -p "$STAGE"
cp -r "$SRC/dist" "$STAGE/KagamiCast-$APP_VERSION"
/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe -NoProfile -Command \
  "[AppContext]::SetSwitch('Switch.System.IO.Compression.ZipFile.UseBackslash', \$false); Add-Type -AssemblyName System.IO.Compression.FileSystem; [IO.Compression.ZipFile]::CreateFromDirectory('$(cygpath -w "$STAGE/KagamiCast-$APP_VERSION")', '$(cygpath -w "$REL/KagamiCast-$APP_VERSION-portable.zip")', [IO.Compression.CompressionLevel]::Optimal, \$true)"
rm -rf "$STAGE"
# source archives of the components built here (LGPL: ship the corresponding source)
cp deps/src/ffmpeg-*.tar.xz deps/src/gst-libav-*.tar.xz "$REL/"
(cd "$REL" && sha256sum -- * > SHA256SUMS.txt)
ls -la "$REL"
