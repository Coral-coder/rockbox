# Restore to hosted Rockbox (stock Linux + SD app)

**Goal:** HiBy launcher on boot → launch Rockbox from stock app → Bluetooth in Rockbox settings.

---

## Prerequisites

- NAND should be **stock** (or dual-boot with **Hiby Player**). If power-on goes straight to bare-metal Rockbox, do [02-RESTORE-TO-STOCK.md](02-RESTORE-TO-STOCK.md) first.
- SD card formatted **FAT32**.

---

## Option A — PowerShell script (recommended)

1. Clone repo or open `Build_products` folder.
2. Insert SD (e.g. drive `E:`).
3. Run:

```powershell
cd path\to\rockbox\Build_products
.\scripts\Apply-Hosted-To-SD.ps1 -Drive E: -CopyBtBringup
```

4. Eject SD, boot H2 normally, open Rockbox from HiBy.

---

## Option B — Manual copy

Copy **exactly** these files from this kit:

| From kit | To SD |
|----------|-------|
| `hosted-on-sd\rockbox.erosq` | `E:\.rockbox\rockbox.erosq` |
| `hosted-on-sd\device-bt-bringup.sh` | `E:\.rockbox\device-bt-bringup.sh` (optional, HW4 BT) |

Create `.rockbox` if missing.

---

## Remove these from SD (hosted safety)

Delete if present:

```
E:\bootloader.erosq
E:\spl.erosq
E:\update.upt                    # unless actively flashing stock — then delete after
E:\.rockbox\bootloader.erosq
E:\.rockbox\spl.erosq
E:\.rockbox\rockbox_main.aigo_erosqn
E:\.rockbox\bcm43438.fw
```

---

## Verify on PC

```powershell
(Get-FileHash E:\.rockbox\rockbox.erosq -Algorithm MD5).Hash
# CA89ABB8047B1574015D9593C1A112E7

(Get-FileHash E:\.rockbox\device-bt-bringup.sh -Algorithm MD5).Hash
# 0CA9B5A29E54C3E92468D027340AAADD
```

---

## On device

1. Power on **without** holding VOL+ (unless you need recovery).
2. HiBy / stock launcher appears.
3. Open **Rockbox** from stock music app.
4. **Settings → General Settings → Bluetooth** → turn on → scan / pair.

If Bluetooth fails on HW4, ensure `device-bt-bringup.sh` is on SD; Rockbox can invoke it when bringing BT up.

---

## Update app without full SD reflash (ADB)

When stock Linux is running and Rockbox is **closed**:

```powershell
adb push Build_products\rockbox.erosq /mnt/sd_0/.rockbox/rockbox.erosq
```

Pushing while Rockbox is running often fails (“no space” / file busy).

---

## Do not confuse with native deploy

| Hosted (this kit) | Native |
|-------------------|--------|
| `aigoerosq` `rockbox.erosq` | `erosqnative` build |
| Linux BT stack | `bcm43438.fw` on SD |
| HiBy launcher | Power-on = Rockbox |
