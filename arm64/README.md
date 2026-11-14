# Celeste on aarch64 Linux

This fork of [Celeste-FMOD2](https://github.com/pixelomer/Celeste-FMOD2) collects the extra
changes needed to run Celeste + Everest natively on aarch64 Linux, without emulation or compatibility layers. Everything here was worked out on one machine, so read "Tested on" first.

## Tested on

| | |
|---|---|
| Hardware / OS | Apple M1, NixOS (nixos-apple-silicon), Wayland |
| GPU driver | Mesa 26.2.5 (Honeykrisp / Asahi GL), OpenGL 4.6 |
| Game | Celeste 1.4.0.0 (Steam copy), Everest 6642, .NET 8.0.31 |
| FMOD | 2.02.22 |

Other distros and Celeste versions are untested. Steam copies of the game work here, but
the upstream README warns that Steam builds can be a problem because of DRM; the Steam
stand-in below is what gets around it.

## What this fork adds on top of upstream

| Change | Where | Why |
|---|---|---|
| **Music fix** (`reaper`) | `fmod/reaper.c`, `fmod/make-fmodstudio-shim.sh`, `fmod/Makefile` | Under FMOD 2 a fade-out stop never finishes for some events (B-side remixes, cassette tracks, credits), so they keep playing and stack up. The shim now fades those out itself and stops them. |
| **Steamworks.NET.dll** | `arm64/build-steamworks-net.sh` | The DLL shipped with the game is stamped x86-64-only and fails to load on aarch64. It is rebuilt from source as AnyCPU (`dotnet build ... -p:PlatformTarget=AnyCPU`). |
| **Steam stand-in** | `arm64/make-steam-stub.sh` | Everest's Steamworks.NET calls `libsteam_api.so`, which does not exist for this setup. The stub is generated from the names the DLL imports. |
| **`Celeste` executable** | `arm64/fix-launcher.sh` | Everest relaunches the game by running `Celeste` in the game folder, and its download ships an x86-64 one ("Exec format error", right after an Everest update). Replaced by a script that runs `dotnet Celeste.dll`. |
| **Splash helper** | `arm64/fix-splash.sh` | Everest's loading splash is an x86-64 binary ("Exec format error"). Replaced by a no-op. |
| **Lua / SDL2 on NixOS** | `arm64/install.sh` | `liblua54.so` is only installed through apt/dnf by upstream; the from-source SDL2 could not load Vulkan/X11/Wayland on NixOS, so nixpkgs' SDL2 is used. |
| **Everest updater** | `arm64/update-everest.sh` | Everest's in-game updater runs `MiniInstaller-linux`, an x86-64 binary. The script downloads a build and runs `MiniInstaller.dll` through dotnet instead, then re-applies the fixes above. |
| **Launcher** | `arm64/play.sh` | Sets the library path and uses OpenGL by default. |
| **Nix environment** | `arm64/shell.nix` | Everything the build and run steps need (not tested as a whole). |
| **Diagnostics** | `arm64/tools/fmodtest.c` | Standalone program that checks which events fail to stop (see below). |

## Install

1. Get **FMOD 2.02.22 for Linux** from fmod.com (free account) and unpack it where upstream's
   `patch.sh` expects it (`otherlibs/`). Do not use upstream's `download-fmod.sh`: it signs up
   for an account with a throwaway email address, which is against FMOD's terms.
2. Build and patch as upstream describes (`./patch.sh /path/to/Celeste.exe`). The shim built
   by `make -C fmod sound` now includes the music fix.
3. Start the game once so Everest installs itself, then run:

   ```bash
   bash arm64/install.sh /path/to/game/dir
   /path/to/game/dir/play.sh
   ```

   `install.sh` is safe to run again; it skips what is already in place. (Run scripts with
   `bash`: file permissions are not preserved when the files are copied around.)

`install.sh` needs `git` and the .NET 8 SDK (to rebuild `Steamworks.NET.dll`, with network access to
GitHub and NuGet), and `monodis` (package `mono`) for the Steam stub. The original DLL is kept as
`Steamworks.NET.dll.x64`.

## Updating Everest

```bash
bash arm64/update-everest.sh -l                      # newest build of stable / beta / dev
bash arm64/update-everest.sh /path/to/game/dir       # update to the newest stable build
bash arm64/update-everest.sh -b beta /path/to/game/dir
bash arm64/update-everest.sh -v <build id> /path/to/game/dir
```

If you used the in-game updater and it failed at the installer step, the new files are already
in the game folder. Finish the install with:

```bash
bash arm64/update-everest.sh --finish /path/to/game/dir
```

An update copies Everest's files over the game folder (which brings back the x86-64 splash
helper and its native libraries), runs `MiniInstaller.dll`, and then runs `install.sh` again to
re-apply this fork's fixes. If the installer fails, running it again is safe; Everest keeps the
original game files in an `orig/` folder. `--no-post` skips the re-apply step.

