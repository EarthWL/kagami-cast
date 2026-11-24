@echo off
REM SPDX-License-Identifier: GPL-3.0-or-later
chcp 65001 >nul
REM ลองเล่นเสียงบี๊บผ่าน audio sink แต่ละตัว ในเซสชันเดสก์ท็อปของคุณ
REM ตัวที่ใช้ได้จะมีเสียงบี๊บ + exitcode=0 ในไฟล์ผลลัพธ์
REM หมายเหตุ: ห้ามเปิด enabledelayedexpansion ในไฟล์นี้ เพราะ cmd จะกิน "!" ที่ใช้ต่อ
REM element ของ gst-launch ทำให้ pipeline ไม่ถูกต่อ (error "not-linked")
setlocal
set "PATH=C:\msys64\ucrt64\bin;%PATH%"
set "OUT=%~dp0audio-test.txt"
echo audio sink test > "%OUT%"

for %%S in (wasapisink wasapi2sink directsoundsink) do call :test %%S

echo ===================================== >> "%OUT%"
echo DONE >> "%OUT%"
echo.
echo เสร็จแล้ว - ตัวไหนมีเสียงบี๊บคือใช้ได้ (ผลละเอียดในไฟล์ audio-test.txt)
pause
exit /b

:test
echo ===================================== >> "%OUT%"
echo ===== %1 ===== >> "%OUT%"
echo [ กำลังทดสอบ %1 - ฟังเสียงบี๊บ ]
gst-launch-1.0 -q audiotestsrc num-buffers=90 wave=sine freq=660 volume=0.3 ! audioconvert ! audioresample ! %1 >> "%OUT%" 2>&1
echo exitcode=%errorlevel% >> "%OUT%"
exit /b
