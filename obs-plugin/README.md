# Kagami Cast for OBS

OBS Studio source that shows the iPhone/iPad picture received by Kagami Cast,
without a window capture and without copying frames through system memory.

```
iPhone ──AirPlay──> uxplay (Kagami Cast, "Send to OBS" on)
                    decoder ! d3d11convert → BGRA ! d3d11ipcsink  pipe \\.\pipe\kagami-cast
                                                        │  shared D3D11 texture (NT handle + keyed mutex)
OBS ── source "Kagami Cast (iPhone / iPad)" ────────────┘  src/kagami-ipc.cpp: client of GStreamer's d3d11ipc protocol
```

* Zero copy needs the sender and OBS on the same graphics adapter. The plugin
  writes OBS's adapter LUID to `%LOCALAPPDATA%\KagamiCast\obs-adapter.txt`;
  Kagami Cast reads it when it starts and passes the matching `adapter=` index to
  `d3d11convert` and `d3d11ipcsink`.
* If they still differ (Kagami Cast was started before OBS, or a GPU preference
  reorders the adapters), the plugin opens the frames on a helper device for the
  sender's adapter and uploads a CPU copy. The source's properties say which mode
  is in use.
* Audio still plays through the speakers; capture it in OBS with
  *Application Audio Capture* → `uxplay.exe`.

## Build (Windows)

Visual Studio 2026 (or 2022) with the C++ workload. The first configure downloads
the OBS 31.1.1 sources and obs-deps listed in `buildspec.json` into `.deps/`.

```
cmake --preset windows-x64-vs2026        (or windows-x64 for VS 2022)
cmake --build --preset windows-x64-vs2026
cmake --install build_x64 --config RelWithDebInfo --prefix C:/ProgramData/obs-studio/plugins
```

The install step puts the plugin where OBS 30+ looks for per-machine plugins:
`C:\ProgramData\obs-studio\plugins\kagami-cast-obs\{bin\64bit,data}`.

`tests/ipc-client-test.cpp` is a stand-alone client for checking a d3d11ipcsink
producer (MinGW: `g++ -std=c++17 -municode -static tests/ipc-client-test.cpp
src/kagami-ipc.cpp -ld3d11 -ldxgi`).

## License

GPL-3.0-or-later, like the rest of Kagami Cast. The CMake helper files in
`cmake/` come from [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate)
(GPL-2.0-or-later, see `LICENSE-obs-plugintemplate`).
