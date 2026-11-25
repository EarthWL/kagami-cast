# Kagami Cast

**สะท้อนหน้าจอ iPhone/iPad ขึ้น PC Windows — ฟรีและโอเพนซอร์ส**

Kagami Cast คือหน้าต่างควบคุมบน Windows ที่ครอบ [UxPlay](https://github.com/FDH2/UxPlay)
(เซิร์ฟเวอร์ AirPlay mirroring แบบโอเพนซอร์ส) ตั้งชื่อเครื่อง เลือกโหมด กด **เริ่มรับสัญญาณ**
แล้วเลือกชื่อเครื่องใน *Screen Mirroring* บน iPhone — ภาพและเสียงขึ้นในหน้าต่างแยก
หรือส่งภาพเข้า OBS Studio โดยตรง ส่วนหน้าต่างโปรแกรมแสดงสถานะและ log

[English](README.md)

## ความสามารถ

- Screen Mirroring พร้อมเสียงจาก iPhone/iPad (ทดสอบกับ iPhone 17 Pro Max)
- โหมด: สมดุล 1080p60, หน่วงต่ำสุด, 4K H.265, เข้ากันได้สูงสุด
- ถอดรหัสภาพด้วย GPU (Direct3D 11/12, NVIDIA, Intel, AMD) และมีแบบซอฟต์แวร์สำรอง
- เลือกให้ต้องใส่ PIN ได้ กันคนอื่นในเครือข่ายส่งภาพขึ้นจอ
- **ส่งภาพเข้า OBS Studio** เป็น Source โดยตรง ไม่ต้องใช้ Window Capture และภาพไม่ต้องผ่าน CPU
- ไม่ต้องลง Apple Bonjour: ประกาศชื่อเครื่องผ่านระบบ mDNS ที่มีใน Windows เอง
- ตัวติดตั้งตั้ง Firewall ให้ (ค่าเริ่มต้นเฉพาะเครือข่าย Private)
- ไม่มี telemetry ไม่ต้องสมัครบัญชี ไม่มีโฆษณา

## ติดตั้ง

ดาวน์โหลด `KagamiCast-<version>-setup.exe` จาก[หน้า Releases](https://github.com/EarthWL/kagami-cast/releases) แล้วรัน — ตัวติดตั้งจะลงโปรแกรม
และเพิ่มกฎ Firewall ให้ตัวรับภาพ มีแบบ zip portable ให้ด้วย

ตัวติดตั้งยังไม่ได้ลงลายเซ็นดิจิทัล Windows SmartScreen จึงอาจเตือนตอนเปิดครั้งแรก
(*More info → Run anyway*)

**ต้องมี:** Windows 10/11 64-bit และ iPhone กับ PC อยู่เครือข่ายเดียวกัน — ถ้า iPhone ไม่เห็นชื่อเครื่อง
ให้ตั้ง Wi-Fi ใน Windows เป็น *Private*

**อัปเกรดจากรุ่น 1.2 หรือเก่ากว่า:** รุ่นเหล่านั้นลง Apple Bonjour ไว้ ซึ่งตอนนี้ไม่ได้ใช้แล้ว ถอนออกได้
(*Settings → Apps → Bonjour*) และข้อความ *"This module is blocked from loading into the Local Security
Authority"* (`mdnsNSP.dll`) ที่ Windows 11 เด้งตอนเปิดเครื่องจะหายไปด้วย

## ใช้กับ OBS Studio

ต้องใช้ OBS 31 ขึ้นไป

1. ตอนติดตั้ง ติ๊ก "ติดตั้งปลั๊กอินที่แสดงภาพ iPhone เป็น Source ใน OBS" (ติ๊กไว้ให้แล้วถ้าเครื่องมี OBS)
   — ชุด portable ให้ดับเบิลคลิก `INSTALL-OBS-Plugin.bat`
2. ใน OBS: Sources → **+** → **Kagami Cast (iPhone / iPad)**
3. ใน Kagami Cast: ติ๊ก **ส่งภาพเข้า OBS** แล้วกดเริ่ม จากนั้นเปิด Screen Mirroring บน iPhone

- **หมุนจอแล้วภาพไม่พอดี:** เลือก Source แล้วกด **Ctrl+F** (Fit to screen) ครั้งเดียว หรือใน
  *Edit Transform* ตั้ง **Bounds Type = Fit** พร้อมขนาดกล่อง — OBS จะย่อขยายให้พอดีเองทุกครั้งที่หมุน
- **เสียง:** ยังออกลำโพงตามปกติ ถ้าจะอัด/สตรีมเสียงด้วย ให้เพิ่ม *Application Audio Capture* แล้วเลือก `uxplay.exe`
- **การ์ดจอ:** ปลั๊กอินบอกโปรแกรมว่า OBS ใช้การ์ดใบไหน ภาพจึงส่งตรงบน GPU เดียวกัน ถ้าสถานะใน
  Source ขึ้นว่า "ผ่าน CPU" ให้ปิดแล้วเปิด Kagami Cast ใหม่ตอนที่ OBS เปิดอยู่

## Build จาก source

ต้องมี [MSYS2](https://www.msys2.org) (UCRT64), [Inno Setup 6](https://jrsoftware.org/isinfo.php) สำหรับตัวติดตั้ง
และ Visual Studio 2022/2026 (C++) สำหรับปลั๊กอิน OBS — ถ้าไม่มี VS จะข้ามปลั๊กอินไป

```sh
# ใน MSYS2 UCRT64 shell
pacman -S --needed make diffutils mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,nasm,meson,openssl,libplist,qt6-base,gstreamer,gst-plugins-base,gst-plugins-good,gst-plugins-bad}
git clone --recurse-submodules https://github.com/EarthWL/kagami-cast.git
cd kagami-cast
bash build-all.sh
```

ผลลัพธ์: `dist/` (โปรแกรมแบบ portable) และ `build/release/` (ตัวติดตั้ง, zip, source archive, `SHA256SUMS.txt`)

### รายละเอียดการ build
- **UxPlay** เป็น git submodule ต้นฉบับ ไม่ได้แก้ — **ห้าม build แบบ `CMAKE_BUILD_TYPE=Release`**
  เพราะ `-DNDEBUG` ตัด `assert()` ทิ้งทั้งหมด รวมถึงตัวที่มีการเรียกฟังก์ชันที่จำเป็นอยู่ข้างใน
  (`pairing_session_make_nonce` ใน `lib/raop_handlers.h`); UxPlay ใส่ `-O2` ให้เองอยู่แล้ว
- **`build-deps.sh`** ดาวน์โหลด (ตรวจ SHA-256) แล้ว build FFmpeg รุ่นเล็ก (LGPL) ที่มีแค่ decoder
  AAC, ALAC, H.264, HEVC กับ gst-libav — แทน FFmpeg ตัวเต็มของ MSYS2 (~150 MB → ~6 MB)
- **`package.sh`** คัดเฉพาะปลั๊กอิน GStreamer 24 ตัวที่ uxplay ใช้ (รวมตัวถอดรหัส GPU ทุกค่าย
  เพราะ decodebin เลือกตามเครื่อง) หา DLL ด้วย `objdump` (ไม่ใช่ ldd เพราะ DLL ของ FFmpeg
  รุ่นเล็กชื่อซ้ำกับของ MSYS2) และสร้าง `THIRD-PARTY-NOTICES.txt` + โฟลเดอร์ `licenses/`
  — ถ้าจะใช้ option ใหม่ของ uxplay (`-hls`, `-ca`, `-mp4` …) ต้องเพิ่มปลั๊กอินในรายการ `PLUGINS`
- **`obs-plugin/`** ปลั๊กอิน OBS (สร้างจาก obs-plugintemplate, build ด้วย MSVC) — รายละเอียดใน
  [obs-plugin/README.md](obs-plugin/README.md); `build-all.sh` หา Visual Studio ด้วย vswhere แล้ววางผลไว้ที่
  `dist/obs-plugin/`
- **`installer/setup.iss`** สคริปต์ Inno Setup (อังกฤษ/ไทย)
- ชื่อโปรแกรมและ id ของตัวติดตั้งอยู่ใน `product.conf`
- `tools/` สคริปต์ตอนพัฒนา (รัน GUI/uxplay จาก build tree, firewall, ทดสอบเสียง)

## การทำงาน

`KagamiCast.exe` สั่งรัน `uxplay.exe` เป็น child process ตาม option ที่เลือก อ่าน log ของมัน
มาแสดงสถานะ และปิดมันไปพร้อมหน้าต่าง (Windows Job Object) — uxplay ประกาศตัวผ่าน
mDNS รับสตรีม AirPlay แล้วแสดงผลด้วย GStreamer

- uxplay โหลด `dnssd.dll` (ไลบรารี DNS-SD ของ Apple) ตอนรัน Kagami Cast ใส่ `dnssd.dll` ของตัวเองไว้ข้าง
  `uxplay.exe` ([dnssd-shim/](dnssd-shim/)) ซึ่งทำ 7 ฟังก์ชันเดียวกันบน DNS-SD API ที่มีใน Windows
  (`DnsServiceRegister`, Windows 10 ขึ้นไป) — UxPlay จึงไม่ต้องแก้ และไม่ต้องใช้ Bonjour
  ถ้า Windows ไม่มี API นี้จะใช้ Bonjour ที่ติดตั้งไว้แทน
- ค่าที่ตั้ง `settings.ini`, log `session.log` (หมุนไฟล์เมื่อเกิน 1 MB), key ของเครื่องรับ `uxplay.pem`,
  รายชื่อเครื่องที่ใส่ PIN แล้ว `uxplay.register` และ GStreamer registry อยู่ที่ `%LOCALAPPDATA%\KagamiCast`
- **โหมด PIN** ส่ง `-pin -key … -reg …` ต้องมีครบ: ไม่มี `-reg` uxplay จะเชื่อ client ที่อ้างว่า
  เคยจับคู่แล้วโดยไม่ถาม PIN; ไม่มี `-key` key ของเครื่องรับจะคำนวณจาก MAC address
- สถานะอ่านจากบรรทัด log ปกติของ UxPlay (`Initialized server socket`, `Accepted … client on socket N`,
  `connection request from`, `Begin streaming to GStreamer video pipeline`,
  `raop_rtp_mirror->running is no longer true`, `Connection closed on socket N`, `CLIENT MUST NOW ENTER PIN`)

## สัญญาอนุญาต

Kagami Cast เป็นซอฟต์แวร์เสรีภายใต้ **GNU General Public License v3.0 or later** ([LICENSE](LICENSE))
ไฟล์ release มีส่วนประกอบจากผู้อื่นซึ่งใช้สัญญาอนุญาตของตัวเอง (GPL, LGPL, MIT, BSD, Apache-2.0, ICU …)
ดู `THIRD-PARTY-NOTICES.txt` — UxPlay เป็น GPL-3.0 ส่วนรองรับ FairPlay (`lib/playfair`) มาจากการ reverse-engineer

AirPlay, iPhone และ iPad เป็นเครื่องหมายการค้าของ Apple Inc. — Kagami Cast เป็นโปรเจคอิสระ
ไม่ได้เกี่ยวข้อง ได้รับการรับรอง หรือสนับสนุนจาก Apple
