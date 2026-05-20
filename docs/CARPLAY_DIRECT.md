# Carplay_Direct branch

**Goal:** H2 presents as a USB device to the car (iPhone role). Car head unit shows H2 UI.

This branch is experimental. It does not implement CarPlay or iAP2 yet.

## In-tree (this branch)

| Piece | Purpose |
|-------|---------|
| `apps/menus/carplay_lab_menu.c` | Hosted Rockbox lab menu: run USB/FB audit scripts from SD |
| `docs/CARPLAY_DIRECT.md` | This file |

## On SD card (deploy from HifiWalker repo)

Copy `carplay/scripts/*` to `/.rockbox/carplay/` on the SD, then use **Settings → CarPlay lab** in hosted Rockbox.

Logs: `/tmp/rb_carplay_lab.log` and `/.rockbox/carplay/last-run.log`

## Phases

1. **Phase 0** — USB UDC / `android_usb` / framebuffer audit (scripts + menu)
2. **Phase 1** — Gadget composite spike (iAP2 + NCM), still no full CarPlay UI
3. **Phase 2+** — Auth, session, H.264 encode (see HifiWalker `docs/carplay/`)

Base branch: `h2-hw4-hosted-bt`. Merge target when CarPlay path is proven.
