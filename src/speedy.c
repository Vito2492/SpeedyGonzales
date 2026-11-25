// Speedy Gonzales: Arriba Edition — native cross-platform frontend for
// Speedy Gonzales: Los Gatos Bandidos (USA, 1995, Sunsoft / Acclaim, SNES).
// Runs the player's own legally dumped ROM through snes9x_libretro, extracts
// its title background and authentic 4bpp cartoon font from that ROM at startup,
// and adds Difficulty Presets, instant 7-World Warp (bypassing RNC load wait),
// live RAM Powers/Cheats (God Mode, Infinite Lives, 5 Hearts, Speedy Shoes,
// Super Jump, Freeze Timer, Rescue Mice, Ghost Speedy, Sunsoft Cheat Mode),
// No-Slowdown overclock, 3 Save State slots, Controls remapping, smart Android
// multi-touch overlay, accessible menus, and Spanish/English UI.
// The ROM on disk is never modified or distributed.

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include "libretro.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#define CORE_NAME "snes9x_libretro.dll"
#define PATH_SEP '\\'
#elif defined(__ANDROID__)
#define CORE_NAME "libsnes9x_libretro.so"
#define PATH_SEP '/'
#else
#define CORE_NAME "snes9x_libretro.so"
#define PATH_SEP '/'
#endif

#define W 256
#define H 224
#define ROM_SIZE 0x100000u
#define ROM_CRC_REV0 0xE2DBAD76u

/* ------------------------------------------------------------------ */
/* Verified Speedy Gonzales WRAM Addresses (7E:xxxx)                    */
/* ------------------------------------------------------------------ */
#define RAM_CHEAT_MODE  0x002Eu  // 0x08 = Sunsoft Cheat Mode
#define RAM_SUPER_JUMP  0x086Cu  // 0x48 = Super Jump / Levitate (B)
#define RAM_SPEEDY_SHOE 0x08B0u  // 0xFF = Speedy Shoes turbo timer
#define RAM_HP          0x08CCu  // Current Hearts (1..5)
#define RAM_MAX_HP      0x08CEu  // Max Hearts (default 3, up to 5)
#define RAM_MICE        0x08E0u  // Rescued Mice counter (0..8)
#define RAM_TIMER       0x0934u  // Stage timer tick / freeze
#define RAM_LIVES       0x0935u  // Lives counter (HUD shows lives-1)
#define RAM_GHOST       0x0992u  // 0x02 = Ghost Speedy
#define RAM_INVULN      0x099Eu  // 0xFF = Invulnerability (God Mode)
#define RAM_WORLD_ID    0x0B55u  // 0..6 World index on World Map

/* ------------------------------------------------------------------ */
/* Core                                                                 */
/* ------------------------------------------------------------------ */

static struct {
    void (*init)(void), (*deinit)(void), (*run)(void), (*reset)(void);
    void (*set_environment)(retro_environment_t);
    void (*set_video_refresh)(retro_video_refresh_t);
    void (*set_audio_sample)(retro_audio_sample_t);
    void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
    void (*set_input_poll)(retro_input_poll_t);
    void (*set_input_state)(retro_input_state_t);
    bool (*load_game)(const struct retro_game_info *);
    void (*get_av)(struct retro_system_av_info *);
    void *(*mem_data)(unsigned);
    size_t (*mem_size)(unsigned);
    size_t (*ser_size)(void);
    bool (*serialize)(void *, size_t);
    bool (*unserialize)(const void *, size_t);
} core;

static enum retro_pixel_format pixfmt = RETRO_PIXEL_FORMAT_0RGB1555;
static uint32_t game[512 * 512];
static int gw = W, gh = H;
static uint16_t pad;
static bool overclock;
static bool scripting;
static bool vars_dirty;
static SDL_AudioDeviceID audio;
static bool mute;
static char base_dir[1024];
static char save_dir[1024];

static bool env_cb(unsigned cmd, void *data)
{
    switch (cmd & 0xffff) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        pixfmt = *(enum retro_pixel_format *)data;
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = save_dir;
        return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool *)data = true;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = (struct retro_variable *)data;
        if (!strcmp(v->key, "snes9x_overclock_cycles")) {
            v->value = overclock && !scripting ? "compatible" : "disabled";
            return true;
        }
        return false;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = vars_dirty;
        vars_dirty = false;
        return true;
    }
    return false;
}

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
    if (!data || w > 512 || h > 512) return;
    gw = (int)w; gh = (int)h;
    for (unsigned y = 0; y < h; y++) {
        const uint8_t *row = (const uint8_t *)data + y * pitch;
        uint32_t *out = game + y * w;
        for (unsigned x = 0; x < w; x++) {
            uint32_t r, g, b;
            if (pixfmt == RETRO_PIXEL_FORMAT_XRGB8888) {
                out[x] = 0xff000000u | ((const uint32_t *)row)[x];
                continue;
            }
            uint16_t p = ((const uint16_t *)row)[x];
            if (pixfmt == RETRO_PIXEL_FORMAT_RGB565) {
                r = p >> 11; g = (p >> 5) & 63; b = p & 31;
                r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
            } else {
                r = (p >> 10) & 31; g = (p >> 5) & 31; b = p & 31;
                r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 2); b = (b << 3) | (b >> 2);
            }
            out[x] = 0xff000000u | (r << 16) | (g << 8) | b;
        }
    }
}

static size_t audio_batch_cb(const int16_t *d, size_t frames)
{
    if (audio && !mute) SDL_QueueAudio(audio, d, (Uint32)(frames * 4));
    return frames;
}
static void audio_cb(int16_t l, int16_t r) { int16_t s[2] = { l, r }; audio_batch_cb(s, 1); }
static void poll_cb(void) {}
static int16_t input_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)index;
    if (port || device != RETRO_DEVICE_JOYPAD) return 0;
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)pad;
    return id < 16 ? (int16_t)((pad >> id) & 1) : 0;
}

static uint8_t *wram(void) { return (uint8_t *)core.mem_data(RETRO_MEMORY_SYSTEM_RAM); }

static bool load_core(const char *path)
{
    void *so = SDL_LoadObject(path);
    if (!so) return false;
#define SYM(field, name) if (!(*(void **)&core.field = SDL_LoadFunction(so, name))) return false
    SYM(init, "retro_init"); SYM(deinit, "retro_deinit");
    SYM(run, "retro_run"); SYM(reset, "retro_reset");
    SYM(set_environment, "retro_set_environment");
    SYM(set_video_refresh, "retro_set_video_refresh");
    SYM(set_audio_sample, "retro_set_audio_sample");
    SYM(set_audio_sample_batch, "retro_set_audio_sample_batch");
    SYM(set_input_poll, "retro_set_input_poll");
    SYM(set_input_state, "retro_set_input_state");
    SYM(load_game, "retro_load_game");
    SYM(get_av, "retro_get_system_av_info");
    SYM(mem_data, "retro_get_memory_data");
    SYM(mem_size, "retro_get_memory_size");
    SYM(ser_size, "retro_serialize_size");
    SYM(serialize, "retro_serialize");
    SYM(unserialize, "retro_unserialize");
#undef SYM
    core.set_environment(env_cb);
    core.set_video_refresh(video_cb);
    core.set_audio_sample(audio_cb);
    core.set_audio_sample_batch(audio_batch_cb);
    core.set_input_poll(poll_cb);
    core.set_input_state(input_cb);
    core.init();
    return true;
}

static void step(int frames, uint16_t held)
{
    pad = held;
    for (int i = 0; i < frames; i++) core.run();
}

static void tap(int btn, int hold, int wait)
{
    step(hold, (uint16_t)(1u << btn));
    step(wait, 0);
}

/* ------------------------------------------------------------------ */
/* ROM validation & Native File Picker                                  */
/* ------------------------------------------------------------------ */

static uint32_t crc32_buf(const uint8_t *p, size_t n)
{
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int b = 0; b < 8; b++) c = (c >> 1) ^ (0xedb88320u & -(int)(c & 1u));
    }
    return ~c;
}

static uint8_t *rom_buf;
static size_t rom_len;
static char rom_path[1024];

static bool load_rom_file(const char *path, char *err, size_t errsz)
{
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errsz, "No se pudo abrir el archivo"); return false; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < (long)ROM_SIZE || sz > (long)(ROM_SIZE + 512)) {
        fclose(f);
        snprintf(err, errsz, "Tamano invalido (esperado 1 MB)");
        return false;
    }
    uint8_t *tmp = (uint8_t *)malloc((size_t)sz);
    if (!tmp || fread(tmp, 1, (size_t)sz, f) != (size_t)sz) {
        free(tmp); fclose(f);
        snprintf(err, errsz, "Error leyendo archivo");
        return false;
    }
    fclose(f);
    size_t skip = (sz == (long)(ROM_SIZE + 512)) ? 512u : 0u;
    const uint8_t *raw = tmp + skip;
    // Check SNES LoROM internal header "Speedy Gonzales"
    bool hdr_ok = (memcmp(raw + 0x7FC0, "Speedy Gonzales", 15) == 0);
    uint32_t crc = crc32_buf(raw, ROM_SIZE);
    if (!hdr_ok && crc != ROM_CRC_REV0) {
        free(tmp);
        snprintf(err, errsz, "No es Speedy Gonzales (USA) SNES");
        return false;
    }
    uint8_t *clean = (uint8_t *)malloc(ROM_SIZE);
    if (!clean) { free(tmp); return false; }
    memcpy(clean, raw, ROM_SIZE);
    free(tmp);
    free(rom_buf);
    rom_buf = clean;
    rom_len = ROM_SIZE;
    snprintf(rom_path, sizeof rom_path, "%s", path);
    return true;
}

