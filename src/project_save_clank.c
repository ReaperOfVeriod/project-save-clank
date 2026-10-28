/* SPDX-License-Identifier: MIT
 * Warm, same-process savestates for RAC1 PAL and NTSC.
 * Snapshot primitives are supplied by OpenRAC; policy, controls and file I/O live here.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openrac/keys.h"
#include "openrac/openrac_mod.h"

OPENRAC_MOD_DEFINE("project-save-clank", "0.1.0")

static const openrac_api* s_api;
static openrac_mod* s_mod;
static char s_path[112];
static unsigned char* s_state;
static size_t s_state_size;

static void report(const char* action, int ok) {
    char line[192];
    snprintf(line, sizeof line, "%s %s", action, ok ? s_path : "failed; see log");
    s_api->log(s_mod, ok ? OPENRAC_LOG_INFO : OPENRAC_LOG_WARN, line);
}

static int ensure_buffer(void) {
    if (s_state != NULL) return 1;
    s_state_size = s_api->savestate_size();
    if (s_state_size == 0 || (s_state = (unsigned char*)malloc(s_state_size)) == NULL) {
        s_api->log(s_mod, OPENRAC_LOG_ERROR, "cannot allocate this build's snapshot buffer");
        return 0;
    }
    return 1;
}

static int write_state(void) {
    FILE* file;
    int ok;
    if (!ensure_buffer() || s_api->savestate_save(s_state, s_state_size) != 0) return 0;
    file = fopen(s_path, "wb");
    if (file == NULL) return 0;
    ok = fwrite(s_state, 1, s_state_size, file) == s_state_size;
    if (fclose(file) != 0) ok = 0;
    return ok;
}

static int read_state(void) {
    FILE* file;
    int ok;
    int extra;
    if (!ensure_buffer() || (file = fopen(s_path, "rb")) == NULL) return 0;
    ok = fread(s_state, 1, s_state_size, file) == s_state_size;
    extra = fgetc(file);
    if (extra != EOF || ferror(file)) ok = 0;
    fclose(file);
    return ok;
}

static void tick_boundary(void* user, uint32_t tick) {
    const openrac_input* input = s_api->input();
    (void)user;
    (void)tick;
    if (input->overlay) return;
    /* HID usages: Numpad 1 = 89, Numpad 2 = 90. */
    if (openrac_bind_pressed(input, 89)) {
        report("save:", write_state());
    } else if (openrac_bind_pressed(input, 90)) {
        if (!read_state()) report("load:", 0);
        else report("load:", s_api->savestate_load(s_state, s_state_size) == 0);
    }
}

OPENRAC_MOD_EXPORT int openrac_mod_load(const openrac_api* api, openrac_mod* mod) {
    if (api == NULL || mod == NULL || api->api_minor < 5 || strcmp(api->game, "rac1") != 0
        || (strcmp(api->version, "pal") != 0 && strcmp(api->version, "ntsc") != 0)) {
        if (api != NULL && mod != NULL)
            api->fail(mod, "warm savestates require RAC1 PAL or NTSC and interface 1.5");
        return 1;
    }
    s_api = api;
    s_mod = mod;
    snprintf(s_path, sizeof s_path, "project-save-clank-rac1-%s.state", api->version);
    /* A state from another process is deliberately not a supported cold restore. */
    remove(s_path);
    if (api->on_tick_boundary(mod, tick_boundary, NULL) != OPENRAC_HOOK_OK) {
        api->fail(mod, "the port refused the tick-boundary callback");
        return 1;
    }
    api->log(mod, OPENRAC_LOG_INFO,
             "Numpad 1 saves; Numpad 2 loads (warm, current session only)");
    return 0;
}

OPENRAC_MOD_EXPORT void openrac_mod_unload(void) {
    free(s_state);
    s_state = NULL;
}
