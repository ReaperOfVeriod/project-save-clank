# Project Save Clank

An optional native OpenRAC mod that saves and restores a warm, in-session
snapshot of Ratchet & Clank 1 for PAL and NTSC. The mod owns the controls,
buffer and state-file I/O; OpenRAC supplies the tick-boundary and snapshot APIs.

## Controls

- **Numpad 1** saves to `project-save-clank-rac1-<disc>.state` in the game's
  working directory.
- **Numpad 2** restores that snapshot during the same game process.

The mod removes its state file at startup. Cold restore, cross-build state files
and portable state files are not supported. Loading invalidates renderer state
and stops current audio voices/streams; audio resumes from the game's subsequent
requests.

## Build

This project uses the OpenRAC mod SDK. By default CMake looks for the SDK in a
neighboring `OpenRAC` checkout:

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
tab (or package it with the SDK's `tools/package_mod.py`).


