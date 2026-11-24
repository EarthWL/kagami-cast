@echo off
REM SPDX-License-Identifier: GPL-3.0-or-later
REM Run this ONCE as Administrator (right-click > Run as administrator).
REM Opens the firewall (Private/Domain networks only) for the locally built uxplay.
set "UXPLAY=%~dp0..\build\uxplay\uxplay.exe"
netsh advfirewall firewall add rule name="Kagami Cast dev (TCP)" dir=in action=allow program="%UXPLAY%" protocol=TCP enable=yes profile=private,domain
netsh advfirewall firewall add rule name="Kagami Cast dev (UDP)" dir=in action=allow program="%UXPLAY%" protocol=UDP enable=yes profile=private,domain
echo.
echo Done. Firewall rules added for the development uxplay.exe.
pause
