# Hifi Walker H2 (HW4) — Build_products kit

**Hosted Rockbox** under stock HiBy Linux v2.3.

This folder is a **hosted recovery and deploy kit**. For bare-metal native Rockbox, see **[03-NATIVE-VGSKYE.md](03-NATIVE-VGSKYE.md)** (vgskye’s repo — not maintained here).

---

## Which guide do I need?

| Your situation | Read this |
|----------------|-----------|
| I want to **build** hosted Rockbox from source | [01-BUILD-HOSTED.md](01-BUILD-HOSTED.md) |
| Player boots wrong firmware; I want **stock HiBy** back | [02-RESTORE-TO-STOCK.md](02-RESTORE-TO-STOCK.md) |
| Stock Linux works; I want **hosted** Rockbox + Bluetooth on SD | [04-RESTORE-TO-HOSTED.md](04-RESTORE-TO-HOSTED.md) |
| **Black screen**, recovery broken, USB-only rescue | [05-JZTOOL-EMERGENCY.md](05-JZTOOL-EMERGENCY.md) |
| I want **native** bare-metal Rockbox | [03-NATIVE-VGSKYE.md](03-NATIVE-VGSKYE.md) |

---

## What is in this folder?

| Path | Put on SD? | Purpose |
|------|------------|---------|
| `rockbox.erosq` | → `/.rockbox/` | Hosted app (BT menu; wired restore after BT disconnect) |
| `update.upt` | → **SD root** (temporary) | Stock v2.3 + **forced uboot** NAND recovery |
| `hosted-on-sd/` | Copy contents to `/.rockbox/` | App + `device-bt-bringup.sh` |
| `nand-recovery/bootloader.erosq` | → **SD root** only when fixing NAND | Dual-boot hosted bootloader |
| `jztool/jztool.exe` | PC only | Load bootloader into RAM over USB |
| `scripts/Apply-Hosted-To-SD.ps1` | PC only | Automated hosted SD layout |
| `CHECKSUMS.txt` | — | MD5 verify every file |

**Verify after copy:**

```powershell
cd Build_products
Get-FileHash rockbox.erosq, update.upt, nand-recovery\bootloader.erosq -Algorithm MD5
# Compare to CHECKSUMS.txt
```

---

## Normal daily use (hosted)

1. SD has **only** `/.rockbox/rockbox.erosq` (+ optional `device-bt-bringup.sh`).
2. **No** `bootloader.erosq` or `update.upt` on SD unless you are actively recovering.
3. Power on → **HiBy launcher** → open Rockbox from the stock music app.
4. Bluetooth: **Settings → General Settings → Bluetooth**.

Quick deploy: `.\scripts\Apply-Hosted-To-SD.ps1 -Drive E: -CopyBtBringup`

---

## Critical mistakes (avoid)

| Mistake | Result |
|---------|--------|
| Flash `H2-v23-patched.upt` when you want hosted | NAND boots **native-first** (see vgskye wiki if that is what you want) |
| Use Rockbox **Install or update** to apply `update.upt` | Wrong tool — use **HiBy stock updater** |
| Leave `update.upt` on SD after flash | Accidental re-flash |
| `bootloader.erosq` on SD root “just in case” | Can flash a Rockbox bootloader to NAND unintentionally |

---

## Build info

| Item | Value |
|------|-------|
| Source | https://github.com/Coral-coder/rockbox `master` |
| Target | `aigoerosq` hosted (`--type=N`) |
| `rockbox.erosq` MD5 | see `CHECKSUMS.txt` |

See [01-BUILD-HOSTED.md](01-BUILD-HOSTED.md) to reproduce.