The builds come from Everest's Azure DevOps pipeline, the same place the other Linux installers
read from. If Everest's download location changes, `-l` will fail first.

## Settings (environment variables)

| Variable | Default | Meaning |
|---|---|---|
| `FNA3D_FORCE_DRIVER` | `OpenGL` | `Vulkan` currently shows a black window on Asahi, see below |
| `CELESTE_FMOD_FADE_MS` | `1500` | length of the music fade-out for events FMOD 2 would never stop |
| `CELESTE_FMOD_GENERAL_MS` | `15000` | safety net: any other event still "stopping" after this long is force-stopped |
| `CELESTE_FMOD_REAPER` | on | set to `0` to turn the music fix off completely |
| `DOTNET` | `dotnet` | path to the dotnet binary |

## Known issues

- **Vulkan backend: black window.** The game runs and the audio plays, but nothing is drawn.
  FNA3D logs `RGBA swapchain unsupported, falling back to BGRA with swizzle`. The cause is not
  known. Use OpenGL.
- **No Steam features.** Achievements, stats and cloud sync through the API do nothing.
- **Cloud saves.** Celeste uses Steam's file-based Auto-Cloud. The native build keeps its saves
  in `~/.local/share/Celeste/Saves`, what I do is set the launch options of Celeste in steam(ran with default compatibility tools) to run my version via play.sh, and Steam backs up as usual:
  ```bash
  /path/to/game/dir/play.sh %command%
  ```
- **The in-game Everest updater** cannot finish on aarch64 (it runs an x86-64 installer), so use
  `arm64/update-everest.sh`. Mod updates are not affected.
- **Audio fixes were chosen by measurement on one install.** The list of events the music fix
  treats as stuck (`/remix/`, `/cassette/`, `menu/credits`, `cassette_rooms`, `seed_complete`)
  comes from the test below. Other Celeste versions or mods may have more.

## How the music problem was found

`arm64/tools/fmodtest.c` talks to the real FMOD 2 library (no shim, no game): it starts every
event matching a filter, stops them the way Celeste does, waits, and lists the ones still alive.

```bash
gcc -o fmodtest arm64/tools/fmodtest.c \
    lib-fmod2/libfmodstudio_real.so lib-fmod2/libfmod.so.13 -Wl,-rpath,$PWD/lib-fmod2
./fmodtest <game dir>/Content/FMOD/Desktop event:/ allowfade   # fade-out stop: 27 of 819 stay alive
./fmodtest <game dir>/Content/FMOD/Desktop event:/ immediate   # immediate stop: 0 stay alive
./fmodtest <game dir>/Content/FMOD/Desktop event:/music/remix/01_forsaken_city probe
```

The `probe` mode showed that a stuck event gets no fade of its own: it sits in the "stopping"
state with its output volume still at about 0.98 after six seconds. `SetVolume` does work on
such an event, which is what the fix uses. On NixOS the program also needs `libstdc++` on the
library path (`LD_LIBRARY_PATH=$(nix-build '<nixpkgs>' -A stdenv.cc.cc.lib --no-out-link)/lib`).

## Troubleshooting

| Message | Cause / fix |
|---|---|
| `Could not load file or assembly 'Steamworks.NET, Version=2024.8.0.0'` | The shipped DLL is x86-64-only. Run `arm64/build-steamworks-net.sh <game dir> --force`. |
| `Unable to load shared library 'steam_api'` | Run `arm64/install.sh` (builds the stub). |
| `Exec format error` for `EverestSplash-linux` | Run `arm64/fix-splash.sh`. |
| Exec format error for .../Celeste after “Restarting with LD_LIBRARY_PATH” | Run arm64/fix-launcher.sh <game dir> (an Everest update restores the x86-64 file). |
| `SDL_Vulkan_LoadLibrary failed!` / `No supported FNA3D driver found!` | Use nixpkgs' SDL2 (`install.sh` does this on NixOS). |
| `libstdc++.so.6: cannot open shared object file` (test program only) | Add libstdc++ to `LD_LIBRARY_PATH`, see above. |
| Music from B/C sides keeps playing after leaving the map | The music fix is not in the shim you are running; rebuild with `make -C fmod sound`. |
| Game exits at once with `Steam not found!` in `~/.local/share/Celeste/errorLog.txt` | The Steam stub is missing a return value; see `make-steam-stub.sh` for what it returns. |

## Open items

- The Vulkan black window.
