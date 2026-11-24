#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build a portable dist/ that runs on another Windows PC without MSYS2.
# Run inside the MSYS2 UCRT64 shell:  bash package.sh
# (normally via build-all.sh, which first builds uxplay, deps/ and the GUI)
set -euo pipefail

SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"   # repository root
DIST="$SRC/dist"
UCRT="/ucrt64"
DEPS="$SRC/deps/prefix"                                 # minimal FFmpeg + gst-libav (build-deps.sh)
APP_EXE="$SRC/build/app/KagamiCast.exe"
UXPLAY_EXE="$SRC/build/uxplay/uxplay.exe"
# shellcheck source=product.conf
source "$SRC/product.conf"
APP_VERSION="$(sed -n 's/^project(KagamiCast VERSION \([0-9.]*\).*/\1/p' "$SRC/app/CMakeLists.txt")"

# GStreamer plugins uxplay actually uses (everything else is ~50 MB of dead weight):
#   pipeline   app coreelements typefindfunctions playback(decodebin) autodetect
#              videoparsersbad(h264parse/h265parse) videoconvertscale videofilter(videoflip)
#              audioconvert audioresample volume level
#   decoders   libav (avdec_aac/avdec_alac + software H.264/H.265, from deps/) and every
#              GPU decoder family, since decodebin picks one per machine at run time:
#              d3d11 d3d12 nvcodec qsv amfcodec mediafoundation va
#   sinks      d3d11/d3d12 (+ opengl fallback for autovideosink), wasapi wasapi2 directsound
# Add a plugin here if a new uxplay option needs it (e.g. -hls, -ca, -mp4).
PLUGINS="app coreelements typefindfunctions playback autodetect videoparsersbad
 videoconvertscale videofilter audioconvert audioresample volume level
 d3d11 d3d12 nvcodec qsv amfcodec mediafoundation va opengl
 wasapi wasapi2 directsound"

for f in "$APP_EXE" "$UXPLAY_EXE" "$DEPS/lib/gstreamer-1.0/libgstlibav.dll"; do
  [ -f "$f" ] || { echo "missing $f — run build-all.sh" >&2; exit 1; }
done

echo ">> clean dist"
rm -rf "$DIST"
mkdir -p "$DIST" "$DIST/lib/gstreamer-1.0" "$DIST/libexec/gstreamer-1.0"

echo ">> copy executables"
cp "$APP_EXE" "$UXPLAY_EXE" "$DIST/"

echo ">> deploy Qt (windeployqt)"
# Plain Qt Widgets (raster): no Direct3D shader compilers; the "generic" input
# plugins (TUIO touch) would drag in Qt6Network + tls + networkinformation.
windeployqt6 --release --no-translations --no-opengl-sw \
  --no-system-d3d-compiler --no-system-dxc-compiler \
  --skip-plugin-types generic,networkinformation,tls \
  --dir "$DIST" "$DIST/KagamiCast.exe" >/dev/null

echo ">> copy GStreamer plugins (whitelist) + scanner"
for p in $PLUGINS; do
  cp "$UCRT/lib/gstreamer-1.0/libgst$p.dll" "$DIST/lib/gstreamer-1.0/"
done
cp "$DEPS/lib/gstreamer-1.0/libgstlibav.dll" "$DIST/lib/gstreamer-1.0/"
cp "$UCRT/libexec/gstreamer-1.0/gst-plugin-scanner.exe" "$DIST/libexec/gstreamer-1.0/"

echo ">> minimal FFmpeg (LGPL) from deps/"
cp "$DEPS"/bin/av*.dll "$DIST/"

echo ">> resolve DLL dependencies (objdump import tables)"
# For every exe/dll in dist, read its imports; a DLL already in dist wins, one
# found in /ucrt64/bin is copied, anything else is a Windows system DLL (or
# Bonjour's dnssd.dll, loaded at run time and intentionally NOT bundled).
# (ldd is not used: it resolves through PATH and would pull MSYS2's full FFmpeg
#  back in, because those DLLs have the same names as the minimal ones above.)
declare -A IN_DIST UCRT_BIN
for f in "$DIST"/*; do b="$(basename "$f")"; IN_DIST["${b,,}"]=1; done
for f in "$UCRT"/bin/*.dll; do b="$(basename "$f")"; UCRT_BIN["${b,,}"]="$b"; done
pass=0
while : ; do
  pass=$((pass + 1)); added=0
  while IFS= read -r dll; do
    key="${dll,,}"
    [ -n "${IN_DIST[$key]:-}" ] && continue
    src="${UCRT_BIN[$key]:-}"
    [ -n "$src" ] || continue
    cp "$UCRT/bin/$src" "$DIST/"
    IN_DIST[$key]=1; added=$((added + 1))
  done < <(find "$DIST" -type f \( -iname '*.dll' -o -iname '*.exe' \) -print0 \
             | xargs -0 objdump -p 2>/dev/null | sed -n 's/^[[:space:]]*DLL Name: //p' | sort -u)
  echo "   pass $pass: +$added DLLs"
  [ "$added" -eq 0 ] && break
done

echo ">> licenses + THIRD-PARTY-NOTICES.txt"
LIC="$DIST/licenses"
NOTICE="$DIST/THIRD-PARTY-NOTICES.txt"
mkdir -p "$LIC"
cp "$SRC/LICENSE" "$DIST/LICENSE.txt"
UXPLAY_COMMIT="$(git -C "$SRC/UxPlay" rev-parse HEAD 2>/dev/null || echo unknown)"
FFMPEG_VER="$(sed -n 's/^FFMPEG_VER=\([0-9.]*\).*/\1/p' "$SRC/build-deps.sh")"
GSTLIBAV_VER="$(sed -n 's/^GSTLIBAV_VER=\([0-9.]*\).*/\1/p' "$SRC/build-deps.sh")"
mkdir -p "$LIC/uxplay" "$LIC/ffmpeg" "$LIC/gst-libav"
cp "$SRC/UxPlay/LICENSE" "$LIC/uxplay/LICENSE"
cp "$SRC/UxPlay/lib/playfair/LICENSE.md" "$LIC/uxplay/playfair-LICENSE.md"
cp "$SRC/UxPlay/lib/llhttp/LICENSE-MIT" "$LIC/uxplay/llhttp-LICENSE-MIT"
cp "$DEPS/share/licenses/ffmpeg/"* "$LIC/ffmpeg/"
cp "$DEPS/share/licenses/gst-libav/"* "$LIC/gst-libav/"

