# Kagami Cast

**Mirror your iPhone or iPad screen to a Windows PC — free and open source.**

Kagami Cast is a small Windows control panel around [UxPlay](https://github.com/FDH2/UxPlay),
the open-source AirPlay mirroring server. Pick a name and a quality preset, press
**Start**, then choose the PC under *Screen Mirroring* on the iPhone. Video and audio
play in their own window, or the picture goes straight into OBS Studio; the panel
shows the connection status and log.

[ภาษาไทย](README.th.md)

## Features

- Screen mirroring with audio from iPhone/iPad (tested with an iPhone 17 Pro Max)
- Presets: balanced 1080p60, low latency, 4K H.265, and a maximum-compatibility mode
- GPU video decoding (Direct3D 11/12, NVIDIA, Intel, AMD), software fallback
- Optional PIN pairing so nobody else on the network can cast to your screen
- **Send the picture to OBS Studio** as a source of its own — no window capture, no copy through the CPU
- No Apple Bonjour needed: the receiver is announced through the mDNS service built into Windows
- Installer sets up Windows Firewall (private networks by default)
- No telemetry, no account, no ads

## Install

Download `KagamiCast-<version>-setup.exe` from the [Releases page](https://github.com/EarthWL/kagami-cast/releases) and run it.
It installs the program and adds firewall rules for the receiver. A portable zip
is also provided.

The installer is not code-signed yet, so Windows SmartScreen may warn on first run
(*More info → Run anyway*).

**Requirements:** Windows 10/11 64-bit and the iPhone and PC on the same network.
If the iPhone does not list the PC, set the Wi-Fi network profile to *Private* in
Windows settings.

**Upgrading from 1.2 or older:** those versions installed Apple Bonjour, which is
no longer used. You can uninstall it (*Settings → Apps → Bonjour*); that also
ends the *"This module is blocked from loading into the Local Security Authority"*
message (`mdnsNSP.dll`) that Windows 11 shows at start-up while Bonjour is installed.

## Use with OBS Studio

Requires OBS 31 or newer.

1. In the installer, keep *Install the plugin that shows the iPhone picture as an OBS source*
   ticked (it is pre-ticked when OBS is installed). Portable zip: run `INSTALL-OBS-Plugin.bat`.
2. In OBS: Sources → **+** → **Kagami Cast (iPhone / iPad)**.
3. In Kagami Cast: tick **Send to OBS** (ส่งภาพเข้า OBS), press Start, then mirror the iPhone.

- **Rotation:** select the source and press **Ctrl+F** (*Fit to screen*) once, or set
  **Bounds Type = Fit** with a box size in *Edit Transform*. OBS then rescales the picture
  whenever the iPhone turns.
- **Audio** still plays on the speakers; to record or stream it, add an
  *Application Audio Capture* source for `uxplay.exe`.
- **Graphics card:** the plugin tells Kagami Cast which adapter OBS renders on, so the frames
  stay on that GPU. If the source's status says it receives through a CPU copy, restart
  Kagami Cast while OBS is open.

## Build from source

Requires [MSYS2](https://www.msys2.org) (UCRT64), [Inno Setup 6](https://jrsoftware.org/isinfo.php)
for the installer, and Visual Studio 2022/2026 with C++ for the OBS plugin (skipped
when Visual Studio is missing).

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
| `obs-plugin/` | The OBS source plugin (from obs-plugintemplate, built with MSVC; see [obs-plugin/README.md](obs-plugin/README.md)). `build-all.sh` finds Visual Studio with vswhere and puts the result in `dist/obs-plugin/`. |
| `installer/setup.iss` | Inno Setup script (English/Thai). |

The product name and installer id live in `product.conf`.

## How it works

`KagamiCast.exe` starts `uxplay.exe` as a child process with the chosen options,
reads its log to show the session state, and stops it with the window. uxplay
advertises the receiver over mDNS, receives the AirPlay stream and renders it with
GStreamer. Settings and logs are stored in `%LOCALAPPDATA%\KagamiCast`.

uxplay loads Apple's DNS-SD client library, `dnssd.dll`, at run time. Kagami Cast
ships its own `dnssd.dll` next to `uxplay.exe` ([dnssd-shim/](dnssd-shim/)): the same
seven calls, implemented on Windows' built-in DNS-SD API (`DnsServiceRegister`,
Windows 10 and later), so UxPlay stays unmodified and Bonjour is not needed. If
that API is missing it falls back to an installed Bonjour.

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
