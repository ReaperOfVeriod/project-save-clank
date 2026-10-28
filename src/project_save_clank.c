/* SPDX-License-Identifier: MIT
 * Warm and cross-launch savestates for RAC1 PAL and NTSC.
 * Snapshot primitives are supplied by OpenRAC; policy, controls and file I/O
 * live here.
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <direct.h>
#include <windows.h>
#define mkdir(path, mode) _mkdir(path)
#define stat _stat
#ifndef S_IFDIR
#define S_IFDIR _S_IFDIR
#endif
#endif

#include "openrac/keys.h"
#include "openrac/openrac_mod.h"

OPENRAC_MOD_DEFINE("project-save-clank", "0.3.1")

#define SLOT_COUNT 10
#define MAX_STATE_SIZE (64u * 1024u * 1024u)
#define PATH_CAPACITY 1024
#define ACTION_SAVE(slot) ((uint32_t)(1 + (slot)))
#define ACTION_LOAD(slot) ((uint32_t)(11 + (slot)))

static const openrac_api* s_api;
static openrac_mod* s_mod;
static char s_paths[SLOT_COUNT][PATH_CAPACITY];
static unsigned char* s_state;
static size_t s_state_size;
static char s_planets[SLOT_COUNT][64];

typedef struct state_file_header {
    char magic[8];
    uint32_t version;
    uint32_t header_size;
    uint64_t state_size;
    uint64_t checksum;
    char planet[64];
} state_file_header;

static const char s_file_magic[8] = {'P', 'S', 'C', 'S', 'T', 'A', 'T', 'E'};

static int directory_exists(const char* path) {
    struct stat info;
#if defined(_WIN32)
    return stat(path, &info) == 0 && (info.st_mode & S_IFDIR) != 0;
#else
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

static int make_directory(const char* path) {
    if (mkdir(path, 0700) == 0 || errno == EEXIST)
        return directory_exists(path);
    return 0;
}

static int replace_file(const char* temporary, const char* destination) {
#if defined(_WIN32)
    return MoveFileExA(temporary, destination, MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return rename(temporary, destination) == 0;
#endif
}

static int make_directories(char* path) {
    char* p;
    size_t length = strlen(path);
    if (length == 0 || length >= PATH_CAPACITY)
        return 0;
    for (p = path + 1; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
#if defined(_WIN32)
            if (p == path + 2 && path[1] == ':')
                continue;
#endif
            *p = '\0';
            if (!make_directory(path)) {
                *p = '/';
                return 0;
            }
            *p = '/';
        }
    }
    return make_directory(path);
}

static int initialize_paths(const openrac_api* api) {
    const char* base;
    char directory[PATH_CAPACITY];
    unsigned i;
#if defined(__APPLE__)
    base = getenv("HOME");
    if (base == NULL || *base == '\0')
        base = ".";
    if (snprintf(
            directory, sizeof directory,
            "%s/Library/Application Support/OpenRAC/mods/project-save-clank",
            base) >= (int)sizeof directory)
        return 0;
#elif defined(_WIN32)
    base = getenv("LOCALAPPDATA");
    if (base == NULL || *base == '\0') {
        base = getenv("USERPROFILE");
        if (base == NULL || *base == '\0')
            base = ".";
        if (snprintf(directory, sizeof directory,
                     "%s/AppData/Local/openrac/mods/project-save-clank",
                     base) >= (int)sizeof directory)
            return 0;
    } else if (snprintf(directory, sizeof directory,
                        "%s/openrac/mods/project-save-clank",
                        base) >= (int)sizeof directory) {
        return 0;
    }
#else
    base = getenv("XDG_DATA_HOME");
    if (base != NULL && *base != '\0') {
        if (snprintf(directory, sizeof directory,
                     "%s/openrac/mods/project-save-clank",
                     base) >= (int)sizeof directory)
            return 0;
    } else {
        base = getenv("HOME");
        if (base == NULL || *base == '\0')
            base = ".";
        if (snprintf(directory, sizeof directory,
                     "%s/.local/share/openrac/mods/project-save-clank",
                     base) >= (int)sizeof directory)
            return 0;
    }
#endif
    if (!make_directories(directory)) {
        char message[PATH_CAPACITY + 64];
        snprintf(message, sizeof message,
                 "cannot create persistent savestate directory: %s", directory);
        api->log(s_mod, OPENRAC_LOG_ERROR, message);
        return 0;
    }
    for (i = 0; i < SLOT_COUNT; ++i) {
        int length = snprintf(s_paths[i], sizeof s_paths[i],
                              "%s/project-save-clank-rac1-%s-slot-%u.state",
                              directory, api->version, i);
        if (length < 0 || (size_t)length >= sizeof s_paths[i])
            return 0;
    }
    return 1;
}

static uint64_t checksum(const unsigned char* data, size_t size) {
    uint64_t value = UINT64_C(14695981039346656037);
    size_t i;
    for (i = 0; i < size; ++i) {
        value = (value ^ data[i]) * UINT64_C(1099511628211);
    }
    return value;
}

static const char* current_planet(void) {
    openrac_play_status status = {0};
    const char* name;
    status.size = sizeof status;
    if (s_api->play_status(&status) != OPENRAC_HOOK_OK || status.level < 0)
        return "Unknown";
    name = s_api->level_name(status.level);
    return name != NULL && *name != '\0' ? name : "Unknown";
}

static void set_slot_label(unsigned slot, const char* planet) {
    char label[64];
    snprintf(label, sizeof label, "[Saved] %s", planet);
    s_api->developer_menu_set_label(s_mod, ACTION_SAVE(slot), label);
    snprintf(label, sizeof label, "[Saved] %s", planet);
    s_api->developer_menu_set_label(s_mod, ACTION_LOAD(slot), label);
}

static int ensure_buffer(size_t wanted) {
    unsigned char* replacement;
    if (wanted == 0 || wanted > MAX_STATE_SIZE)
        return 0;
    if (s_state != NULL && s_state_size == wanted)
        return 1;
    replacement = (unsigned char*)malloc(wanted);
    if (replacement == NULL) {
        s_api->log(s_mod, OPENRAC_LOG_ERROR,
                   "cannot allocate this build's snapshot buffer");
        return 0;
    }
    free(s_state);
    s_state = replacement;
    s_state_size = wanted;
    return 1;
}

static void report(unsigned slot, const char* verb, int ok) {
    char line[192];
    if (ok) {
        snprintf(line, sizeof line, "%.8s slot %u: ok (%.140s)", verb, slot,
                 s_paths[slot]);
    } else {
        snprintf(line, sizeof line, "%.8s slot %u: failed", verb, slot);
    }
    s_api->log(s_mod, ok ? OPENRAC_LOG_INFO : OPENRAC_LOG_WARN, line);
}

static int gameplay_ready(void) {
    openrac_play_status status = {0};
    status.size = sizeof status;
    if (s_api->play_status(&status) != OPENRAC_HOOK_OK || status.level < 0)
        return 0;
    return status.phase == OPENRAC_PLAY_PLAYING ||
           status.phase == OPENRAC_PLAY_SCENE ||
           status.phase == OPENRAC_PLAY_MENU;
}

static void report_not_ready(void) {
    s_api->log(
        s_mod, OPENRAC_LOG_WARN,
        "savestate action ignored: wait until a planet has finished loading");
}

static int write_state(unsigned slot) {
    FILE* file;
    state_file_header header = {{0}, 1, sizeof(state_file_header), 0, 0, {0}};
    char temporary[PATH_CAPACITY + 8];
    int ok;
    if (!ensure_buffer(s_api->savestate_size()) ||
        s_api->savestate_save(s_state, s_state_size) != 0)
        return 0;
    memcpy(header.magic, s_file_magic, sizeof(s_file_magic));
    header.state_size = s_state_size;
    snprintf(header.planet, sizeof header.planet, "%s", current_planet());
    header.checksum = checksum(s_state, s_state_size);
    snprintf(temporary, sizeof temporary, "%s.tmp", s_paths[slot]);
    remove(temporary);
    file = fopen(temporary, "wb");
    if (file == NULL)
        return 0;
    ok = fwrite(&header, 1, sizeof header, file) == sizeof header &&
         fwrite(s_state, 1, s_state_size, file) == s_state_size;
    if (fclose(file) != 0)
        ok = 0;
    if (ok && !replace_file(temporary, s_paths[slot]))
        ok = 0;
    if (!ok)
        remove(temporary);
    else
        snprintf(s_planets[slot], sizeof s_planets[slot], "%s", header.planet);
    return ok;
}

static int read_state(unsigned slot) {
    FILE* file;
    state_file_header header;
    int ok;
    if ((file = fopen(s_paths[slot], "rb")) == NULL)
        return 0;
    ok = fread(&header, 1, sizeof header, file) == sizeof header &&
         memcmp(header.magic, s_file_magic, sizeof(s_file_magic)) == 0 &&
         header.version == 1 && header.header_size == sizeof header &&
         header.planet[sizeof header.planet - 1] == '\0' &&
         header.state_size > 0 && header.state_size <= MAX_STATE_SIZE;
    if (ok)
        ok = ensure_buffer((size_t)header.state_size);
    if (ok)
        ok = fread(s_state, 1, s_state_size, file) == s_state_size &&
             checksum(s_state, s_state_size) == header.checksum;
    if (ok) {
        const int extra = fgetc(file);
        if (extra != EOF || ferror(file))
            ok = 0;
    }
    fclose(file);
    if (ok)
        snprintf(s_planets[slot], sizeof s_planets[slot], "%s", header.planet);
    return ok;
}

static void read_slot_label(unsigned slot) {
    FILE* file;
    state_file_header header;
    if ((file = fopen(s_paths[slot], "rb")) == NULL)
        return;
    if (fread(&header, 1, sizeof header, file) == sizeof header &&
        memcmp(header.magic, s_file_magic, sizeof(s_file_magic)) == 0 &&
        header.version == 1 && header.header_size == sizeof header &&
        header.planet[sizeof header.planet - 1] == '\0' &&
        header.state_size > 0 && header.state_size <= MAX_STATE_SIZE) {
        if (fseek(file, 0, SEEK_END) == 0) {
            long end = ftell(file);
            if (end >= 0 &&
                (uint64_t)end == sizeof header + header.state_size) {
                snprintf(s_planets[slot], sizeof s_planets[slot], "%s",
                         header.planet);
            }
        }
    }
    fclose(file);
}

static void perform(uint32_t action) {
    unsigned slot;
    int ok;
    if (action >= 1 && action <= SLOT_COUNT) {
        slot = (unsigned)(action - 1);
        if (!gameplay_ready()) {
            report_not_ready();
            return;
        }
        ok = write_state(slot);
        if (ok)
            set_slot_label(slot, current_planet());
        report(slot, "save", ok);
    } else if (action > SLOT_COUNT && action <= SLOT_COUNT * 2) {
        slot = (unsigned)(action - SLOT_COUNT - 1);
        if (!gameplay_ready()) {
            report_not_ready();
            return;
        }
        ok = read_state(slot);
        if (ok)
            ok = s_api->savestate_load(s_state, s_state_size) == 0;
        report(slot, "load", ok);
    }
}

static void tick_boundary(void* user, uint32_t tick) {
    static const int numpad[SLOT_COUNT] = {98, 89, 90, 91, 92,
                                           93, 94, 95, 96, 97};
    const openrac_input* input = s_api->input();
    uint32_t action;
    unsigned slot;
    (void)user;
    (void)tick;

    action = s_api->take_developer_menu_action(s_mod);
    if (action != 0) {
        perform(action);
        return;
    }
    if (input->overlay || !input->focused)
        return;
    for (slot = 0; slot < SLOT_COUNT; ++slot) {
        if (!openrac_bind_pressed(input, numpad[slot]))
            continue;
        if (openrac_bind_held(input, 225) || openrac_bind_held(input, 229)) {
            if (!openrac_bind_held(input, 224) &&
                !openrac_bind_held(input, 228))
                perform(ACTION_SAVE(slot));
        } else if (openrac_bind_held(input, 224) ||
                   openrac_bind_held(input, 228)) {
            perform(ACTION_LOAD(slot));
        }
        return;
    }
}

OPENRAC_MOD_EXPORT int openrac_mod_load(const openrac_api* api,
                                        openrac_mod* mod) {
    static const char* shortcuts[SLOT_COUNT * 2] = {
        "Shift+Num0", "Shift+Num1", "Shift+Num2", "Shift+Num3", "Shift+Num4",
        "Shift+Num5", "Shift+Num6", "Shift+Num7", "Shift+Num8", "Shift+Num9",
        "Ctrl+Num0",  "Ctrl+Num1",  "Ctrl+Num2",  "Ctrl+Num3",  "Ctrl+Num4",
        "Ctrl+Num5",  "Ctrl+Num6",  "Ctrl+Num7",  "Ctrl+Num8",  "Ctrl+Num9"};
    openrac_developer_menu_item items[SLOT_COUNT * 2];
    char labels[SLOT_COUNT * 2][64];
    unsigned i;
    if (api == NULL || mod == NULL || api->api_minor < 10 ||
        strcmp(api->game, "rac1") != 0 ||
        (strcmp(api->version, "pal") != 0 &&
         strcmp(api->version, "ntsc") != 0)) {
        if (api != NULL && mod != NULL)
            api->fail(mod,
                      "savestates require RAC1 PAL or NTSC and interface 1.10");
        return 1;
    }
    s_api = api;
    s_mod = mod;
    if (!initialize_paths(api)) {
        api->fail(mod, "cannot initialize persistent savestate storage");
        return 1;
    }
    for (i = 0; i < SLOT_COUNT; ++i) {
        snprintf(labels[i], sizeof labels[i], "Empty");
        snprintf(labels[SLOT_COUNT + i], sizeof labels[SLOT_COUNT + i],
                 "Empty");
        read_slot_label(i);
        if (s_planets[i][0] != '\0') {
            snprintf(labels[i], sizeof labels[i], "[Saved] %.52s",
                     s_planets[i]);
            snprintf(labels[SLOT_COUNT + i], sizeof labels[SLOT_COUNT + i],
                     "[Saved] %.52s", s_planets[i]);
        }
        items[i] = (openrac_developer_menu_item){labels[i], shortcuts[i],
                                                 ACTION_SAVE(i), "Save"};
        items[SLOT_COUNT + i] = (openrac_developer_menu_item){
            labels[SLOT_COUNT + i], shortcuts[SLOT_COUNT + i], ACTION_LOAD(i),
            "Load"};
    }
    if (api->developer_menu(mod, "Savestates", items, SLOT_COUNT * 2) !=
        OPENRAC_HOOK_OK) {
        api->fail(mod, "the port refused the Savestates menu");
        return 1;
    }
    if (api->on_tick_boundary(mod, tick_boundary, NULL) != OPENRAC_HOOK_OK) {
        api->fail(mod, "the port refused the tick-boundary callback");
        return 1;
    }
    api->log(mod, OPENRAC_LOG_INFO,
             "Savestates: Shift+Numpad 0-9 saves; Ctrl+Numpad 0-9 loads (same "
             "build and disc; slots persist across launches)");
    return 0;
}

OPENRAC_MOD_EXPORT void openrac_mod_unload(void) {
    free(s_state);
    s_state = NULL;
}
