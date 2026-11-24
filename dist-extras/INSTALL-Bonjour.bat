@echo off
chcp 65001 >nul
REM ===== ติดตั้ง Apple Bonjour (จำเป็นต้องมีก่อนใช้งาน) =====
REM AirPlay ใช้ Bonjour/mDNS ในการให้ iPhone มองเห็นเครื่องนี้
REM รันครั้งเดียวบนเครื่องใหม่

sc query "Bonjour Service" >nul 2>&1
if not errorlevel 1 (
  echo มี Apple Bonjour ติดตั้งอยู่แล้ว - ไม่ต้องทำอะไรเพิ่ม
  pause
  exit /b 0
)

where winget >nul 2>&1
if errorlevel 1 goto nowinget

echo กำลังติดตั้ง Apple Bonjour ผ่าน winget...
winget install --id Apple.Bonjour -e --accept-source-agreements --accept-package-agreements
if errorlevel 1 goto failed
echo.
echo ติดตั้ง Bonjour เสร็จแล้ว
pause
exit /b 0

:nowinget
echo ไม่พบ winget บนเครื่องนี้
:failed
echo.
echo ติดตั้งอัตโนมัติไม่สำเร็จ - ให้ติดตั้ง iTunes หรือ "Bonjour Print Services" จาก Apple แทน
echo (จะได้ Bonjour service มาด้วย)
pause
exit /b 1
