@echo off
REM SPDX-License-Identifier: GPL-3.0-or-later
REM ===== Prototype for the OBS integration (development) =====
REM uxplay (from dist\) sends the decoded iPhone video as Direct3D 11 shared
REM textures through d3d11ipcsink instead of opening its own window. A second
REM process - standing in for the future OBS plugin - receives and shows them.
REM Audio still plays through the normal audio output.
REM
REM Usage:  dev-test-ipc.bat [adapter] [h265]
REM   adapter  DXGI adapter index for the shared textures (-1 = default GPU).
REM            On a PC with two GPUs try 0 and 1 (e.g. RTX 5070 / Radeon).
REM   h265     use H.265 (like the 4K preset) instead of H.264
REM Close Kagami Cast first (only one receiver can use the AirPlay ports).
setlocal
set "ROOT=%~dp0.."
set "DIST=%ROOT%\dist"
set "ADAPTER=%~1"
if "%ADAPTER%"=="" set "ADAPTER=-1"
set "CODEC="
if /i "%~2"=="h265" set "CODEC=-h265"

REM receiver: MSYS2 GStreamer (same version as dist), shows fps on the picture
start "IPC receiver - stand-in for OBS" cmd /k "set PATH=C:\msys64\ucrt64\bin;%PATH% && gst-launch-1.0 d3d11ipcsrc connection-timeout=0 ! fpsdisplaysink video-sink=d3d11videosink text-overlay=true sync=false"

REM sender: Kagami Cast's packaged uxplay with the IPC sink
set "PATH=%DIST%;%PATH%"
set "GST_PLUGIN_SYSTEM_PATH_1_0=%DIST%\lib\gstreamer-1.0"
set "GST_PLUGIN_PATH=%DIST%\lib\gstreamer-1.0"
set "GST_PLUGIN_SCANNER=%DIST%\libexec\gstreamer-1.0\gst-plugin-scanner.exe"
set "GST_REGISTRY=%LOCALAPPDATA%\KagamiCast\gst-registry.bin"
echo Sender: adapter=%ADAPTER% %CODEC%  -- on the iPhone choose "Kagami-IPC-Test"
"%DIST%\uxplay.exe" -n "Kagami-IPC-Test" -nh %CODEC% -s 1920x1080@60 -fps 60 -vs "d3d11ipcsink adapter=%ADAPTER%"
endlocal
