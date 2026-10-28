# Project Save Clank

An optional native OpenRAC mod that saves and restores warm and cross-launch snapshots
of Ratchet & Clank 1 PAL and NTSC. The mod owns controls, slot files and save/load
policy; OpenRAC supplies the safe tick boundary, snapshot calls and the top-level
mod-provided desktop menu.

## Controls

- **Shift + Numpad 0–9** saves to that slot.
- **Ctrl + Numpad 0–9** loads that slot.
- The in-window **Savestates → Save/Load** menus provide the same actions. Each slot starts as
  **Empty** and shows **[Saved] <planet>** after a successful save, so occupied slots are easy to
  spot; its shortcut identifies the slot.

Each slot is stored as `project-save-clank-rac1-<disc>-slot-<n>.state` in
OpenRAC's per-user data folder, under `mods/project-save-clank/`. On Linux this
is `$XDG_DATA_HOME/openrac/mods/project-save-clank/`, or
`~/.local/share/openrac/mods/project-save-clank/` when `XDG_DATA_HOME` is unset.
Windows uses `%LOCALAPPDATA%/openrac/mods/project-save-clank/`; macOS uses
`~/Library/Application Support/OpenRAC/mods/project-save-clank/`. This keeps
slots independent of the launcher's working directory and preserves them across
game and launcher restarts. Slots work only with the same OpenRAC build and
disc. Save and load actions are accepted only after a planet has finished
loading; requests during startup or travel are ignored. Loading invalidates
renderer state and stops current audio voices/streams; audio resumes from the
game's subsequent requests.

## Build

This project uses the OpenRAC mod SDK. By default CMake prefers a neighboring
`OpenRAC-upstream` checkout, then falls back to a neighboring `OpenRAC` checkout:

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

If OpenRAC is elsewhere, point CMake at its SDK package directory:

```sh
cmake -S . -B build -G Ninja \
  -DOpenRACMod_DIR=/path/to/OpenRAC/sdk/cmake
cmake --build build
```

The ready-to-install package is written to
`build/package/project_save_clank/`. Install that folder in the launcher's Mods
tab (or package it with the SDK's `tools/package_mod.py`). The Developer submenu
appears only while a mod that registered one is loaded.
