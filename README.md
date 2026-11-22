# VST Bridge 32 → 64

ปลั๊กอิน VST3 (64-bit) ที่ "ห่อ" ปลั๊กอิน VST2 แบบ 32-bit (x86) ให้ใช้ใน DAW แบบ 64-bit ได้

> ไฟล์ .dll แบบ 32-bit **แปลงเป็น 64-bit ตรง ๆ ไม่ได้** (ไม่มีซอร์สโค้ด) ทางเดียวคือ "บริดจ์":
> รันปลั๊กอิน 32-bit ในโปรเซสแยก แล้วส่งเสียง/MIDI/พารามิเตอร์/หน้าต่าง GUI ข้ามโปรเซส (เหมือน jBridge / yabridge)

## โครงสร้าง

```
DAW (64-bit)
 └─ VSTBridge.vst3  (JUCE, 64-bit)  ── shared memory + named events ──┐
                                                                       ▼
                                          Bridge32Host.exe (32-bit) → โหลด plugin32.dll
```

- `host32/`  โปรเซส 32-bit: โหลด VST2, ประมวลผลเสียง, เปิด GUI เป็น child window
- `plugin/`  ตัว VST3 64-bit: ส่งเสียง/MIDI/พารามิเตอร์ไปให้โฮสต์, ฝัง GUI ของปลั๊กอิน 32-bit
- `common/ipc.h`  โครงหน่วยความจำร่วม

## Build (Windows, Visual Studio 2022 + CMake)

```
cmake -S . -B build32 -A Win32 -DBRIDGE_TARGET=host32
cmake --build build32 --config Release

cmake -S . -B build64 -A x64 -DBRIDGE_TARGET=plugin -DBRIDGE32_EXE=build32/host32/Release/Bridge32Host.exe
cmake --build build64 --config Release
```

หรือ push ขึ้น GitHub แล้วให้ `.github/workflows/build.yml` build ให้ (ดาวน์โหลดจาก Artifacts)

ผลลัพธ์: `VSTBridge.vst3` — `Bridge32Host.exe` ต้องอยู่ในโฟลเดอร์เดียวกับไฟล์ binary ข้างใน
`VSTBridge.vst3/Contents/x86_64-win/` (CMake คัดลอกให้อัตโนมัติ)

## วิธีใช้

1. คัดลอก `VSTBridge.vst3` ไปที่ `C:\Program Files\Common Files\VST3`
2. โหลด "VST Bridge 32-64" บนแทร็กใน DAW
3. กดปุ่ม **Load 32-bit VST2 (.dll)...** เลือกปลั๊กอิน 32-bit
4. GUI ของปลั๊กอินจะแสดงใต้แถบด้านบน; พารามิเตอร์แรก 128 ตัวทำ automation ได้

## ข้อจำกัดของเวอร์ชันแรก (v0.1)

- **ยังไม่ได้ compile/ทดสอบ** — เขียนในสภาพแวดล้อมที่ไม่มี Windows/MSVC ต้อง build แล้วแก้ error เล็กน้อยก่อน
- รองรับสเตอริโอ 2 in / 2 out เท่านั้น (ช่องเกินจะถูกตัด), ยังไม่รองรับ sidechain
- ไม่บันทึก chunk state ภายในของปลั๊กอิน (บันทึกเฉพาะ path + ค่าพารามิเตอร์) — พรีเซตที่ไม่ผ่านพารามิเตอร์อาจไม่ครบ
- ไม่ส่ง tempo/transport (audioMasterGetTime คืน 0) ปลั๊กอินที่ sync BPM จะไม่ได้ค่า
- MIDI ขาเข้าอย่างเดียว (ไม่มี MIDI out), ไม่มี SysEx
- GUI: ยังไม่จัดการ DPI scaling; การฝังหน้าต่างข้ามโปรเซสอาจมีปัญหา focus/คีย์บอร์ดในบาง DAW
- ถ้า 32-bit host ค้างหรือแครช เสียงจะเงียบ (timeout 250 ms ต่อบล็อก) ไม่ทำให้ DAW แครช
- แต่ละอินสแตนซ์ = 1 โปรเซส (ใช้ RAM มากขึ้นถ้าเปิดหลายตัว)
- `host32/vst2_min.h` เป็นการเขียนโครงสร้าง ABI ของ VST2 ขึ้นเองเพื่อไม่ต้องใช้ VST2 SDK ของ Steinberg (ที่แจกจ่ายต่อไม่ได้แล้ว)
- ตรวจสอบสิทธิ์/ใบอนุญาต: ปลั๊กอิน VST2 ของคนอื่นใช้ผ่านบริดจ์ได้ แต่ไม่ได้ทำให้ถูก relicense

## ต่อยอดที่แนะนำ

1. `effGetChunk / effSetChunk` สำหรับบันทึก state เต็ม
2. ส่ง `VstTimeInfo` (tempo, ตำแหน่ง, play state) ผ่าน shared memory
3. รองรับจำนวนช่องแบบไดนามิก + sidechain
4. ใช้ thread แยกสำหรับ GUI ของ host32 เพื่อไม่ให้ GUI ค้างกระทบเสียง
5. บริดจ์ VST2 64-bit ↔ host 32-bit (ทิศทางกลับกัน) ใช้โครงเดียวกันได้
