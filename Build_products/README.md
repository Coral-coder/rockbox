# Hifi Walker H2 (HW4) — Build_products kit

**Hosted Rockbox** under stock HiBy Linux v2.3 — not bare-metal native boot.

This folder is a **complete recovery and deploy kit**. Read this page first, then open the numbered guide that matches your situation.

---

## Which guide do I need?

| Your situation | Read this |
|----------------|-----------|
| I want to **build** hosted Rockbox from source | [01-BUILD-HOSTED.md](01-BUILD-HOSTED.md) |
| Player boots **native Rockbox** or wrong NAND; I want **stock HiBy** back | [02-RESTORE-TO-STOCK.md](02-RESTORE-TO-STOCK.md) |
| I want **native** bare-metal Rockbox (advanced; not daily use) | [03-RESTORE-TO-NATIVE.md](03-RESTORE-TO-NATIVE.md) |
| Stock Linux works; I want **hosted** Rockbox + Bluetooth on SD | [04-RESTORE-TO-HOSTED.md](04-RESTORE-TO-HOSTED.md) |
| **Black screen**, recovery broken, USB-only rescue | [05-JZTOOL-EMERGENCY.md](05-JZTOOL-EMERGENCY.md) |

---

## What is in this folder?

| Path | Put on SD? | Purpose |
|------|------------|---------|
| `rockbox.erosq` | → `/.rockbox/` | Hosted app (master build, BT menu) |
| `update.upt` | → **SD root** (temporary) | Stock v2.3 + **forced uboot** NAND recovery |
| `hosted-on-sd/` | Copy contents to `/.rockbox/` | Same app + `device-bt-bringup.sh` |
| `nand-recovery/bootloader.erosq` | → **SD root** only when fixing NAND via recovery menu | Dual-boot hosted bootloader |
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
2. **No** `bootloader.erosq`, `update.upt`, or `rockbox_main.aigo_erosqn` on SD.
3. Power on → **HiBy launcher** → open Rockbox from stock music app.
4. Bluetooth: **Settings → General Settings → Bluetooth**.

Quick deploy: [04-RESTORE-TO-HOSTED.md](04-RESTORE-TO-HOSTED.md) or `.\scripts\Apply-Hosted-To-SD.ps1 -Drive E:`

---

## Critical mistakes (avoid)

| Mistake | Result |
|---------|--------|
| Flash `H2-v23-patched.upt` when you want hosted | NAND becomes **native-first** |
| Use Rockbox **Install or update** to apply `update.upt` | Wrong tool — use **HiBy stock updater** |
| Leave `update.upt` on SD after flash | Accidental re-flash |
| `bootloader.erosq` on SD root “just in case” | Can flash **native** bootloader to NAND |
| Restore `erosqnative-boot.bin` if file starts with `spl.eros` | **Corrupts NAND** (not a backup) |

---

## Build info

| Item | Value |
|------|-------|
| Source | https://github.com/Coral-coder/rockbox `master` |
| Target | `aigoerosq` hosted (`--type=N`) |
| Built | 2026-05-20 |
| `rockbox.erosq` MD5 | `CA89ABB8047B1574015D9593C1A112E7` |

See [01-BUILD-HOSTED.md](01-BUILD-HOSTED.md) to reproduce.
