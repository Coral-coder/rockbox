# Restore to stock HiBy (NAND) — H2 v2.3 HW4

Use this when the player does not boot **stock HiBy Linux** by default, recovery is wrong, or you need **factory HiBy v2.3** back in NAND.

---

## What you need from this kit

| File | Copy to SD |
|------|------------|
| `update.upt` | **SD card root** as `E:\update.upt` |

**MD5 must be:** `2DAC5D428BC60572DB42C05E9303D5A9`  
This is **H2-v23-stock-FORCE-UBOOT** — stock v2.3 image with **bootloader=** in the manifest (OEM zip alone does **not** replace uboot).

---

## Before you flash — clean the SD

Remove files that fight hosted/stock recovery:

| Remove from SD | Why |
|----------------|-----|
| `bootloader.erosq` (root or `.rockbox`) | Can trigger native NAND install |
| `H2-v23-patched.upt` / wrong `update.upt` | Wrong manifest |
| `rockbox_main.aigo_erosqn` | Native artifact |
| `spl.erosq` | Bootloader fragment |

You may keep `/.rockbox/rockbox.erosq` — stock flash does not erase SD.

---

## Flash steps (stock updater, not Rockbox Install)

1. Copy `Build_products\update.upt` → `E:\update.upt`
2. Safely eject SD, insert in H2.
3. Enter **HiBy stock firmware update**:
   - **Screen works:** VOL+ at boot → **HibyOS Recovery** (or stock Settings → firmware update).
   - **Black screen:** Hold **PLAY** ~3 s at power-on, wait ~90 s for Linux; use stock update UI if USB/ADB connects.
4. Run update from SD. Wait until success/reboot (several minutes). **Do not** remove SD mid-flash.
5. On PC: **delete `E:\update.upt`** so it never reflashes by accident.

**Do not** use Rockbox recovery → **Bootloader → Install or update** for `update.upt`.

---

## After stock NAND is back

1. Normal boot should show **HiBy launcher** (not bare-metal Rockbox on power-on only).
2. Deploy hosted app: [04-RESTORE-TO-HOSTED.md](04-RESTORE-TO-HOSTED.md)

---

## Verify on PC

```powershell
(Get-FileHash E:\update.upt -Algorithm MD5).Hash
# 2DAC5D428BC60572DB42C05E9303D5A9  (before flash — delete after)
```

---

## If flash “completes” but still boots bare-metal Rockbox

- Wrong updater (Rockbox Install instead of HiBy).
- SD still had **patched** `update.upt` (not `2DAC5D42…`).
- OEM `update.upt` without forced uboot — uboot partition unchanged.

Repeat with this kit’s `update.upt` only.

---

## Wrong firmware warning (HW4)

Do **not** flash v1.8 / hw3 zips from HiFi Walker’s old Google Drive on a **v2.3 HW4** unit — black screen is common. This kit’s `update.upt` is for **v2.3 HW4** only.
