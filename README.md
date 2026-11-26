# Speedy Gonzales: Arriba Edition (SNES Frontend)

Native, cross-platform enhanced frontend for **Speedy Gonzales: Los Gatos Bandidos (USA, 1995, SNES)** built in C + SDL2 + `snes9x_libretro` for **Windows (`.exe`)**, **Linux**, and **Android (`.apk`)**.

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

## Descargar y jugar

### Windows
1. Descarga [SpeedyGonzales-Windows.zip](https://github.com/Vito2492/SpeedyGonzales/releases/latest/download/SpeedyGonzales-Windows.zip) y descomprímelo.
2. Abre `Speedy.exe`. La primera vez elige tu ROM `Speedy Gonzales - Los Gatos Bandidos (USA).sfc` (`.sfc` o `.smc`), o déjala junto al `.exe`.

### Linux
1. Descarga [SpeedyGonzales-Linux.tar.gz](https://github.com/Vito2492/SpeedyGonzales/releases/latest/download/SpeedyGonzales-Linux.tar.gz).
2. Descomprime y ejecuta (trae su propia copia de SDL2 en `lib/`):
   ```bash
   tar xzf SpeedyGonzales-Linux.tar.gz && cd SpeedyGonzales-Linux
   ./speedy "ruta/a/Speedy Gonzales - Los Gatos Bandidos (USA).sfc"
   ```

### Android
1. Descarga e instala [SpeedyGonzales-Android.apk](https://github.com/Vito2492/SpeedyGonzales/releases/latest/download/SpeedyGonzales-Android.apk) desde el celular (hay que permitir instalar apps de origen desconocido).
2. La primera vez te pide elegir tu ROM (`.sfc`, `.smc` o `.zip`); se valida y queda guardada dentro de la app.
3. Las versiones nuevas se instalan encima y conservan las partidas. Juega con la pantalla táctil o con un mando Bluetooth/USB.

> Ningún paquete incluye el juego: necesitas tu propia copia (versión USA).

---

## Compilación desde código fuente

- **Linux:** `sudo apt install libsdl2-dev && make` (pon `snes9x_libretro.so` junto al programa).
- **Windows (MinGW, igual que el CI):**
  ```bash
  x86_64-w64-mingw32-gcc -O2 -Isrc -I$SDL/include/SDL2 -I$SDL/include src/speedy.c -o Speedy.exe \
    -L$SDL/lib -lmingw32 -lSDL2main -lSDL2 -lcomdlg32 -luser32 -lshell32 -lm -mwindows
  ```
  (o `build_win.bat` con MSVC 2022). Junto al `.exe` van `SDL2.dll` y `snes9x_libretro.dll`.
- **Android:** `tools/android-prepare.sh SDL2-2.30.9 snes9x/libretro/libs` y luego `cd android && gradle assembleRelease` (el workflow muestra los pasos completos).
- **GitHub Actions:** [`.github/workflows/build.yml`](.github/workflows/build.yml) compila Windows, Linux y Android (arm64-v8a, armeabi-v7a, x86_64) en cada push a `main`, con SDL2 2.30.9 y snes9x compilado desde su código fuente, y publica o actualiza el release `v` + [`VERSION`](VERSION).
- **Firma del APK:** con los secretos `ANDROID_KEYSTORE_B64`, `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS` y `ANDROID_KEY_PASSWORD` el APK sale firmado siempre con la misma clave, así cada versión se instala encima de la anterior. Sin ellos se compila igual, pero el APK no se publica.


