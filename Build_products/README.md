# Hifi Walker H2 (HW4) — prebuilt binaries

Hosted Rockbox for stock HiBy Linux (not bare-metal native boot).

## Files

| File | Size | MD5 | Notes |
|------|------|-----|-------|
| [rockbox.erosq](rockbox.erosq) | 1,376,548 bytes | `DD4872BD47A901374CA1A57CEE0128B9` | Hosted BT build (before CarPlay lab menu) |
| [update.upt](update.upt) | 61,483,008 bytes | `2DAC5D428BC60572DB42C05E9303D5A9` | Stock v2.3 **force bootloader** recovery |

## Install hosted Rockbox

Copy `rockbox.erosq` to SD: `/.rockbox/rockbox.erosq`  
Boot stock HiBy → launch Rockbox from the launcher.

## Install recovery `update.upt`

Copy to **SD card root** (not inside `.rockbox`). Flash from stock update UI, then remove the file from SD so it is not applied again.

## Verify

```powershell
(Get-FileHash .\.rockbox\rockbox.erosq -Algorithm MD5).Hash   # DD4872BD...
(Get-FileHash .\update.upt -Algorithm MD5).Hash               # 2DAC5D42...
```
