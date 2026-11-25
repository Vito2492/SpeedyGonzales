# Speedy Gonzales: Arriba Edition (SNES Frontend)

Native, cross-platform enhanced frontend for **Speedy Gonzales: Los Gatos Bandidos (USA, 1995, SNES)** built in C + SDL2 + `snes9x_libretro` for **Windows (`.exe`)**, **Linux (`.AppImage`)**, and **Android (`.apk`)**.

> **Legal Notice:** This repository contains **zero** copyrighted ROMs, graphics, fonts, or audio. On first launch, the application prompts you to link your own legally dumped `Speedy Gonzales - Los Gatos Bandidos (USA).sfc` (or `.smc`) ROM, validates it locally, extracts the menu art & 4bpp cartoon font directly from your ROM in memory, and never modifies the ROM file on disk.

---

## Features

- **Native ROM Linking & Validation:** Built-in file picker on Windows (`GetOpenFileName`), Linux (`zenity`/`kdialog`), and Android (SAF `RomPickerActivity`), plus drag-and-drop and auto-detection of adjacent `.sfc`/`.smc` files. Strips optional 512-byte SMC headers automatically and checks the SNES LoROM header & CRC32 (`0xE2DBAD76`).
- **ROM-Extracted UI:** Captures the Mexican village Title Screen artwork and decodes the authentic 4bpp cartoon font straight from VRAM at startup.
- **Fast-Start & Instant 7-World Warp (`7E:0B55`):**
  - Jump straight into any of the 7 worlds (**Sleepy Rock**, **Surely Wood**, **Fiesta City**, **Ancient Keep**, **Snowy Cabins**, **Galactical Galaxies**, **Ye Olde Bounty**) while automatically fast-forwarding past the cage intro & RNC level decompression wait.
  - **Stage 6-1 Switch Bug safe:** Tested directly on the *Galactical Galaxies* switch without lockups using modern `snes9x_libretro`.
- **4 Difficulty Presets:**
  - `NORMAL` — Classic 1995 rules (3 hearts, 3 lives).
  - `ARRIBA!` — Easy mode (5 max hearts + 9 lives).
  - `SPEEDRUN` — Permanent **Speedy Shoes** turbo speed (`7E:08B0`).
  - `BANDIDO` — Hardcore 1-heart challenge.
- **Live Powers & Cheats Menu (Real-Time WRAM):**
  - **God Mode / Invulnerability** (`7E:099E`)
  - **Infinite Lives** (`7E:0935`)
  - **5 Max Hearts + Full Refill** (`7E:08CE` / `7E:08CC`)
  - **Permanent Speedy Shoes Turbo** (`7E:08B0`)
  - **Super Jump / Levitate (B)** (`7E:086C`)
  - **Freeze Timer** (`7E:0934`)
  - **Rescue 8 Mice Instantly** (`7E:08E0 = 8`)
  - **Ghost Speedy** (`7E:0992`)
  - **Sunsoft Secret Cheat Mode** (`7E:002E = 0x08`)
  - **No Slowdown** (libretro cycle overclock)
- **Save States (3 Slots):** `F5` Save, `F7` Load, `F6` Switch Slot (also accessible from Pause Menu and Android top bar).
- **Smart Android Multi-Touch & Accessible Menus:**
  - Ergonomic 8-way D-Pad + `JUMP (B)`, `KICK (Y)`, `SPIN (A)`, `DASH (X)`, `L/R`, `SEL/STRT`, `MENU`, `SAVE`, `LOAD`.
  - `TOUCH CONTROLS: AUTO / ON / OFF` — auto-hides the virtual pad whenever a physical Bluetooth/USB gamepad is active.
  - Menus navigable by direct touch, analog stick, D-Pad, keyboard, or mouse; `ESC`/`F1`/`BACK` or `L+R+START` opens Pause anytime.
- **Bilingual UI:** Full Spanish / English switchable in Options.

---

## Building Locally

### Windows (MSVC 2022)
Place `SDL2.dll` and `snes9x_libretro.dll` next to `build_win.bat` and run:
```bat
build_win.bat
```

### Linux
```bash
sudo apt install libsdl2-dev pkg-config
make
# Optional AppImage:
bash packaging/linux/build_appimage.sh
```

### Android
See [.github/workflows/build.yml](.github/workflows/build.yml) for the automated Gradle + CMake + SDL2 + `snes9x_libretro` build pipeline (generates `SpeedyArriba-Android.apk` on every push and GitHub Release on `v*` tags).
