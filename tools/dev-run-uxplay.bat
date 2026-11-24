@echo off
REM SPDX-License-Identifier: GPL-3.0-or-later
REM ===== uxplay without the GUI (development) =====
REM Adds the MSYS2 UCRT64 runtime (GStreamer) to PATH, then runs the locally built uxplay.
setlocal
set "PATH=C:\msys64\ucrt64\bin;%PATH%"
set "UXPLAY=%~dp0..\build\uxplay\uxplay.exe"

REM -n sets the name shown on the iPhone's AirPlay list, -nh stops uxplay from
REM appending "@<computer name>". Extra flags are passed through.
"%UXPLAY%" -n "Kagami-Dev" -nh %*

endlocal