# which MSYS2 package each bundled binary came from
declare -A PKG_FILES
while IFS= read -r rel; do
  b="$(basename "$rel")"
  for cand in "$UCRT/bin/$b" "$UCRT/lib/gstreamer-1.0/$b" "$UCRT/libexec/gstreamer-1.0/$b" "$UCRT/share/qt6/plugins/$rel"; do
    if [ -f "$cand" ]; then
      pkg="$(pacman -Qoq "$cand" 2>/dev/null)" && PKG_FILES["$pkg"]+="$rel "
      break
    fi
  done
done < <(cd "$DIST" && find . -type f \( -iname '*.dll' -o -iname '*.exe' \) | sed 's|^\./||' \
           | grep -vE '^(KagamiCast|uxplay)\.exe$|^av[a-z]+-[0-9]+\.dll$|/libgstlibav\.dll$' | sort)

{
  cat <<EOF
$APP_DISPLAY_NAME $APP_VERSION — third-party components
$(printf '%*s' 60 '' | tr ' ' '=')

$APP_DISPLAY_NAME itself is free software under the GNU General Public License,
version 3 or later (LICENSE.txt). It bundles the components below; each keeps
its own license. License texts are in the "licenses" folder: one sub-folder per
component where the upstream package provides its texts, plus the GPL/LGPL texts
in licenses/ffmpeg (COPYING.GPLv2, COPYING.GPLv3, COPYING.LGPLv2.1, COPYING.LGPLv3).

Corresponding source code:
- $APP_DISPLAY_NAME and the build scripts: the project's source repository.
- UxPlay: https://github.com/FDH2/UxPlay at commit $UXPLAY_COMMIT (unmodified;
  a git submodule of the source repository).
- FFmpeg $FFMPEG_VER and gst-libav $GSTLIBAV_VER: built from the source archives attached
  to every release (see licenses/ffmpeg/configure-flags.txt for the configuration).
- Everything else: unmodified MSYS2 UCRT64 packages of the versions listed below;
  build recipes at https://github.com/msys2/MINGW-packages, upstream sources at the
  project URLs listed. Apple Bonjour is NOT included (installed separately).

------------------------------------------------------------------------------
uxplay.exe — UxPlay (AirPlay mirroring server)
  License : GPL-3.0-or-later (includes llhttp, MIT; playfair, GPL-3.0)
  Source  : https://github.com/FDH2/UxPlay  commit $UXPLAY_COMMIT
  Texts   : licenses/uxplay/

avcodec/avformat/avfilter/avutil DLLs — FFmpeg $FFMPEG_VER (decoders aac, aac_latm, alac, h264, hevc only)
  License : LGPL-2.1-or-later (built without --enable-gpl, no external libraries)
  Source  : https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VER.tar.xz
  Texts   : licenses/ffmpeg/

lib/gstreamer-1.0/libgstlibav.dll — gst-libav $GSTLIBAV_VER (built against the FFmpeg above)
  License : LGPL-2.1-or-later
  Source  : https://gstreamer.freedesktop.org/src/gst-libav/gst-libav-$GSTLIBAV_VER.tar.xz
  Texts   : licenses/gst-libav/
EOF
  for pkg in $(printf '%s\n' "${!PKG_FILES[@]}" | sort); do
    short="${pkg#mingw-w64-ucrt-x86_64-}"
    ver="$(pacman -Q "$pkg" | cut -d' ' -f2)"
    url="$(pacman -Qi "$pkg" | sed -n 's/^URL *: //p')"
    lic="$(pacman -Qi "$pkg" | sed -n 's/^Licenses *: //p')"
    # license texts: from the MSYS2 package, else from licenses-extra/ in this
    # repository (e.g. ICU), else the standard GPL/LGPL texts in licenses/ffmpeg
    texts="(standard GPL/LGPL text, see licenses/ffmpeg)"
    for ldir in "$UCRT/share/licenses/$short" "$SRC/licenses-extra/$short"; do
      if [ -d "$ldir" ]; then
        mkdir -p "$LIC/$short"; cp -r "$ldir/." "$LIC/$short/"
        texts="licenses/$short/"
        break
      fi
    done
    printf '\n%s %s (MSYS2 package %s)\n  License : %s\n  Source  : %s\n  Texts   : %s\n  Files   : %s\n' \
      "$short" "$ver" "$pkg" "$lic" "$url" "$texts" "$(echo ${PKG_FILES[$pkg]})"
  done
} > "$NOTICE"
echo "   $(grep -c '^  License' "$NOTICE") components listed"

echo ">> copy end-user scripts + README (dist-extras/)"
cp "$SRC/dist-extras/"* "$DIST/"

# Windows line endings for the text files users open with Notepad
for t in "$NOTICE" "$DIST/LICENSE.txt"; do sed -i 's/\r\?$/\r/' "$t"; done

echo ">> dist size"
echo "   $(find "$DIST" -type f | wc -l) files, $(du -sh "$DIST" | cut -f1)"
echo ">> done: $DIST"
