#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the minimal FFmpeg + gst-libav that dist/ ships instead of MSYS2's
# full FFmpeg (which drags ~150 MB of encoders/codecs uxplay never uses).
# Run in the MSYS2 UCRT64 shell; build-all.sh calls it. Skips work already done.
#
# Needs: pacman -S make diffutils mingw-w64-ucrt-x86_64-{gcc,nasm,meson,pkgconf,gst-plugins-base}
set -euo pipefail

SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
D="$SRC/deps"
PREFIX="$D/prefix"
FFMPEG_VER=9.1          # must match MSYS2's gst-libav ABI (avcodec-63)
GSTLIBAV_VER=1.28.9     # must match the installed GStreamer version
# The stamp carries a hash of this script, so any change to it (versions, flags,
# copied files) triggers a rebuild instead of silently reusing an older prefix.
STAMP="$PREFIX/.built-$(sha256sum "${BASH_SOURCE[0]}" | cut -c1-16)"
JOBS="$(nproc)"

# Pinned source archives. The FFmpeg hash was taken from a tarball whose GPG
# signature verified against the FFmpeg release key FCF986EA15E6E293A5644F10B4322F04D67658D8;
# the gst-libav hash matches the .sha256sum published next to the tarball.
SOURCES=(
  "ffmpeg-$FFMPEG_VER.tar.xz|https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VER.tar.xz|1a4d394ae63d190a526cd052eb4415c156b5c5e2df37efab41a7de0b8fde2433"
  "gst-libav-$GSTLIBAV_VER.tar.xz|https://gstreamer.freedesktop.org/src/gst-libav/gst-libav-$GSTLIBAV_VER.tar.xz|4b3df21fa70972a80891d2298cf32c14a62c3db4483d545c7ac018230399186b"
)

if [ -f "$STAMP" ]; then
  echo ">> deps up to date ($(basename "$STAMP"))"
  exit 0
fi

mkdir -p "$D/src"
for entry in "${SOURCES[@]}"; do
  IFS='|' read -r file url sha <<< "$entry"
  if [ ! -f "$D/src/$file" ]; then
    echo ">> download $url"
    curl -fL --retry 3 -o "$D/src/$file.part" "$url"
    mv "$D/src/$file.part" "$D/src/$file"
  fi
  echo "$sha *$D/src/$file" | sha256sum -c --quiet - \
    || { echo "checksum mismatch for $file — delete it and retry" >&2; exit 1; }
done

rm -rf "$D/build" "$PREFIX"
mkdir -p "$D/build" "$PREFIX"

# Only what uxplay feeds to libav: AAC-ELD/AAC (avdec_aac), ALAC (avdec_alac) and
# software fallback for H.264/H.265 when no GPU decoder is available. No external
# libraries, no --enable-gpl: the result is LGPL-2.1+.
#
# FFmpeg embeds its whole configure line in the DLLs (avcodec_configuration()), so
# the prefix is the neutral "/" and the files are placed with DESTDIR — a real
# prefix would publish the builder's local path (user name) inside every DLL.
FFMPEG_CONFIGURE=(
  --prefix=/
  --enable-shared --disable-static
  --disable-programs --disable-doc --disable-debug
  --disable-autodetect --disable-network
  --disable-avdevice --disable-swscale --disable-swresample
  --disable-everything
  --enable-decoder=aac,aac_latm,alac,h264,hevc
  --enable-parser=aac,aac_latm,h264,hevc
)
echo ">> FFmpeg $FFMPEG_VER: ${FFMPEG_CONFIGURE[*]}"
tar -xJf "$D/src/ffmpeg-$FFMPEG_VER.tar.xz" -C "$D/build"
cd "$D/build/ffmpeg-$FFMPEG_VER"
./configure "${FFMPEG_CONFIGURE[@]}" > "$D/build/ffmpeg-configure.log"
make -j"$JOBS" > "$D/build/ffmpeg-make.log" 2>&1
make install DESTDIR="$PREFIX" > /dev/null
# point the pkg-config files at the real location for the gst-libav build below
PFX_M="$(cygpath -m "$PREFIX")"
sed -i -e "s|^prefix=.*|prefix=$PFX_M|" -e 's|^exec_prefix=.*|exec_prefix=${prefix}|' \
       -e 's|^libdir=.*|libdir=${prefix}/lib|' -e 's|^includedir=.*|includedir=${prefix}/include|' \
       "$PREFIX"/lib/pkgconfig/*.pc
# license texts + exact build configuration, shipped in dist/licenses (LGPL)
mkdir -p "$PREFIX/share/licenses/ffmpeg"
cp LICENSE.md COPYING.LGPLv2.1 COPYING.LGPLv3 COPYING.GPLv2 COPYING.GPLv3 "$PREFIX/share/licenses/ffmpeg/"
printf '%s\n' "${FFMPEG_CONFIGURE[@]}" > "$PREFIX/share/licenses/ffmpeg/configure-flags.txt"

echo ">> gst-libav $GSTLIBAV_VER against that FFmpeg"
tar -xJf "$D/src/gst-libav-$GSTLIBAV_VER.tar.xz" -C "$D/build"
cd "$D/build"
PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}" \
  meson setup "gst-libav-$GSTLIBAV_VER/build" "gst-libav-$GSTLIBAV_VER" \
    --prefix="$PREFIX" --buildtype=release -Ddoc=disabled -Dtests=disabled > "$D/build/gst-libav-setup.log"
meson compile -C "gst-libav-$GSTLIBAV_VER/build" > "$D/build/gst-libav-compile.log" 2>&1
meson install -C "gst-libav-$GSTLIBAV_VER/build" > /dev/null
mkdir -p "$PREFIX/share/licenses/gst-libav"
cp "gst-libav-$GSTLIBAV_VER/COPYING" "$PREFIX/share/licenses/gst-libav/"

touch "$STAMP"
echo ">> deps done: $PREFIX"
