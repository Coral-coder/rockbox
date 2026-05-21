# Build hosted Rockbox (aigoerosq) — H2 HW4

Hosted Rockbox runs **inside stock HiBy Linux**. Native `erosqnative` builds are documented in [03-NATIVE-VGSKYE.md](03-NATIVE-VGSKYE.md) (vgskye’s repo).

---

## Requirements

| Item | Notes |
|------|-------|
| **WSL2** (Ubuntu) | Builds run in Linux |
| **Toolchain** | `mipsel-rockbox-linux-gnu-gcc` on PATH (Rockbox hosted cross compiler) |
| **Source** | `git clone https://github.com/Coral-coder/rockbox.git` |
| **Branch** | `master` (includes hosted Bluetooth) |

---

## One-time configure

From WSL:

```bash
cd /mnt/c/Users/YOU/vibes/HifiWalker   # adjust path
git clone https://github.com/Coral-coder/rockbox.git rockbox-coral
cd rockbox-coral
tools/configure --target=aigoerosq --type=N
# When prompted for build dir, use: ../rockbox-build-aigoerosq2
```

This writes `../rockbox-build-aigoerosq2/Makefile` with `ROOTDIR` pointing at `rockbox-coral`.

---

## Build

```bash
cd ../rockbox-build-aigoerosq2
make -j$(nproc)
ls -la rockbox.erosq
md5sum rockbox.erosq
```

**Expected:** file ~1.38 MB; strings should include `Scan for devices`, `Paired devices`, `Bluetooth`.

```bash
strings rockbox.erosq | grep -E 'Scan for|Paired devices'
```

---

## Windows / CRLF note

If `make` fails with `No such file or directory` on `genversion.sh` or `perl\r`:

- Line endings on `tools/*.sh` and `tools/*.pl` must be **LF**, not CRLF.
- From PowerShell (repo root):

```powershell
Get-ChildItem rockbox-coral -Include *.sh,*.pl -Recurse -File | ForEach-Object {
  $t = [IO.File]::ReadAllText($_.FullName).Replace("`r`n","`n").Replace("`r","`n")
  [IO.File]::WriteAllText($_.FullName, $t, [Text.UTF8Encoding]::new($false))
}
```

Then rerun `make` in WSL.

---

## Install build output into this kit

```powershell
Copy-Item ..\rockbox-build-aigoerosq2\rockbox.erosq .\Build_products\rockbox.erosq -Force
Copy-Item ..\rockbox-build-aigoerosq2\rockbox.erosq .\Build_products\hosted-on-sd\rockbox.erosq -Force
Get-FileHash .\Build_products\rockbox.erosq -Algorithm MD5
# Update CHECKSUMS.txt and commit
```

---

## Hosted NAND bootloader (optional, separate build)

The **app** (`rockbox.erosq`) does not change NAND. For a dual-boot menu (Hiby Player + Rockbox recovery), you need `bootloader.erosq` built for HW4 hosted path — see `nand-recovery/bootloader.erosq` in this kit (MD5 `CF5BF902…`).

Do **not** put `bootloader.erosq` on SD unless you are deliberately running **Bootloader → Install or update** in recovery.

---

## Not hosted

| Item | Where |
|------|--------|
| Native `erosqnative` | [03-NATIVE-VGSKYE.md](03-NATIVE-VGSKYE.md) |
| `H2-v23-patched.upt` | Rockbox download server (native-first NAND) |
