# jztool emergency recovery — H2 / Eros Q (HW4)

Use when the screen is dead, recovery menu is wrong, or you cannot reach HiBy stock updater — but the PC can still see the player in **USB boot mode**.

---

## Kit contents

| File | Role |
|------|------|
| `jztool/jztool.exe` | Windows loader (MD5 `93AC8D60…`) |
| `nand-recovery/bootloader.erosq` | Image to load (MD5 `CF5BF902…`) |
| `jztool/recover-load-bootloader.ps1` | Guided script |

Upstream docs: `rockbox/utils/jztool/README.md` in the Rockbox tree.

---

## Step 1 — Zadig (once per PC)

1. Download [Zadig](https://zadig.akeo.ie) (third-party; admin required).
2. Put H2 in USB boot mode (Step 2).
3. In Zadig, select device **USB ID `A108 1000`** (name may show as “X”).
4. Install driver **WinUSB** (not WinUSB (composite) variants that break access).

---

## Step 2 — USB boot mode

1. Power **off** H2.
2. Hold **MENU** (Eros Q / H2 USB boot key).
3. Plug USB cable to PC.
4. Release **MENU**.

If MENU fails, try **VOL-** or **PLAY** (some units vary).

---

## Step 3 — Load bootloader into RAM

```powershell
cd Build_products\jztool
.\recover-load-bootloader.ps1
```

Or manually:

```powershell
.\jztool.exe -v erosq load ..\nand-recovery\bootloader.erosq
```

**Success:** device runs Rockbox bootloader from RAM (screen may work even if NAND is bad).

---

## Step 4 — Fix NAND (choose one path)

### A — Hosted dual-boot (recommended after stock flash)

1. Copy `nand-recovery\bootloader.erosq` → **SD root** `E:\bootloader.erosq` (not inside `.rockbox`).
2. Insert SD, boot device (from RAM or normal).
3. Hold **VOL+** → recovery → **Bootloader → Install or update** → wait for **Success**.
4. **Remove** `E:\bootloader.erosq` from SD immediately.
5. Apply stock if needed: [02-RESTORE-TO-STOCK.md](02-RESTORE-TO-STOCK.md).
6. Deploy hosted: [04-RESTORE-TO-HOSTED.md](04-RESTORE-TO-HOSTED.md).

### B — Return to stock Linux only

After jztool RAM boot, use **Hiby Player** / **HibyOS Recovery** if visible, then flash `update.upt` per [02-RESTORE-TO-STOCK.md](02-RESTORE-TO-STOCK.md).

---

## Rules

| Do | Don't |
|----|-------|
| **Install or update** with `bootloader.erosq` on SD root | **Restore** with `erosqnative-boot.bin` that starts with `spl.eros` |
| Remove `bootloader.erosq` after NAND success | Leave `bootloader.erosq` on SD “for next time” |
| Verify MD5 before load | Use random BL from forum without HW4 match |

---

## Troubleshooting

| Problem | Fix |
|---------|-----|
| jztool “cannot open USB” | Re-run Zadig WinUSB on `A108:1000`; replug; retry USB boot key |
| Install fails NAND error | Different SD, FAT32 format, or stock `update.upt` |
| Load OK but black screen | Wrong BL HW — use kit `bootloader.erosq` only |
| Still native on boot | Flash `update.upt` (`2DAC5D42…`) via stock updater |

---

## Linux / WSL (optional)

Build from `utils/jztool` in Rockbox source (`make`; needs libusb). Run as root:

```bash
./jztool -v erosq load bootloader.erosq
```

Windows kit includes prebuilt `jztool.exe` so you do not need to compile.