static bool pick_rom_dialog(char *out, size_t outsz)
{
#ifdef _WIN32
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof ofn);
    out[0] = 0;
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = "SNES ROM (*.sfc;*.smc)\0*.sfc;*.smc\0Todos (*.*)\0*.*\0";
    ofn.lpstrFile = out;
    ofn.nMaxFile = (DWORD)outsz;
    ofn.lpstrTitle = "Seleccionar ROM de Speedy Gonzales (USA)";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&ofn) != 0;
#elif defined(__ANDROID__)
    (void)out; (void)outsz;
    return false; // Android uses RomPickerActivity SAF before SDL starts
#else
    const char *cmds[] = {
        "zenity --file-selection --title='Seleccionar ROM de Speedy Gonzales (USA)' --file-filter='SNES ROM | *.sfc *.smc' 2>/dev/null",
        "kdialog --getopenfilename . '*.sfc *.smc|SNES ROM' 2>/dev/null"
    };
    for (int i = 0; i < 2; i++) {
        FILE *p = popen(cmds[i], "r");
        if (!p) continue;
        out[0] = 0;
        if (fgets(out, (int)outsz, p)) {
            size_t l = strlen(out);
            while (l && (out[l - 1] == '\n' || out[l - 1] == '\r')) out[--l] = 0;
        }
        int rc = pclose(p);
        if (rc == 0 && out[0]) return true;
    }
    return false;
#endif
}

/* ------------------------------------------------------------------ */
/* Config & Persistence                                                 */
/* ------------------------------------------------------------------ */

static const int snes_btn[12] = {
    RETRO_DEVICE_ID_JOYPAD_UP,    RETRO_DEVICE_ID_JOYPAD_DOWN,
    RETRO_DEVICE_ID_JOYPAD_LEFT,  RETRO_DEVICE_ID_JOYPAD_RIGHT,
    RETRO_DEVICE_ID_JOYPAD_B,     RETRO_DEVICE_ID_JOYPAD_Y,
    RETRO_DEVICE_ID_JOYPAD_A,     RETRO_DEVICE_ID_JOYPAD_X,
    RETRO_DEVICE_ID_JOYPAD_L,     RETRO_DEVICE_ID_JOYPAD_R,
    RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_SELECT
};

static const char *btn_es[12] = {
    "ARRIBA", "ABAJO", "IZQUIERDA", "DERECHA",
    "SALTO (B)", "PATADA (Y)", "GIRO (A)", "TURBO (X)",
    "L", "R", "START", "SELECT"
};
static const char *btn_en[12] = {
    "UP", "DOWN", "LEFT", "RIGHT",
    "JUMP (B)", "KICK (Y)", "SPIN (A)", "DASH (X)",
    "L", "R", "START", "SELECT"
};

typedef struct {
    int diff;          // 0=Normal, 1=Arriba (Facil), 2=Speedrun, 3=Bandido (Dificil)
    int scale;         // 0=Pixel, 1=Suave
    int fullscreen;    // 0/1
    int noslow;        // 0/1
    int touch;         // 0=Auto, 1=Si, 2=No
    int lang;          // 0=ES, 1=EN
    int slot;          // 0..2 save state slot
    // Live cheats
    int c_god;         // invulnerability
    int c_lives;       // infinite lives (9)
    int c_hearts;      // 5 max hearts + full refill
    int c_shoes;       // permanent Speedy Shoes
    int c_jump;        // Super Jump (B)
    int c_timer;       // Freeze Timer
    int c_ghost;       // Ghost Speedy
    int c_sunsoft;     // Sunsoft secret cheat mode
    int kb[12];
    int gp[12];
    char last_rom[1024];
} cfg_t;

static cfg_t cfg;

static void default_controls(void)
{
    int dk[12] = {
        SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
        SDL_SCANCODE_Z,  SDL_SCANCODE_X,    SDL_SCANCODE_A,    SDL_SCANCODE_S,
        SDL_SCANCODE_Q,  SDL_SCANCODE_W,    SDL_SCANCODE_RETURN, SDL_SCANCODE_RSHIFT
    };
    int dg[12] = {
        SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
        SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
        SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_X,
        SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_Y,
        SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
        SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK
    };
    memcpy(cfg.kb, dk, sizeof dk);
    memcpy(cfg.gp, dg, sizeof dg);
}

static void cfg_path(char *out, size_t n) { snprintf(out, n, "%s%cspeedy.cfg", save_dir, PATH_SEP); }

static void load_cfg(void)
{
    memset(&cfg, 0, sizeof cfg);
    default_controls();
    char p[1024]; cfg_path(p, sizeof p);
    FILE *f = fopen(p, "r");
    if (!f) return;
    char line[1200];
    while (fgets(line, sizeof line, f)) {
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq++ = 0;
        int v = atoi(eq), idx;
        if (!strcmp(line, "diff")) cfg.diff = v;
        else if (!strcmp(line, "scale")) cfg.scale = v;
        else if (!strcmp(line, "fullscreen")) cfg.fullscreen = v;
        else if (!strcmp(line, "noslow")) cfg.noslow = v;
        else if (!strcmp(line, "touch")) cfg.touch = v;
        else if (!strcmp(line, "lang")) cfg.lang = v;
        else if (!strcmp(line, "slot")) cfg.slot = v % 3;
        else if (!strcmp(line, "c_god")) cfg.c_god = v;
        else if (!strcmp(line, "c_lives")) cfg.c_lives = v;
        else if (!strcmp(line, "c_hearts")) cfg.c_hearts = v;
        else if (!strcmp(line, "c_shoes")) cfg.c_shoes = v;
        else if (!strcmp(line, "c_jump")) cfg.c_jump = v;
        else if (!strcmp(line, "c_timer")) cfg.c_timer = v;
        else if (!strcmp(line, "c_ghost")) cfg.c_ghost = v;
        else if (!strcmp(line, "c_sunsoft")) cfg.c_sunsoft = v;
        else if (!strcmp(line, "rom")) snprintf(cfg.last_rom, sizeof cfg.last_rom, "%s", eq);
        else if (sscanf(line, "kb%d", &idx) == 1 && idx >= 0 && idx < 12) cfg.kb[idx] = v;
        else if (sscanf(line, "gp%d", &idx) == 1 && idx >= 0 && idx < 12) cfg.gp[idx] = v;
    }
    fclose(f);
    overclock = cfg.noslow != 0;
}

