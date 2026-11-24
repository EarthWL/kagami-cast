@echo off
REM SPDX-License-Identifier: GPL-3.0-or-later
REM ===== Kagami Cast - development launcher =====
REM Runs the GUI from the build tree against the MSYS2 UCRT64 runtime (Qt6 +
REM GStreamer) and the locally built engine. A packaged dist needs none of this:
REM there KagamiCast.exe finds uxplay.exe and the runtime next to itself.
setlocal
set "ROOT=%~dp0.."
set "PATH=C:\msys64\ucrt64\bin;%PATH%"
set "QT_QPA_PLATFORM_PLUGIN_PATH=C:\msys64\ucrt64\share\qt6\plugins\platforms"
set "UXPLAY_EXE=%ROOT%\build\uxplay\uxplay.exe"
start "" "%ROOT%\build\app\KagamiCast.exe"
endlocal
