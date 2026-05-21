# Restore to native bare-metal Rockbox (advanced)

**Warning:** Native-first NAND is the opposite of the **hosted** workflow in this kit. Most H2 HW4 users should use [04-RESTORE-TO-HOSTED.md](04-RESTORE-TO-HOSTED.md) and [02-RESTORE-TO-STOCK.md](02-RESTORE-TO-STOCK.md) instead.

Native = Rockbox bootloader in NAND → power-on boots **bare-metal** `rockbox.erosq` from SD without HiBy Linux.

---

## When you need this path

- You intentionally run **erosqnative** builds.
- You use `H2-v23-patched.upt` from Rockbox download server.
- You flash `bootloader.erosq` via **Install or update**.

---

## Files for native SD layout

| File | SD location | In this kit? |
|------|-------------|--------------|
| `rockbox.erosq` (native build) | `/.rockbox/` | **No** — this kit ships **hosted** `rockbox.erosq` only |
| `bcm43438.fw` | `/.rockbox/` | No — build native yourself |
| `bootloader.erosq` | **SD root** (temporary) | `nand-recovery/bootloader.erosq` |
| `H2-v23-patched.upt` | SD root (temporary) | No — download from Rockbox |

---

## Typical native NAND path

1. Obtain **native** `rockbox.erosq` + `bcm43438.fw` from an `erosqnative` build.
2. Flash **patched** full image: `H2-v23-patched.upt` via HiBy recovery (sets native uboot).
3. Or: jztool RAM boot → copy `bootloader.erosq` to SD root → **Bootloader → Install or update**.
4. SD: `/.rockbox/rockbox.erosq` + `bcm43438.fw`.
5. Remove `bootloader.erosq` from SD root after successful NAND install.

---

## Boot keys (native bootloader in NAND)

| Goal | At power-on |
|------|-------------|
| Native Rockbox (default) | Power on only |
| Stock Linux / hosted | Hold **PLAY** ~3 s |
| Rockbox recovery menu | Hold **VOL+** |

---

## Return to hosted (recommended)

1. [02-RESTORE-TO-STOCK.md](02-RESTORE-TO-STOCK.md) with `update.upt` (`2DAC5D42…`).
2. [04-RESTORE-TO-HOSTED.md](04-RESTORE-TO-HOSTED.md).

---

## Emergency RAM boot

If NAND/recovery is broken: [05-JZTOOL-EMERGENCY.md](05-JZTOOL-EMERGENCY.md)

**Never** use **Restore** with a file named `erosqnative-boot.bin` that starts with `spl.eros` — that is a bootloader tarball, not a NAND backup.
