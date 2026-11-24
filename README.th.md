# Kagami Cast

**สะท้อนหน้าจอ iPhone/iPad ขึ้น PC Windows — ฟรีและโอเพนซอร์ส**

Kagami Cast คือหน้าต่างควบคุมบน Windows ที่ครอบ [UxPlay](https://github.com/FDH2/UxPlay)
(เซิร์ฟเวอร์ AirPlay mirroring แบบโอเพนซอร์ส) ตั้งชื่อเครื่อง เลือกโหมด กด **เริ่มรับสัญญาณ**
แล้วเลือกชื่อเครื่องใน *Screen Mirroring* บน iPhone — ภาพและเสียงขึ้นในหน้าต่างแยก
ส่วนหน้าต่างโปรแกรมแสดงสถานะและ log

[English](README.md)

## ความสามารถ

- Screen Mirroring พร้อมเสียงจาก iPhone/iPad (ทดสอบกับ iPhone 17 Pro Max)
- โหมด: สมดุล 1080p60, หน่วงต่ำสุด, 4K H.265, เข้ากันได้สูงสุด
- ถอดรหัสภาพด้วย GPU (Direct3D 11/12, NVIDIA, Intel, AMD) และมีแบบซอฟต์แวร์สำรอง
- เลือกให้ต้องใส่ PIN ได้ กันคนอื่นในเครือข่ายส่งภาพขึ้นจอ
- ตัวติดตั้งตั้ง Firewall ให้ (ค่าเริ่มต้นเฉพาะเครือข่าย Private) และลง Apple Bonjour ให้
- ไม่มี telemetry ไม่ต้องสมัครบัญชี ไม่มีโฆษณา

## ติดตั้ง

ดาวน์โหลด `KagamiCast-<version>-setup.exe` จาก[หน้า Releases](https://github.com/EarthWL/kagami-cast/releases) แล้วรัน — ตัวติดตั้งจะลงโปรแกรม
เพิ่มกฎ Firewall ให้ตัวรับภาพ และลง Apple Bonjour ผ่าน winget ถ้ายังไม่มี มีแบบ zip portable ให้ด้วย

ตัวติดตั้งยังไม่ได้ลงลายเซ็นดิจิทัล Windows SmartScreen จึงอาจเตือนตอนเปิดครั้งแรก
(*More info → Run anyway*)

**ต้องมี:** Windows 10/11 64-bit, iPhone กับ PC อยู่เครือข่ายเดียวกัน และ Apple Bonjour
(ตัวติดตั้งจัดการให้) — ถ้า iPhone ไม่เห็นชื่อเครื่อง ให้ตั้ง Wi-Fi ใน Windows เป็น *Private*

## Build จาก source

ต้องมี [MSYS2](https://www.msys2.org) (UCRT64) และ [Inno Setup 6](https://jrsoftware.org/isinfo.php) สำหรับตัวติดตั้ง

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
- **`installer/setup.iss`** สคริปต์ Inno Setup (อังกฤษ/ไทย)
- ชื่อโปรแกรมและ id ของตัวติดตั้งอยู่ใน `product.conf`
- `tools/` สคริปต์ตอนพัฒนา (รัน GUI/uxplay จาก build tree, firewall, ทดสอบเสียง)

## การทำงาน

`KagamiCast.exe` สั่งรัน `uxplay.exe` เป็น child process ตาม option ที่เลือก อ่าน log ของมัน
มาแสดงสถานะ และปิดมันไปพร้อมหน้าต่าง (Windows Job Object) — uxplay ประกาศตัวผ่าน
Bonjour (mDNS) รับสตรีม AirPlay แล้วแสดงผลด้วย GStreamer

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
