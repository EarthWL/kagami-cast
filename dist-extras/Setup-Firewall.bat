@echo off
chcp 65001 >nul
setlocal
REM ===== เปิด Firewall ให้ Kagami Cast (รันครั้งเดียว) =====
REM ขอสิทธิ์ admin อัตโนมัติ แล้วเพิ่ม inbound rule ให้ uxplay.exe
REM ใช้แทนการรอ popup "Allow access" ที่บางเครื่องไม่เด้ง
REM ค่าเริ่มต้นเปิดเฉพาะเครือข่าย Private/Domain; เครือข่าย Public ต้องตอบ Y เอง

REM --- ตรวจสิทธิ์ admin ถ้ายังไม่ใช่ ให้เปิดไฟล์นี้ใหม่แบบ admin (UAC) ---
REM ส่ง path ผ่านตัวแปร SELF เพื่อไม่ให้ path ที่มีเครื่องหมาย ' ทำให้คำสั่งพัง
net session >nul 2>&1
if errorlevel 1 (
  echo กำลังขอสิทธิ์ผู้ดูแลระบบ...
  set "SELF=%~f0"
  powershell -NoProfile -Command "Start-Process -FilePath $env:SELF -Verb RunAs" || (
    echo ไม่ได้รับสิทธิ์ผู้ดูแลระบบ - ยกเลิกแล้ว
    pause
  )
  exit /b
)

set "HERE=%~dp0"
set "UXPLAY=%HERE%uxplay.exe"
if not exist "%UXPLAY%" (
  echo ไม่พบ uxplay.exe ในโฟลเดอร์เดียวกับไฟล์นี้
  pause
  exit /b 1
)

echo เพิ่ม firewall rule สำหรับ:
echo   %UXPLAY%
echo.
echo ค่าเริ่มต้นจะเปิดเฉพาะเครือข่ายแบบ Private (Wi-Fi บ้าน/ที่ทำงานที่ตั้งเป็น Private)
echo ถ้า Wi-Fi ถูกตั้งเป็น Public iPhone จะมองไม่เห็นเครื่อง - แนะนำให้เปลี่ยน Wi-Fi เป็น Private:
echo   Settings ^> Network ^& internet ^> Wi-Fi ^> (ชื่อ Wi-Fi) ^> Network profile type = Private
echo.
choice /C YN /N /M "เปิดบนเครือข่าย Public ด้วยไหม? ไม่แนะนำ: ใครอยู่ Wi-Fi เดียวกันก็ส่งภาพขึ้นจอได้ [Y/N] "
if errorlevel 2 (
  set "PROFILES=private,domain"
) else (
  set "PROFILES=private,public,domain"
)

REM ลบ rule เก่า (ถ้ามี) กันซ้ำ
netsh advfirewall firewall delete rule name="Kagami Cast (TCP)" >nul 2>&1
netsh advfirewall firewall delete rule name="Kagami Cast (UDP)" >nul 2>&1

netsh advfirewall firewall add rule name="Kagami Cast (TCP)" dir=in action=allow program="%UXPLAY%" protocol=TCP enable=yes profile=%PROFILES%
netsh advfirewall firewall add rule name="Kagami Cast (UDP)" dir=in action=allow program="%UXPLAY%" protocol=UDP enable=yes profile=%PROFILES%

echo.
echo เสร็จแล้ว - เปิดให้เครือข่าย: %PROFILES%
echo หมายเหตุ: ถ้าย้ายโฟลเดอร์โปรแกรมไปที่อื่น ต้องรันไฟล์นี้ใหม่ (rule ผูกกับตำแหน่ง uxplay.exe)
pause
