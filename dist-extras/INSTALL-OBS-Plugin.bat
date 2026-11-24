@echo off
chcp 65001 >nul
REM ===== ติดตั้งปลั๊กอิน Kagami Cast สำหรับ OBS Studio (ชุด portable) =====
REM คัดลอกโฟลเดอร์ obs-plugin\kagami-cast-obs ไปที่ %ProgramData%\obs-studio\plugins
REM ซึ่ง OBS 31 ขึ้นไปโหลดปลั๊กอินจากที่นี่ (ตัวติดตั้ง setup.exe ทำขั้นตอนนี้ให้เอง)
REM ถอนออก: ลบโฟลเดอร์ %ProgramData%\obs-studio\plugins\kagami-cast-obs

set "SRC=%~dp0obs-plugin\kagami-cast-obs"
set "DST=%ProgramData%\obs-studio\plugins\kagami-cast-obs"

if not exist "%SRC%\bin\64bit\kagami-cast-obs.dll" (
  echo ไม่พบไฟล์ปลั๊กอินในโฟลเดอร์ obs-plugin
  pause
  exit /b 1
)

tasklist /fi "imagename eq obs64.exe" 2>nul | find /i "obs64.exe" >nul
if not errorlevel 1 (
  echo OBS เปิดอยู่ - ปิด OBS ก่อน แล้วรันไฟล์นี้อีกครั้ง
  pause
  exit /b 1
)

robocopy "%SRC%" "%DST%" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (
  echo คัดลอกไม่สำเร็จ - คลิกขวาที่ไฟล์นี้แล้วเลือก Run as administrator
  pause
  exit /b 1
)
echo ติดตั้งปลั๊กอินแล้วที่ %DST%
echo เปิด OBS แล้วเพิ่ม Source "Kagami Cast (iPhone / iPad)"
echo จากนั้นใน Kagami Cast ติ๊ก "ส่งภาพเข้า OBS" แล้วกดเริ่ม
pause
exit /b 0