static void save_cfg(void)
{
    char p[1024]; cfg_path(p, sizeof p);
    FILE *f = fopen(p, "w");
    if (!f) return;
    fprintf(f, "diff=%d\nscale=%d\nfullscreen=%d\nnoslow=%d\ntouch=%d\nlang=%d\nslot=%d\n",
            cfg.diff, cfg.scale, cfg.fullscreen, cfg.noslow, cfg.touch, cfg.lang, cfg.slot);
    fprintf(f, "c_god=%d\nc_lives=%d\nc_hearts=%d\nc_shoes=%d\nc_jump=%d\nc_timer=%d\nc_ghost=%d\nc_sunsoft=%d\n",
            cfg.c_god, cfg.c_lives, cfg.c_hearts, cfg.c_shoes, cfg.c_jump, cfg.c_timer, cfg.c_ghost, cfg.c_sunsoft);
    if (rom_path[0]) fprintf(f, "rom=%s\n", rom_path);
    for (int i = 0; i < 12; i++) fprintf(f, "kb%d=%d\ngp%d=%d\n", i, cfg.kb[i], i, cfg.gp[i]);
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* ROM-Extracted Font & Title Art (100% Runtime from User's ROM)        */
/* ------------------------------------------------------------------ */

static uint32_t bg_art[W * H];     // Title screen art captured from user's ROM
static uint8_t  glyph[128][8 * 10]; // 0=bg, 1=outline, 2=body (sampled from 4bpp 8x16 VRAM font)
static bool     has_font;

static void decode_4bpp_tile(const uint8_t *tb, uint8_t out[8][8])
{
    for (int r = 0; r < 8; r++) {
        uint8_t p0 = tb[r * 2], p1 = tb[r * 2 + 1];
        uint8_t p2 = tb[16 + r * 2], p3 = tb[16 + r * 2 + 1];
        for (int c = 0; c < 8; c++) {
            int bit = 7 - c;
            out[r][c] = (uint8_t)(((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1) |
                                  (((p2 >> bit) & 1) << 2) | (((p3 >> bit) & 1) << 3));
        }
    }
}

static void extract_glyph_8x16(const uint8_t *vram, int voff, int ch)
{
    uint8_t top[8][8], bot[8][8], full[16][8];
    decode_4bpp_tile(vram + voff, top);
    decode_4bpp_tile(vram + voff + 32, bot);
    memcpy(full, top, 64);
    memcpy(full + 8, bot, 64);
    // Sample 10 balanced rows from 16 so the cartoon outline stays crisp at 256x224
    static const int rows[10] = { 0, 1, 3, 5, 6, 8, 10, 12, 14, 15 };
    for (int r = 0; r < 10; r++) {
        for (int c = 0; c < 8; c++) {
            uint8_t v = full[rows[r]][c];
            // In Speedy's 4bpp title font: 0=transparent, >=8=black outline, 1..7=bright fill
            uint8_t px = 0;
            if (v >= 8) px = 1;
            else if (v > 0) px = 2;
            glyph[ch][r * 8 + c] = px;
        }
    }
}

static void extract_assets_from_vram(const uint8_t *snap, size_t snap_sz)
{
    // Locate VRA:065536: inside snes9x save state
    const uint8_t *vram = NULL;
    for (size_t i = 0; i + 11 + 65536 <= snap_sz; i++) {
        if (snap[i] == 'V' && snap[i+1] == 'R' && snap[i+2] == 'A' && snap[i+3] == ':') {
            vram = snap + i + 11;
            break;
        }
    }
    if (!vram) return;
    memset(glyph, 0, sizeof glyph);
    // Verified VRAM offsets on Title Screen (64 bytes per 8x16 4bpp character)
    for (int i = 0; i < 10; i++) extract_glyph_8x16(vram, 0xAF40 + i * 64, '0' + i);
    for (int i = 0; i < 26; i++) extract_glyph_8x16(vram, 0xB280 + i * 64, 'A' + i);
    for (int i = 0; i < 26; i++) extract_glyph_8x16(vram, 0xB280 + i * 64, 'a' + i);
    extract_glyph_8x16(vram, 0xAF00, '!');
    extract_glyph_8x16(vram, 0xAE40, '\'');
    extract_glyph_8x16(vram, 0xB1C0, ':');
    extract_glyph_8x16(vram, 0xB240, '?');
    // Synthesize crisp '.', ',', '-', '/', '+', '(', ')' with matching black outline
    // '.'
    for (int r = 6; r <= 9; r++) for (int c = 2; c <= 5; c++) glyph['.'][r * 8 + c] = 1;
    glyph['.'][7 * 8 + 3] = 2; glyph['.'][7 * 8 + 4] = 2;
    glyph['.'][8 * 8 + 3] = 2; glyph['.'][8 * 8 + 4] = 2;
    // ','
    for (int r = 6; r <= 9; r++) for (int c = 2; c <= 5; c++) glyph[','][r * 8 + c] = 1;
    glyph[','][7 * 8 + 4] = 2; glyph[','][8 * 8 + 3] = 2;
    // '-'
    for (int r = 3; r <= 6; r++) for (int c = 1; c <= 6; c++) glyph['-'][r * 8 + c] = 1;
    for (int c = 2; c <= 5; c++) { glyph['-'][4 * 8 + c] = 2; glyph['-'][5 * 8 + c] = 2; }
    // '(' and ')'
    for (int r = 1; r <= 8; r++) {
        int cl = (r == 1 || r == 8) ? 5 : (r == 2 || r == 7) ? 4 : 3;
        glyph['('][r * 8 + cl - 1] = 1; glyph['('][r * 8 + cl] = 2; glyph['('][r * 8 + cl + 1] = 1;
        int cr = 7 - cl;
        glyph[')'][r * 8 + cr - 1] = 1; glyph[')'][r * 8 + cr] = 2; glyph[')'][r * 8 + cr + 1] = 1;
    }
    // '/'
    for (int r = 1; r < 9; r++) {
        int c = 6 - (r * 5) / 8;
        if (c >= 1 && c < 7) { glyph['/'][r * 8 + c] = 2; glyph['/'][r * 8 + c - 1] = 1; glyph['/'][r * 8 + c + 1] = 1; }
    }
    // '+'
    for (int r = 2; r <= 7; r++) { glyph['+'][r * 8 + 3] = 1; glyph['+'][r * 8 + 4] = 2; glyph['+'][r * 8 + 5] = 1; }
    for (int c = 1; c <= 6; c++) { glyph['+'][4 * 8 + c] = 1; glyph['+'][5 * 8 + c] = 2; glyph['+'][6 * 8 + c] = 1; }
    glyph['+'][4 * 8 + 4] = 2; glyph['+'][5 * 8 + 4] = 2;
    has_font = true;
}

/* ------------------------------------------------------------------ */
/* Worlds, Snapshots & Live Gameplay                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *name;
    const char *desc_es;
    const char *desc_en;
} world_t;

static const world_t worlds[7] = {
    { "1. SLEEPY ROCK",        "DESIERTO",  "DESERT"   },
    { "2. SURELY WOOD",        "BOSQUE",    "FOREST"   },
    { "3. FIESTA CITY",        "CIUDAD",    "CITY"     },
    { "4. ANCIENT KEEP",       "CASTILLO",  "CASTLE"   },
    { "5. SNOWY CABINS",       "NIEVE",     "SNOW"     },
    { "6. GALACTICAL",         "ESPACIO",   "SPACE"    },
    { "7. YE OLDE BOUNTY",     "PIRATAS",   "PIRATES"  },
};

static uint8_t *title_snap, *map_snap;
static size_t snap_size;
static bool started;
static uint32_t total_frames;

static void boot_and_cache_snapshots(void)
{
    mute = true;
    scripting = true;
    vars_dirty = true;

    // Boot past Warner Bros / Sunsoft logos & story intro to Title Screen
    step(1440, 0);
    tap(RETRO_DEVICE_ID_JOYPAD_START, 8, 60);
    tap(RETRO_DEVICE_ID_JOYPAD_START, 8, 120);
    tap(RETRO_DEVICE_ID_JOYPAD_START, 8, 300);

    // Save Title Screen artwork (256x224)
    memcpy(bg_art, game, sizeof bg_art);

    snap_size = core.ser_size();
    free(title_snap); free(map_snap);
    title_snap = (uint8_t *)malloc(snap_size);
    map_snap   = (uint8_t *)malloc(snap_size);
    if (title_snap) {
        core.serialize(title_snap, snap_size);
        extract_assets_from_vram(title_snap, snap_size);
    }

    // Advance from Title Screen ("Start") to World Map ("Sleepy Rock")
    tap(RETRO_DEVICE_ID_JOYPAD_START, 8, 60);
    tap(RETRO_DEVICE_ID_JOYPAD_START, 8, 120);
    step(420, 0);
    if (map_snap) core.serialize(map_snap, snap_size);

    // Restore Title Screen cleanly
    if (title_snap) core.unserialize(title_snap, snap_size);
    scripting = false;
    vars_dirty = true;
    mute = false;
}

static bool frame_is_gameplay(void)
{
    int nz = 0;
    for (int i = 0; i < W * H; i += 41)
        if ((game[i] & 0x00ffffffu) != 0) nz++;
    return nz > 400;
}

static void apply_difficulty_preset(void)
{
    uint8_t *m = wram();
    if (!m) return;
    if (cfg.diff == 1) { // ARRIBA (Facil)
        m[RAM_MAX_HP] = 5;
        m[RAM_HP] = 5;
        m[RAM_LIVES] = 9;
    } else if (cfg.diff == 2) { // SPEEDRUN (Turbo)
        m[RAM_SPEEDY_SHOE] = 0xFF;
    } else if (cfg.diff == 3) { // BANDIDO (Dificil)
        m[RAM_MAX_HP] = 1;
        if (m[RAM_HP] > 1) m[RAM_HP] = 1;
    }
}

static void apply_cheats_frame(void)
{
    uint8_t *m = wram();
    if (!m || !started) return;
    if (cfg.diff == 2 || cfg.c_shoes) m[RAM_SPEEDY_SHOE] = 0xFF;
    if (cfg.diff == 3) {
        if (m[RAM_MAX_HP] > 1) m[RAM_MAX_HP] = 1;
        if (m[RAM_HP] > 1) m[RAM_HP] = 1;
    }
    if (cfg.c_god)     m[RAM_INVULN] = 0xFF;
    if (cfg.c_lives)   m[RAM_LIVES] = 9;
    if (cfg.c_hearts)  { m[RAM_MAX_HP] = 5; m[RAM_HP] = 5; }
    if (cfg.c_jump)    m[RAM_SUPER_JUMP] = 0x48;
    if (cfg.c_timer)   m[RAM_TIMER] = 0;
    if (cfg.c_ghost)   m[RAM_GHOST] = 0x02;
    if (cfg.c_sunsoft) m[RAM_CHEAT_MODE] = 0x08;
}

static void launch_world(int world_id, bool fast_skip_load)
{
    if (!map_snap) return;
    if (audio) SDL_ClearQueuedAudio(audio);
    mute = true;
    scripting = true;
    vars_dirty = true;

    core.unserialize(map_snap, snap_size);
    uint8_t *m = wram();
    if (m) {
        if (cfg.c_sunsoft) m[RAM_CHEAT_MODE] = 0x08;
        m[RAM_WORLD_ID] = (uint8_t)(world_id % 7);
    }
    if (fast_skip_load) {
        // Press B on World Map and fast-forward past cage card + RNC decompression
        tap(RETRO_DEVICE_ID_JOYPAD_B, 8, 120);
        // Wait until cage card ends (black) and real level renders
        step(900, 0);
        for (int i = 0; i < 600; i++) {
            step(1, 0);
            if (i > 120 && frame_is_gameplay()) { step(20, 0); break; }
        }
    }
    started = true;
    apply_difficulty_preset();
    apply_cheats_frame();

    scripting = false;
    vars_dirty = true;
    mute = false;
}

static void open_world_map(void)
{
    if (!map_snap) return;
    if (audio) SDL_ClearQueuedAudio(audio);
    core.unserialize(map_snap, snap_size);
    started = true;
    apply_difficulty_preset();
    apply_cheats_frame();
}

static void state_path(int slot, char *out, size_t n)
{
    snprintf(out, n, "%s%cspeedy_slot%d.sav", save_dir, PATH_SEP, slot + 1);
}

static bool save_state(int slot)
{
    if (!started || !snap_size) return false;
    uint8_t *buf = (uint8_t *)malloc(snap_size);
    if (!buf || !core.serialize(buf, snap_size)) { free(buf); return false; }
    char p[1024]; state_path(slot, p, sizeof p);
    FILE *f = fopen(p, "wb");
    bool ok = f && fwrite(buf, 1, snap_size, f) == snap_size;
    if (f) fclose(f);
    free(buf);
    return ok;
}

static bool load_state(int slot)
{
    if (!snap_size) return false;
    char p[1024]; state_path(slot, p, sizeof p);
    FILE *f = fopen(p, "rb");
    if (!f) return false;
    uint8_t *buf = (uint8_t *)malloc(snap_size);
    bool ok = buf && fread(buf, 1, snap_size, f) == snap_size && core.unserialize(buf, snap_size);
    fclose(f);
    free(buf);
    if (ok) { started = true; if (audio) SDL_ClearQueuedAudio(audio); }
    return ok;
}

/* ------------------------------------------------------------------ */
/* UI Rendering (256x224)                                               */
/* ------------------------------------------------------------------ */

static uint32_t ui[W * H];
static char toast[80];
static Uint32 toast_until;

static void show_toast(const char *msg)
{
    snprintf(toast, sizeof toast, "%s", msg);
    toast_until = SDL_GetTicks() + 2200u;
}

#define T(es, en) (cfg.lang == 1 ? (en) : (es))

static const uint32_t GOLD  = 0xFFFFD840u;
static const uint32_t WHITE = 0xFFFFFFFFu;
static const uint32_t DIM   = 0xFFB8C4D8u;
static const uint32_t CYAN  = 0xFF55E8FFu;
static const uint32_t GREEN = 0xFF68F088u;
static const uint32_t RED   = 0xFFFF5848u;

static void put_px(int x, int y, uint32_t c)
{
    if (x >= 0 && x < W && y >= 0 && y < H) ui[y * W + x] = c;
}

static void fill_rect(int x, int y, int w, int h, uint32_t argb)
{
    uint32_t a = (argb >> 24) & 0xFFu;
    uint32_t sr = (argb >> 16) & 0xFFu, sg = (argb >> 8) & 0xFFu, sb = argb & 0xFFu;
    for (int j = y; j < y + h; j++) {
        if (j < 0 || j >= H) continue;
        for (int i = x; i < x + w; i++) {
            if (i < 0 || i >= W) continue;
            if (a == 255) { ui[j * W + i] = argb; continue; }
            uint32_t dst = ui[j * W + i];
            uint32_t dr = (dst >> 16) & 0xFFu, dg = (dst >> 8) & 0xFFu, db = dst & 0xFFu;
            uint32_t r = (sr * a + dr * (255 - a)) / 255;
            uint32_t g = (sg * a + dg * (255 - a)) / 255;
            uint32_t b = (sb * a + db * (255 - a)) / 255;
            ui[j * W + i] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }
}

static void draw_box(int x, int y, int w, int h, uint32_t bg, uint32_t border)
{
    fill_rect(x, y, w, h, bg);
    fill_rect(x, y, w, 1, border);
    fill_rect(x, y + h - 1, w, 1, border);
    fill_rect(x, y, 1, h, border);
    fill_rect(x + w - 1, y, 1, h, border);
}

static void draw_char(int x, int y, unsigned char ch, uint32_t col)
{
    if (ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 32);
    if (ch >= 128 || !has_font) return;
    const uint8_t *g = glyph[ch];
    for (int r = 0; r < 10; r++) {
        for (int c = 0; c < 8; c++) {
            uint8_t v = g[r * 8 + c];
            if (v == 1) put_px(x + c, y + r, 0xFF100808u);
            else if (v == 2) put_px(x + c, y + r, col);
        }
    }
}

static void text(int x, int y, const char *s, uint32_t col)
{
    for (; *s; s++, x += 8) draw_char(x, y, (unsigned char)*s, col);
}
static void text_c(int y, const char *s, uint32_t col) { text((W - (int)strlen(s) * 8) / 2, y, s, col); }
static void text_r(int rx, int y, const char *s, uint32_t col) { text(rx - (int)strlen(s) * 8, y, s, col); }

static void draw_cursor(int x, int y)
{
    // Speedy's iconic red arrow from the Title Screen
    for (int r = 0; r < 7; r++) {
        int d = r < 3 ? r : 6 - r;
        int x0 = (r >= 2 && r <= 4) ? 0 : 4;
        int x1 = 5 + d;
        for (int c = x0; c <= x1; c++) {
            uint32_t col = (c == x0 || c == x1 || r == 0 || r == 6) ? 0xFF200505u :
                           (r == 3 && c < x1 - 1) ? 0xFFFF9040u : 0xFFE82820u;
            put_px(x + c, y + r + 1, col);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Menus                                                                */
/* ------------------------------------------------------------------ */

enum { ST_MENU, ST_PLAY };
enum { P_START, P_PAUSE, P_WORLDS, P_CHEATS, P_OPTIONS, P_CONTROLS };

static int state = ST_MENU;
static int page = P_START;
static int sel = 0;
static int binding = -1;
static bool bind_pad = false;
static int menu_last_y0, menu_last_step, menu_last_first, menu_last_rows, menu_last_n;

typedef struct { const char *label, *value, *value2; int id; } item_t;

static const char *diff_name(void)
{
    if (cfg.lang == 1) {
        static const char *d[] = { "NORMAL", "ARRIBA!", "SPEEDRUN", "BANDIDO" };
        return d[cfg.diff & 3];
    }
    static const char *d[] = { "NORMAL", "ARRIBA!", "SPEEDRUN", "BANDIDO" };
    return d[cfg.diff & 3];
}

static const char *touch_name(void)
{
    static const char *es[] = { "AUTO", "SI", "NO" };
    static const char *en[] = { "AUTO", "ON", "OFF" };
    return cfg.lang == 1 ? en[cfg.touch % 3] : es[cfg.touch % 3];
}

static int build(item_t *it)
{
    int n = 0;
    static char slot_buf[16], kb_buf[12][16], gp_buf[12][16];
    const char *on_s = T("SI", "ON"), *off_s = T("NO", "OFF");

    if (page == P_START) {
        it[n++] = (item_t){ T("JUGAR (1-1 RAPIDO)", "PLAY (FAST 1-1)"),   NULL, NULL, 1 };
        it[n++] = (item_t){ T("MAPA DEL MUNDO",     "WORLD MAP"),         NULL, NULL, 2 };
        it[n++] = (item_t){ T("IR A MUNDO (1-7)",   "WARP TO WORLD"),     NULL, NULL, 3 };
        it[n++] = (item_t){ T("DIFICULTAD",         "DIFFICULTY"),        diff_name(), NULL, 4 };
        it[n++] = (item_t){ T("PODERES Y TRUCOS",   "POWERS & CHEATS"),   NULL, NULL, 5 };
        it[n++] = (item_t){ T("OPCIONES",           "OPTIONS"),           NULL, NULL, 6 };
        it[n++] = (item_t){ T("CONTROLES",          "CONTROLS"),          NULL, NULL, 7 };
        it[n++] = (item_t){ T("SALIR",              "QUIT"),              NULL, NULL, 8 };
    } else if (page == P_PAUSE) {
        snprintf(slot_buf, sizeof slot_buf, "SLOT %d", cfg.slot + 1);
        it[n++] = (item_t){ T("CONTINUAR",          "RESUME"),            NULL, NULL, 1 };
        it[n++] = (item_t){ T("GUARDAR ESTADO",     "SAVE STATE"),        slot_buf, NULL, 2 };
        it[n++] = (item_t){ T("CARGAR ESTADO",      "LOAD STATE"),        slot_buf, NULL, 3 };
        it[n++] = (item_t){ T("CAMBIAR SLOT",       "SWITCH SLOT"),       slot_buf, NULL, 4 };
        it[n++] = (item_t){ T("PODERES Y TRUCOS",   "POWERS & CHEATS"),   NULL, NULL, 5 };
        it[n++] = (item_t){ T("IR A MUNDO (1-7)",   "WARP TO WORLD"),     NULL, NULL, 6 };
        it[n++] = (item_t){ T("OPCIONES",           "OPTIONS"),           NULL, NULL, 7 };
        it[n++] = (item_t){ T("CONTROLES",          "CONTROLS"),          NULL, NULL, 8 };
        it[n++] = (item_t){ T("MENU INICIAL",       "TITLE MENU"),        NULL, NULL, 9 };
    } else if (page == P_WORLDS) {
        for (int i = 0; i < 7; i++)
            it[n++] = (item_t){ worlds[i].name, T(worlds[i].desc_es, worlds[i].desc_en), NULL, i };
        it[n++] = (item_t){ T("VOLVER", "BACK"), NULL, NULL, 99 };
    } else if (page == P_CHEATS) {
        it[n++] = (item_t){ T("MODO DIOS",          "GOD MODE"),          cfg.c_god     ? on_s : off_s, NULL, 1 };
        it[n++] = (item_t){ T("VIDAS INFINITAS",    "INFINITE LIVES"),    cfg.c_lives   ? on_s : off_s, NULL, 2 };
        it[n++] = (item_t){ T("5 CORAZONES",        "5 MAX HEARTS"),      cfg.c_hearts  ? on_s : off_s, NULL, 3 };
        it[n++] = (item_t){ T("ZAPATILLAS TURBO",   "SPEEDY SHOES"),      cfg.c_shoes   ? on_s : off_s, NULL, 4 };
        it[n++] = (item_t){ T("SUPER SALTO (B)",    "SUPER JUMP (B)"),    cfg.c_jump    ? on_s : off_s, NULL, 5 };
        it[n++] = (item_t){ T("RELOJ CONGELADO",    "FREEZE TIMER"),      cfg.c_timer   ? on_s : off_s, NULL, 6 };
        it[n++] = (item_t){ T("RESCATAR 8 RATONES", "RESCUE 8 MICE"),     T("YA!", "NOW!"), NULL, 7 };
        it[n++] = (item_t){ T("SPEEDY FANTASMA",    "GHOST SPEEDY"),      cfg.c_ghost   ? on_s : off_s, NULL, 8 };
        it[n++] = (item_t){ T("CHEAT SUNSOFT",      "SUNSOFT CHEAT"),     cfg.c_sunsoft ? on_s : off_s, NULL, 9 };
        it[n++] = (item_t){ T("VOLVER",             "BACK"),              NULL, NULL, 99 };
    } else if (page == P_OPTIONS) {
        it[n++] = (item_t){ T("DIFICULTAD",         "DIFFICULTY"),        diff_name(), NULL, 1 };
        it[n++] = (item_t){ T("FILTRO VIDEO",       "VIDEO FILTER"),      cfg.scale ? T("SUAVE", "SMOOTH") : "PIXEL", NULL, 2 };
        it[n++] = (item_t){ T("PANTALLA COMPLETA",  "FULLSCREEN"),        cfg.fullscreen ? on_s : off_s, NULL, 3 };
        it[n++] = (item_t){ T("SIN SLOWDOWN",       "NO SLOWDOWN"),       cfg.noslow ? on_s : off_s, NULL, 4 };
        it[n++] = (item_t){ T("CONTROLES TACTILES", "TOUCH CONTROLS"),    touch_name(), NULL, 5 };
        it[n++] = (item_t){ T("IDIOMA",             "LANGUAGE"),          cfg.lang ? "ENGLISH" : "ESPANOL", NULL, 6 };
        it[n++] = (item_t){ T("CAMBIAR ROM",        "CHANGE ROM"),        NULL, NULL, 7 };
        it[n++] = (item_t){ T("VOLVER",             "BACK"),              NULL, NULL, 99 };
    } else if (page == P_CONTROLS) {
        for (int i = 0; i < 12; i++) {
            const char *kn = SDL_GetScancodeName((SDL_Scancode)cfg.kb[i]);
            snprintf(kb_buf[i], sizeof kb_buf[i], "%.7s", (kn && *kn) ? kn : "-");
            const char *gn = cfg.gp[i] >= 0 ? SDL_GameControllerGetStringForButton((SDL_GameControllerButton)cfg.gp[i]) : "-";
            snprintf(gp_buf[i], sizeof gp_buf[i], "%.6s", (gn && *gn) ? gn : "-");
            it[n++] = (item_t){ cfg.lang ? btn_en[i] : btn_es[i],
                                (binding == i && !bind_pad) ? "..." : kb_buf[i],
                                (binding == i &&  bind_pad) ? "..." : gp_buf[i], i };
        }
        it[n++] = (item_t){ T("RESTAURAR", "RESET DEFAULTS"), NULL, NULL, 98 };
        it[n++] = (item_t){ T("VOLVER", "BACK"), NULL, NULL, 99 };
    }
    return n;
}

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex_game, *tex_ui;
static bool running = true;

static void apply_video_settings(void)
{
    if (!win || !ren) return;
    SDL_SetWindowFullscreen(win, cfg.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, cfg.scale ? "1" : "0");
    if (tex_game) SDL_DestroyTexture(tex_game);
    tex_game = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
}

static int parent_page(void) { return started ? P_PAUSE : P_START; }

static void back(void)
{
    if (binding >= 0) { binding = -1; return; }
    if (page == P_PAUSE) { state = ST_PLAY; return; }
    if (page == P_START) return;
    page = parent_page();
    sel = 0;
}

static void pause_game(void)
{
    if (audio) SDL_ClearQueuedAudio(audio);
    state = ST_MENU;
    page = P_PAUSE;
    sel = 0;
}

static void activate(void)
{
    item_t it[24];
    int n = build(it);
    if (sel < 0 || sel >= n) return;
    int id = it[sel].id;

    if (page == P_START) {
        if (id == 1) { launch_world(0, true); state = ST_PLAY; }
        else if (id == 2) { open_world_map(); state = ST_PLAY; }
        else if (id == 3) { page = P_WORLDS; sel = 0; }
        else if (id == 4) { cfg.diff = (cfg.diff + 1) % 4; save_cfg(); }
        else if (id == 5) { page = P_CHEATS; sel = 0; }
        else if (id == 6) { page = P_OPTIONS; sel = 0; }
        else if (id == 7) { page = P_CONTROLS; sel = 0; }
        else if (id == 8) { running = false; }
    } else if (page == P_PAUSE) {
        if (id == 1) state = ST_PLAY;
        else if (id == 2) show_toast(save_state(cfg.slot) ? T("ESTADO GUARDADO", "STATE SAVED") : T("ERROR AL GUARDAR", "SAVE FAILED"));
        else if (id == 3) {
            if (load_state(cfg.slot)) { state = ST_PLAY; show_toast(T("ESTADO CARGADO", "STATE LOADED")); }
            else show_toast(T("SLOT VACIO", "EMPTY SLOT"));
        }
        else if (id == 4) { cfg.slot = (cfg.slot + 1) % 3; save_cfg(); }
        else if (id == 5) { page = P_CHEATS; sel = 0; }
        else if (id == 6) { page = P_WORLDS; sel = 0; }
        else if (id == 7) { page = P_OPTIONS; sel = 0; }
        else if (id == 8) { page = P_CONTROLS; sel = 0; }
        else if (id == 9) { started = false; page = P_START; sel = 0; }
    } else if (page == P_WORLDS) {
        if (id == 99) back();
        else { launch_world(id, true); state = ST_PLAY; show_toast(worlds[id].name); }
    } else if (page == P_CHEATS) {
        if (id == 99) back();
        else if (id == 1) cfg.c_god = !cfg.c_god;
        else if (id == 2) cfg.c_lives = !cfg.c_lives;
        else if (id == 3) cfg.c_hearts = !cfg.c_hearts;
        else if (id == 4) cfg.c_shoes = !cfg.c_shoes;
        else if (id == 5) cfg.c_jump = !cfg.c_jump;
        else if (id == 6) cfg.c_timer = !cfg.c_timer;
        else if (id == 7) {
            uint8_t *m = wram();
            if (m && started) { m[RAM_MICE] = 8; show_toast(T("8 RATONES RESCATADOS!", "8 MICE RESCUED!")); }
            else show_toast(T("INICIA UN NIVEL PRIMERO", "START A LEVEL FIRST"));
        }
        else if (id == 8) cfg.c_ghost = !cfg.c_ghost;
        else if (id == 9) cfg.c_sunsoft = !cfg.c_sunsoft;
        apply_cheats_frame();
        save_cfg();
    } else if (page == P_OPTIONS) {
        if (id == 99) back();
        else if (id == 1) cfg.diff = (cfg.diff + 1) % 4;
        else if (id == 2) { cfg.scale = !cfg.scale; apply_video_settings(); }
        else if (id == 3) { cfg.fullscreen = !cfg.fullscreen; apply_video_settings(); }
        else if (id == 4) { cfg.noslow = !cfg.noslow; overclock = cfg.noslow != 0; vars_dirty = true; }
        else if (id == 5) cfg.touch = (cfg.touch + 1) % 3;
        else if (id == 6) cfg.lang = !cfg.lang;
        else if (id == 7) {
            char chosen[1024], err[128];
            if (pick_rom_dialog(chosen, sizeof chosen)) {
                if (load_rom_file(chosen, err, sizeof err)) {
                    struct retro_game_info gi = { rom_path, rom_buf, rom_len, NULL };
                    core.load_game(&gi);
                    boot_and_cache_snapshots();
                    started = false;
                    page = P_START; sel = 0;
                    save_cfg();
                    show_toast(T("ROM CARGADA!", "ROM LOADED!"));
                } else show_toast(err);
            }
        }
        save_cfg();
    } else if (page == P_CONTROLS) {
        if (id == 99) back();
        else if (id == 98) { default_controls(); save_cfg(); show_toast(T("CONTROLES RESTAURADOS", "CONTROLS RESET")); }
        else { binding = id; bind_pad = false; }
    }
}

static void draw_menu(void)
{
    // Base backdrop: Speedy Gonzales title art (or live paused gameplay) darkened neatly
    const uint32_t *src = (state == ST_MENU && page != P_START && started) ? game : bg_art;
    for (int i = 0; i < W * H; i++) {
        uint32_t c = src[i];
        uint32_t r = ((c >> 16) & 0xFFu) * 45 / 100;
        uint32_t g = ((c >>  8) & 0xFFu) * 45 / 100;
        uint32_t b = ( c        & 0xFFu) * 52 / 100;
        ui[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }

    // Mexican Fiesta gold/crimson framed panel
    draw_box(12, 12, W - 24, H - 24, 0xD8140C10u, GOLD);
    draw_box(14, 14, W - 28, H - 28, 0x00000000u, 0xFFC83020u);

    static const char *t_es[] = {
        "SPEEDY ARRIBA!", "PAUSA", "ELEGIR MUNDO", "PODERES Y TRUCOS", "OPCIONES", "CONTROLES"
    };
    static const char *t_en[] = {
        "SPEEDY ARRIBA!", "PAUSED", "WORLD WARP", "POWERS & CHEATS", "OPTIONS", "CONTROLS"
    };
    const char *title = cfg.lang ? t_en[page] : t_es[page];
    fill_rect(22, 18, W - 44, 15, 0xFF701414u);
    text_c(21, title, GOLD);

    item_t it[24];
    int n = build(it);
    int x = 32, y0 = 42, step_y = 18, vx = 234, rows = n, first = 0;
    if (page == P_START)    { step_y = 18; y0 = 42; }
    if (page == P_PAUSE)    { step_y = 16; y0 = 40; }
    if (page == P_WORLDS)   { step_y = 18; y0 = 42; x = 28; }
    if (page == P_CHEATS)   { step_y = 14; y0 = 40; x = 28; }
    if (page == P_OPTIONS)  { step_y = 17; y0 = 42; x = 28; }
    if (page == P_CONTROLS) {
        x = 24; vx = 172; step_y = 13; y0 = 48; rows = 10;
        first = sel - rows / 2;
        if (first > n - rows) first = n - rows;
        if (first < 0) first = 0;
        text_r(172, 36, T("TECLA", "KEY"), DIM);
        text(182, 36, T("MANDO", "PAD"), DIM);
    }

    menu_last_y0 = y0; menu_last_step = step_y;
    menu_last_first = first; menu_last_rows = rows; menu_last_n = n;

    for (int i = first; i < n && i < first + rows; i++) {
        int y = y0 + (i - first) * step_y;
        bool on = (i == sel);
        if (on) fill_rect(18, y - 1, W - 36, 12, 0x70E84020u);
        text(x, y, it[i].label, on ? GOLD : WHITE);
        if (it[i].value) {
            uint32_t vc = on ? GOLD : CYAN;
            if (!strcmp(it[i].value, "SI") || !strcmp(it[i].value, "ON")) vc = GREEN;
            else if (!strcmp(it[i].value, "NO") || !strcmp(it[i].value, "OFF")) vc = DIM;
            text_r(vx, y, it[i].value, vc);
        }
        if (it[i].value2) text(182, y, it[i].value2, on ? GOLD : WHITE);
        if (on) draw_cursor(x - 10, y);
    }

    const char *foot = "";
    if (page == P_START)       foot = T("ESC/F1 O BACK: PAUSA", "ESC/F1 OR BACK: PAUSE");
    else if (page == P_WORLDS) foot = T("SALTA AL MUNDO AL INSTANTE", "INSTANT WORLD WARP");
    else if (page == P_CHEATS) foot = T("ACTIVOS EN TIEMPO REAL", "ACTIVE IN REAL TIME");
    else if (page == P_CONTROLS) foot = binding >= 0 ? T("PULSA TECLA O BOTON...", "PRESS KEY OR BUTTON...")
                                                     : T("ENTER: TECLA  IZQ/DER: MANDO", "ENTER: KEY  L/R: PAD");
    if (*foot) text_c(196, foot, DIM);
}

static void draw_toast(void)
{
    if (!*toast || SDL_TICKS_PASSED(SDL_GetTicks(), toast_until)) { *toast = 0; return; }
    int tw = (int)strlen(toast) * 8 + 14;
    draw_box((W - tw) / 2, 4, tw, 16, 0xEA200C10u, GOLD);
    text_c(7, toast, GOLD);
}

/* ------------------------------------------------------------------ */
/* Input, Gamepads & Smart Android Multi-Touch Overlay                  */
/* ------------------------------------------------------------------ */

static SDL_GameController *pads[4];

#define MAX_FINGERS 10
typedef struct {
    SDL_FingerID id;
    float x, y;
    bool down;
} finger_t;

static finger_t fingers[MAX_FINGERS];
#ifdef __ANDROID__
static bool touch_seen = true;
#else
static bool touch_seen = false;
#endif
static bool pad_active_recently = false;
static Uint32 last_pad_tick = 0;

typedef struct {
    float cx, cy, hw, hh;
    int btn;
    const char *label;
    uint8_t r, g, b;
} tbtn_t;

enum { TACT_MENU = -1, TACT_SAVE = -2, TACT_LOAD = -3 };

static const tbtn_t touch_btns[] = {
    { 0.805f, 0.675f, 0.048f, 0.082f, RETRO_DEVICE_ID_JOYPAD_Y,      "KICK",  55, 200,  90 },
    { 0.865f, 0.785f, 0.048f, 0.082f, RETRO_DEVICE_ID_JOYPAD_B,      "JUMP", 240, 195,  45 },
    { 0.925f, 0.675f, 0.048f, 0.082f, RETRO_DEVICE_ID_JOYPAD_A,      "SPIN", 230,  65,  60 },
    { 0.865f, 0.565f, 0.048f, 0.082f, RETRO_DEVICE_ID_JOYPAD_X,      "DASH",  65, 150, 240 },
    { 0.085f, 0.365f, 0.062f, 0.052f, RETRO_DEVICE_ID_JOYPAD_L,      "L",    185, 195, 220 },
    { 0.915f, 0.365f, 0.062f, 0.052f, RETRO_DEVICE_ID_JOYPAD_R,      "R",    185, 195, 220 },
    { 0.145f, 0.925f, 0.058f, 0.042f, RETRO_DEVICE_ID_JOYPAD_SELECT, "SEL",  155, 165, 190 },
    { 0.855f, 0.925f, 0.058f, 0.042f, RETRO_DEVICE_ID_JOYPAD_START,  "STRT", 155, 165, 190 },
    { 0.080f, 0.068f, 0.065f, 0.048f, TACT_MENU,                     "MENU", 245, 205,  60 },
    { 0.840f, 0.068f, 0.052f, 0.044f, TACT_SAVE,                     "SAVE",  80, 210, 245 },
    { 0.942f, 0.068f, 0.052f, 0.044f, TACT_LOAD,                     "LOAD", 120, 230, 140 },
};
#define NTOUCH_BTNS (int)(sizeof touch_btns / sizeof touch_btns[0])
static const float DPAD_CX = 0.125f, DPAD_CY = 0.695f, DPAD_RX = 0.095f, DPAD_RY = 0.165f;

static bool touch_pad_visible(void)
{
    if (cfg.touch == 1) return true;
    if (cfg.touch == 2) return false;
    if (!touch_seen) return false;
    if (pad_active_recently && (SDL_GetTicks() - last_pad_tick) < 15000u) return false;
    return true;
}

static uint16_t read_touch_pad(void)
{
    if (!touch_pad_visible()) return 0;
    uint16_t b = 0;
    for (int f = 0; f < MAX_FINGERS; f++) {
        if (!fingers[f].down) continue;
        float fx = fingers[f].x, fy = fingers[f].y;
        float dx = (fx - DPAD_CX) / DPAD_RX, dy = (fy - DPAD_CY) / DPAD_RY;
        float d2 = dx * dx + dy * dy;
        if (d2 <= 1.45f && d2 >= 0.04f) {
            if (dx < -0.32f) b |= 1u << RETRO_DEVICE_ID_JOYPAD_LEFT;
            if (dx >  0.32f) b |= 1u << RETRO_DEVICE_ID_JOYPAD_RIGHT;
            if (dy < -0.32f) b |= 1u << RETRO_DEVICE_ID_JOYPAD_UP;
            if (dy >  0.32f) b |= 1u << RETRO_DEVICE_ID_JOYPAD_DOWN;
        }
        for (int i = 0; i < NTOUCH_BTNS; i++) {
            const tbtn_t *tb = &touch_btns[i];
            if (tb->btn < 0) continue;
            if (fx >= tb->cx - tb->hw && fx <= tb->cx + tb->hw &&
                fy >= tb->cy - tb->hh && fy <= tb->cy + tb->hh)
                b |= (uint16_t)(1u << tb->btn);
        }
    }
    return b;
}

static uint16_t read_pad(void)
{
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    uint16_t b = read_touch_pad();
    for (int i = 0; i < 12; i++)
        if (cfg.kb[i] > 0 && cfg.kb[i] < SDL_NUM_SCANCODES && k[cfg.kb[i]])
            b |= (uint16_t)(1u << snes_btn[i]);
    for (int p = 0; p < 4; p++) {
        SDL_GameController *c = pads[p];
        if (!c) continue;
        uint16_t pb = 0;
        for (int i = 0; i < 12; i++)
            if (cfg.gp[i] >= 0 && SDL_GameControllerGetButton(c, (SDL_GameControllerButton)cfg.gp[i]))
                pb |= (uint16_t)(1u << snes_btn[i]);
        Sint16 ax = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
        Sint16 ay = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
        if (ax < -16000) pb |= 1u << RETRO_DEVICE_ID_JOYPAD_LEFT;
        if (ax >  16000) pb |= 1u << RETRO_DEVICE_ID_JOYPAD_RIGHT;
        if (ay < -16000) pb |= 1u << RETRO_DEVICE_ID_JOYPAD_UP;
        if (ay >  16000) pb |= 1u << RETRO_DEVICE_ID_JOYPAD_DOWN;
        if (pb) { pad_active_recently = true; last_pad_tick = SDL_GetTicks(); b |= pb; }
    }
    return b;
}

enum { M_NONE, M_UP, M_DOWN, M_LEFT, M_RIGHT, M_OK, M_BACK, M_PAUSE, M_PGUP, M_PGDN };

static void menu_input(int m)
{
    item_t it[24];
    int n = build(it);
    if (n <= 0) return;
    if (m == M_UP) sel = (sel + n - 1) % n;
    else if (m == M_DOWN) sel = (sel + 1) % n;
    else if (m == M_PGUP) { sel -= 5; if (sel < 0) sel = 0; }
    else if (m == M_PGDN) { sel += 5; if (sel >= n) sel = n - 1; }
    else if (page == P_CONTROLS && (m == M_LEFT || m == M_RIGHT) && it[sel].id < 12) {
        binding = it[sel].id; bind_pad = true;
    } else if (m == M_OK || ((m == M_LEFT || m == M_RIGHT) && it[sel].value && page != P_WORLDS && page != P_CONTROLS)) {
        activate();
    } else if (m == M_BACK) back();
}

static void handle(int m)
{
    if (m == M_NONE) return;
    if (state == ST_PLAY) { if (m == M_PAUSE) pause_game(); return; }
    if (m == M_PAUSE) { if (page == P_START) return; back(); return; }
    menu_input(m);
}

static int key_action(SDL_Keycode k)
{
    switch (k) {
    case SDLK_UP: return M_UP;
    case SDLK_DOWN: return M_DOWN;
    case SDLK_LEFT: return M_LEFT;
    case SDLK_RIGHT: return M_RIGHT;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_z: case SDLK_x: case SDLK_SPACE: return M_OK;
    case SDLK_BACKSPACE: return M_BACK;
    case SDLK_ESCAPE: case SDLK_F1: case SDLK_AC_BACK: return M_PAUSE;
    }
    return M_NONE;
}

static int pad_action(int b)
{
    switch (b) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return M_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return M_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return M_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return M_RIGHT;
    case SDL_CONTROLLER_BUTTON_A: case SDL_CONTROLLER_BUTTON_START: return M_OK;
    case SDL_CONTROLLER_BUTTON_B: return M_BACK;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return M_PGUP;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return M_PGDN;
    case SDL_CONTROLLER_BUTTON_BACK: case SDL_CONTROLLER_BUTTON_GUIDE: return M_PAUSE;
    }
    return M_NONE;
}

static void poll_stick_menu(void)
{
    static int held_dir = M_NONE;
    static Uint32 next_rep = 0;
    if (state == ST_PLAY || binding >= 0) { held_dir = M_NONE; return; }
    int dir = M_NONE;
    for (int p = 0; p < 4; p++) {
        SDL_GameController *c = pads[p];
        if (!c) continue;
        Sint16 ax = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
        Sint16 ay = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
        if (ay < -18000) { dir = M_UP; break; }
        if (ay >  18000) { dir = M_DOWN; break; }
        if (ax < -18000) { dir = M_LEFT; break; }
        if (ax >  18000) { dir = M_RIGHT; break; }
    }
    Uint32 now = SDL_GetTicks();
    if (dir == M_NONE) { held_dir = M_NONE; return; }
    pad_active_recently = true; last_pad_tick = now;
    if (dir != held_dir) { held_dir = dir; next_rep = now + 260u; handle(dir); }
    else if (SDL_TICKS_PASSED(now, next_rep)) { next_rep = now + 110u; handle(dir); }
}

static SDL_Rect game_dst_rect(int rw, int rh)
{
    int dw = rw, dh = rh;
    if (rw * 3 > rh * 4) dw = rh * 4 / 3;
    else dh = rw * 3 / 4;
    SDL_Rect r = { (rw - dw) / 2, (rh - dh) / 2, dw, dh };
    return r;
}

static void handle_menu_tap(float nx, float ny)
{
    int rw = 0, rh = 0;
    SDL_GetRendererOutputSize(ren, &rw, &rh);
    if (rw <= 0 || rh <= 0) return;
    SDL_Rect dr = game_dst_rect(rw, rh);
    float gx = (nx * rw - dr.x) * (float)W / (float)dr.w;
    float gy = (ny * rh - dr.y) * (float)H / (float)dr.h;
    if (gx < 12 || gx > W - 12 || gy < 12 || gy > H - 12) {
        if (page != P_START) back();
        return;
    }
    if (menu_last_step <= 0) return;
    int idx = menu_last_first + (int)((gy - (float)(menu_last_y0 - 2)) / (float)menu_last_step);
    if (idx >= menu_last_first && idx < menu_last_first + menu_last_rows && idx < menu_last_n) {
        sel = idx;
        activate();
    }
}

static void draw_touch_overlay(int rw, int rh)
{
    if (state != ST_PLAY) return;
    bool show_pad = touch_pad_visible();
    bool show_pill = (cfg.touch != 2) && (touch_seen || cfg.touch == 1);
    if (!show_pad && !show_pill) return;

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    if (show_pad) {
        int cx = (int)(DPAD_CX * rw), cy = (int)(DPAD_CY * rh);
        int rx = (int)(DPAD_RX * rw), ry = (int)(DPAD_RY * rh);
        SDL_Rect hbar = { cx - rx, cy - ry / 3, rx * 2, ry * 2 / 3 };
        SDL_Rect vbar = { cx - rx / 3, cy - ry, rx * 2 / 3, ry * 2 };
        SDL_SetRenderDrawColor(ren, 20, 20, 28, 120);
        SDL_RenderFillRect(ren, &hbar);
        SDL_RenderFillRect(ren, &vbar);
        SDL_SetRenderDrawColor(ren, 255, 215, 80, 175);
        SDL_RenderDrawRect(ren, &hbar);
        SDL_RenderDrawRect(ren, &vbar);
    }
    for (int i = 0; i < NTOUCH_BTNS; i++) {
        const tbtn_t *tb = &touch_btns[i];
        if (tb->btn >= 0 && !show_pad) continue;
        if (tb->btn < 0 && !show_pill) continue;
        SDL_Rect rc = {
            (int)((tb->cx - tb->hw) * rw), (int)((tb->cy - tb->hh) * rh),
            (int)(tb->hw * 2 * rw),        (int)(tb->hh * 2 * rh)
        };
        bool pressed = (tb->btn >= 0) && ((pad >> tb->btn) & 1);
        SDL_SetRenderDrawColor(ren, tb->r, tb->g, tb->b, pressed ? 210 : 105);
        SDL_RenderFillRect(ren, &rc);
        SDL_SetRenderDrawColor(ren, 255, 255, 255, pressed ? 255 : 175);
        SDL_RenderDrawRect(ren, &rc);
    }
}

/* ------------------------------------------------------------------ */
/* ROM Link Screen (Before ROM is loaded)                               */
/* ------------------------------------------------------------------ */

static bool rom_link_screen(void)
{
    char err[128] = "";
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) return false;
            if (ev.type == SDL_DROPFILE) {
                char *dropped = ev.drop.file;
                if (load_rom_file(dropped, err, sizeof err)) { SDL_free(dropped); return true; }
                SDL_free(dropped);
            }
            if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_ESCAPE) return false;
                if (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_SPACE) {
                    char chosen[1024];
                    if (pick_rom_dialog(chosen, sizeof chosen) && load_rom_file(chosen, err, sizeof err))
                        return true;
                }
            }
            if (ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_FINGERDOWN) {
                char chosen[1024];
                if (pick_rom_dialog(chosen, sizeof chosen) && load_rom_file(chosen, err, sizeof err))
                    return true;
            }
        }
        SDL_SetRenderDrawColor(ren, 24, 12, 16, 255);
        SDL_RenderClear(ren);
        int rw = 0, rh = 0;
        SDL_GetRendererOutputSize(ren, &rw, &rh);
        SDL_Rect box = { rw / 8, rh / 4, rw * 3 / 4, rh / 2 };
        SDL_SetRenderDrawColor(ren, 55, 20, 20, 255);
        SDL_RenderFillRect(ren, &box);
        SDL_SetRenderDrawColor(ren, 255, 215, 64, 255);
        SDL_RenderDrawRect(ren, &box);
        SDL_RenderPresent(ren);
        SDL_SetWindowTitle(win, err[0] ? err : "Speedy Gonzales: Arriba Edition - Arrastra tu ROM (.sfc) o pulsa ENTER");
        SDL_Delay(16);
    }
    return false;
}

static bool try_find_initial_rom(int argc, char **argv)
{
    char err[128];
    if (argc > 1 && argv[1][0] != '-' && load_rom_file(argv[1], err, sizeof err)) return true;
    if (cfg.last_rom[0] && load_rom_file(cfg.last_rom, err, sizeof err)) return true;
    const char *names[] = {
        "Speedy Gonzales - Los Gatos Bandidos (USA).sfc",
        "Speedy Gonzales - Los Gatos Bandidos (USA).smc",
        "speedy.sfc", "speedy.smc", "rom.sfc", "rom.smc"
    };
    const char *dirs[] = { base_dir, save_dir, "." };
    for (int d = 0; d < 3; d++) {
        for (int i = 0; i < 6; i++) {
            char p[1024];
            snprintf(p, sizeof p, "%s%c%s", dirs[d], PATH_SEP, names[i]);
            if (load_rom_file(p, err, sizeof err)) return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Self-test Mode (--test <outdir>)                                     */
/* ------------------------------------------------------------------ */

static void save_bmp(const char *path, const uint32_t *argb)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceFrom((void *)argb, W, H, 32, W * 4,
                                              0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u);
    if (s) { SDL_SaveBMP(s, path); SDL_FreeSurface(s); }
}

static int run_self_test(const char *outdir)
{
    char p[1024];
    state = ST_MENU; page = P_START; sel = 0; draw_menu();
    snprintf(p, sizeof p, "%s%c01_start.bmp", outdir, PATH_SEP); save_bmp(p, ui);

    page = P_WORLDS; sel = 5; draw_menu();
    snprintf(p, sizeof p, "%s%c02_worlds.bmp", outdir, PATH_SEP); save_bmp(p, ui);

    page = P_CHEATS; sel = 0; cfg.c_god = 1; cfg.c_hearts = 1; cfg.c_lives = 1; cfg.c_shoes = 1; draw_menu();
    snprintf(p, sizeof p, "%s%c03_cheats.bmp", outdir, PATH_SEP); save_bmp(p, ui);

    // Launch World 1 (Sleepy Rock) with cheats active
    launch_world(0, true);
    step(30, 0);
    apply_cheats_frame();
    step(30, 0);
    snprintf(p, sizeof p, "%s%c04_play_1_1_cheats.bmp", outdir, PATH_SEP); save_bmp(p, game);

    // Warp directly to World 6 (Galactical Galaxies) and verify live gameplay
    launch_world(5, true);
    step(30, 0);
    snprintf(p, sizeof p, "%s%c05_warp_galactical.bmp", outdir, PATH_SEP); save_bmp(p, game);

    // Pause overlay on top of Galactical Galaxies
    state = ST_MENU; page = P_PAUSE; sel = 0; draw_menu();
    snprintf(p, sizeof p, "%s%c06_pause_overlay.bmp", outdir, PATH_SEP); save_bmp(p, ui);

    printf("Self-test completed cleanly into %s\n", outdir);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    bool test_mode = false;
    const char *test_dir = ".";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--test")) {
            test_mode = true;
            if (i + 1 < argc) test_dir = argv[++i];
        }
    }

    Uint32 init_flags = SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER;
    if (!test_mode) init_flags |= SDL_INIT_AUDIO;
    if (SDL_Init(init_flags) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    char *bp = SDL_GetBasePath();
    snprintf(base_dir, sizeof base_dir, "%s", bp ? bp : ".");
    if (bp) SDL_free(bp);
    size_t bl = strlen(base_dir);
    while (bl > 1 && (base_dir[bl - 1] == '/' || base_dir[bl - 1] == '\\')) base_dir[--bl] = 0;

#ifdef __ANDROID__
    const char *ip = SDL_AndroidGetInternalStoragePath();
    snprintf(save_dir, sizeof save_dir, "%s", ip ? ip : ".");
#else
    snprintf(save_dir, sizeof save_dir, "%s", base_dir);
#endif

    load_cfg();

    char core_path[1024];
#ifdef __ANDROID__
    snprintf(core_path, sizeof core_path, "%s", CORE_NAME);
#else
    snprintf(core_path, sizeof core_path, "%s%c%s", base_dir, PATH_SEP, CORE_NAME);
#endif
    if (!load_core(core_path) && !load_core(CORE_NAME)) {
        fprintf(stderr, "Failed to load %s\n", core_path);
        SDL_Quit();
        return 1;
    }

    if (!test_mode) {
        win = SDL_CreateWindow("Speedy Gonzales: Arriba Edition",
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               W * 3, H * 3, SDL_WINDOW_RESIZABLE);
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        apply_video_settings();
        tex_ui = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
    }

    if (!try_find_initial_rom(argc, argv)) {
        if (test_mode) { fprintf(stderr, "ROM not found in test mode\n"); return 2; }
        char chosen[1024], err[128];
        if (!pick_rom_dialog(chosen, sizeof chosen) || !load_rom_file(chosen, err, sizeof err)) {
            if (!rom_link_screen()) { core.deinit(); SDL_Quit(); return 0; }
        }
    }
    save_cfg();

    struct retro_game_info gi = { rom_path, rom_buf, rom_len, NULL };
    if (!core.load_game(&gi)) {
        fprintf(stderr, "core.load_game failed\n");
        return 3;
    }

    if (!test_mode) {
        struct retro_system_av_info av;
        core.get_av(&av);
        SDL_AudioSpec want, have;
        memset(&want, 0, sizeof want);
        want.freq = (int)(av.timing.sample_rate > 0 ? av.timing.sample_rate : 32040);
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 1024;
        audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (audio) SDL_PauseAudioDevice(audio, 0);
    }

    boot_and_cache_snapshots();

    if (test_mode) {
        int rc = run_self_test(test_dir);
        core.deinit();
        SDL_Quit();
        return rc;
    }

    for (int i = 0; i < SDL_NumJoysticks() && i < 4; i++)
        if (SDL_IsGameController(i)) pads[i] = SDL_GameControllerOpen(i);

    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 next_frame = SDL_GetPerformanceCounter();

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_CONTROLLERDEVICEADDED:
                for (int i = 0; i < 4; i++) if (!pads[i]) { pads[i] = SDL_GameControllerOpen(ev.cdevice.which); break; }
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                for (int i = 0; i < 4; i++) {
                    if (pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == ev.cdevice.which) {
                        SDL_GameControllerClose(pads[i]); pads[i] = NULL;
                    }
                }
                break;
            case SDL_KEYDOWN:
                if (ev.key.repeat && state == ST_PLAY) break;
                if (binding >= 0 && !bind_pad) {
                    if (ev.key.keysym.sym != SDLK_ESCAPE) {
                        cfg.kb[binding] = ev.key.keysym.scancode;
                        save_cfg();
                    }
                    binding = -1;
                    break;
                }
                if (ev.key.keysym.sym == SDLK_F5 && started) {
                    show_toast(save_state(cfg.slot) ? T("ESTADO GUARDADO", "STATE SAVED") : T("ERROR AL GUARDAR", "SAVE FAILED"));
                } else if (ev.key.keysym.sym == SDLK_F7) {
                    if (load_state(cfg.slot)) { state = ST_PLAY; show_toast(T("ESTADO CARGADO", "STATE LOADED")); }
                    else show_toast(T("SLOT VACIO", "EMPTY SLOT"));
                } else if (ev.key.keysym.sym == SDLK_F6) {
                    cfg.slot = (cfg.slot + 1) % 3; save_cfg();
                    char m[32]; snprintf(m, sizeof m, "SLOT %d", cfg.slot + 1); show_toast(m);
                } else if (ev.key.keysym.sym == SDLK_F11) {
                    cfg.fullscreen = !cfg.fullscreen; apply_video_settings(); save_cfg();
                } else {
                    handle(key_action(ev.key.keysym.sym));
                }
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                pad_active_recently = true; last_pad_tick = SDL_GetTicks();
                if (binding >= 0 && bind_pad) {
                    cfg.gp[binding] = ev.cbutton.button;
                    save_cfg(); binding = -1;
                    break;
                }
                handle(pad_action(ev.cbutton.button));
                break;
            case SDL_FINGERDOWN: {
                touch_seen = true;
                float fx = ev.tfinger.x, fy = ev.tfinger.y;
                if (state == ST_MENU) { handle_menu_tap(fx, fy); break; }
                bool special = false;
                for (int i = 0; i < NTOUCH_BTNS; i++) {
                    const tbtn_t *tb = &touch_btns[i];
                    if (tb->btn >= 0) continue;
                    if (fx >= tb->cx - tb->hw && fx <= tb->cx + tb->hw &&
                        fy >= tb->cy - tb->hh && fy <= tb->cy + tb->hh) {
                        if (tb->btn == TACT_MENU) pause_game();
                        else if (tb->btn == TACT_SAVE) show_toast(save_state(cfg.slot) ? T("ESTADO GUARDADO", "STATE SAVED") : T("ERROR", "ERROR"));
                        else if (tb->btn == TACT_LOAD) {
                            if (load_state(cfg.slot)) show_toast(T("ESTADO CARGADO", "STATE LOADED"));
                            else show_toast(T("SLOT VACIO", "EMPTY SLOT"));
                        }
                        special = true; break;
                    }
                }
                if (!special) {
                    for (int f = 0; f < MAX_FINGERS; f++)
                        if (!fingers[f].down) { fingers[f] = (finger_t){ ev.tfinger.fingerId, fx, fy, true }; break; }
                }
                break;
            }
            case SDL_FINGERMOTION:
                for (int f = 0; f < MAX_FINGERS; f++)
                    if (fingers[f].down && fingers[f].id == ev.tfinger.fingerId) {
                        fingers[f].x = ev.tfinger.x; fingers[f].y = ev.tfinger.y; break;
                    }
                break;
            case SDL_FINGERUP:
                for (int f = 0; f < MAX_FINGERS; f++)
                    if (fingers[f].down && fingers[f].id == ev.tfinger.fingerId) { fingers[f].down = false; break; }
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (state == ST_MENU && ev.button.button == SDL_BUTTON_LEFT) {
                    int rw = 0, rh = 0; SDL_GetRendererOutputSize(ren, &rw, &rh);
                    if (rw > 0 && rh > 0) handle_menu_tap((float)ev.button.x / rw, (float)ev.button.y / rh);
                }
                break;
            }
        }

        poll_stick_menu();

        if (state == ST_PLAY) {
            pad = read_pad();
            // L + R + START chord on any controller opens pause menu cleanly
            if ((pad & (1u << RETRO_DEVICE_ID_JOYPAD_L)) &&
                (pad & (1u << RETRO_DEVICE_ID_JOYPAD_R)) &&
                (pad & (1u << RETRO_DEVICE_ID_JOYPAD_START))) {
                pause_game();
            } else {
                apply_cheats_frame();
                core.run();
                total_frames++;
            }
        }

        // Render
        int rw = 0, rh = 0;
        SDL_GetRendererOutputSize(ren, &rw, &rh);
        SDL_Rect dst = game_dst_rect(rw, rh);
        SDL_SetRenderDrawColor(ren, 8, 6, 10, 255);
        SDL_RenderClear(ren);

        if (state == ST_PLAY) {
            SDL_UpdateTexture(tex_game, NULL, game, gw * 4);
            SDL_RenderCopy(ren, tex_game, NULL, &dst);
            if (*toast) {
                memset(ui, 0, sizeof ui);
                draw_toast();
                SDL_SetTextureBlendMode(tex_ui, SDL_BLENDMODE_BLEND);
                SDL_UpdateTexture(tex_ui, NULL, ui, W * 4);
                SDL_RenderCopy(ren, tex_ui, NULL, &dst);
            }
            draw_touch_overlay(rw, rh);
        } else {
            draw_menu();
            draw_toast();
            SDL_SetTextureBlendMode(tex_ui, SDL_BLENDMODE_NONE);
            SDL_UpdateTexture(tex_ui, NULL, ui, W * 4);
            SDL_RenderCopy(ren, tex_ui, NULL, &dst);
        }
        SDL_RenderPresent(ren);

        // 60.0988 Hz SNES pacing
        next_frame += freq / 60;
        Uint64 now = SDL_GetPerformanceCounter();
        if (next_frame > now) {
            Uint32 ms = (Uint32)((next_frame - now) * 1000 / freq);
            if (ms > 0) SDL_Delay(ms);
        } else {
            next_frame = now;
        }
    }

    for (int i = 0; i < 4; i++) if (pads[i]) SDL_GameControllerClose(pads[i]);
    if (audio) SDL_CloseAudioDevice(audio);
    core.deinit();
    free(rom_buf); free(title_snap); free(map_snap);
    SDL_Quit();
    return 0;
}
