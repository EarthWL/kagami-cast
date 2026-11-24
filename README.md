# Kagami Cast

**Mirror your iPhone or iPad screen to a Windows PC — free and open source.**

Kagami Cast is a small Windows control panel around [UxPlay](https://github.com/FDH2/UxPlay),
the open-source AirPlay mirroring server. Pick a name and a quality preset, press
**Start**, then choose the PC under *Screen Mirroring* on the iPhone. Video and audio
play in their own window; the panel shows the connection status and log.

[ภาษาไทย](README.th.md)

## Features

- Screen mirroring with audio from iPhone/iPad (tested with an iPhone 17 Pro Max)
- Presets: balanced 1080p60, low latency, 4K H.265, and a maximum-compatibility mode
- GPU video decoding (Direct3D 11/12, NVIDIA, Intel, AMD), software fallback
- Optional PIN pairing so nobody else on the network can cast to your screen
- Installer sets up Windows Firewall (private networks by default) and Apple Bonjour
- No telemetry, no account, no ads

## Install

Download `KagamiCast-<version>-setup.exe` from the [Releases page](https://github.com/EarthWL/kagami-cast/releases) and run it.
It installs the program, adds firewall rules for the receiver and installs
Apple Bonjour via winget if it is missing. A portable zip is also provided.

The installer is not code-signed yet, so Windows SmartScreen may warn on first run
(*More info → Run anyway*).

**Requirements:** Windows 10/11 64-bit, the iPhone and PC on the same network, and
Apple Bonjour (the installer handles it). If the iPhone does not list the PC, set
the Wi-Fi network profile to *Private* in Windows settings.

## Build from source

Requires [MSYS2](https://www.msys2.org) (UCRT64) and, for the installer,
[Inno Setup 6](https://jrsoftware.org/isinfo.php).

```sh
# MSYS2 UCRT64 shell
pacman -S --needed make diffutils mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,nasm,meson,openssl,libplist,qt6-base,gstreamer,gst-plugins-base,gst-plugins-good,gst-plugins-bad}
git clone --recurse-submodules https://github.com/EarthWL/kagami-cast.git
cd kagami-cast
bash build-all.sh
```

Results: `dist/` (portable program) and `build/release/` (installer, portable zip,
source archives, `SHA256SUMS.txt`).

What the build does:

| Step | Notes |
|---|---|
| `UxPlay/` | Upstream UxPlay as a git submodule, unmodified. Built **without** `CMAKE_BUILD_TYPE=Release` on purpose: `-DNDEBUG` would strip an `assert()` that contains a required call. |
| `build-deps.sh` | Downloads (pinned SHA-256) and builds a minimal LGPL FFmpeg with only the AAC, ALAC, H.264 and HEVC decoders, plus gst-libav against it. Replaces ~150 MB of MSYS2's full FFmpeg with ~6 MB. |
| `app/` | The Qt 6 control panel (`KagamiCast.exe`). |
| `package.sh` | Copies only the 24 GStreamer plugins uxplay uses, resolves DLLs with `objdump`, writes `THIRD-PARTY-NOTICES.txt` and `licenses/`. |
| `installer/setup.iss` | Inno Setup script (English/Thai). |

The product name and installer id live in `product.conf`.

## How it works

`KagamiCast.exe` starts `uxplay.exe` as a child process with the chosen options,
reads its log to show the session state, and stops it with the window. uxplay
advertises the receiver through Bonjour (mDNS), receives the AirPlay stream and
renders it with GStreamer. Settings and logs are stored in `%LOCALAPPDATA%\KagamiCast`.

## License

Kagami Cast is free software under the **GNU General Public License v3.0 or later**
(see [LICENSE](LICENSE)). Binary releases bundle third-party components under their
own licenses (GPL, LGPL, MIT, BSD, Apache-2.0, ICU…); see `THIRD-PARTY-NOTICES.txt`
in the release. UxPlay is GPL-3.0; its FairPlay support (`lib/playfair`) comes from
reverse engineering.

AirPlay, iPhone and iPad are trademarks of Apple Inc. Kagami Cast is an independent
project and is not affiliated with, endorsed or sponsored by Apple.

## Credits

- [UxPlay](https://github.com/FDH2/UxPlay) — the AirPlay server doing the real work
- [GStreamer](https://gstreamer.freedesktop.org), [FFmpeg](https://ffmpeg.org),
  [Qt](https://www.qt.io), [MSYS2](https://www.msys2.org)
