# The Legend of Zelda: Twilight Princess — PC Port

This project is a **native PC port** of *Twilight Princess* for GameCube. It uses [zeldaret/tp](https://github.com/zeldaret/tp) as its **base foundation**: the game logic and data layouts come from that codebase, while this repository adds a desktop platform layer (SDL2, OpenGL 3.3) so the game runs on macOS, Linux, and Windows. The approach is similar in spirit to the [Animal Crossing (GameCube) PC port](https://github.com/flyngmt/ACGC-PC-Port)—run the game on PC and read assets from a disc image instead of a console.

The CMake project lives under [`pc/`](pc/). Builds target **64-bit** hosts first (e.g. macOS ARM64, Linux, Windows).

> [!IMPORTANT]
> This repository does **not** contain game assets or original ROM dumps. You must supply your **own legally obtained copy** of the **North American (USA) GameCube** release as a disc image (product code **GZ2E01**). Other regions or platforms are not supported by this build. The executable reads archives and files from that image at runtime (no separate asset extraction step is required for normal play).

<!--ts-->
- [Game data and the `rom/` folder](#game-data-and-the-rom-folder)
- [Dependencies](#dependencies)
- [Build](#build)
- [Run](#run)
- [Environment variables (startup)](#environment-variables-startup)

## Game data and the `rom/` folder

The PC port expects a dump of the **USA GameCube** disc—typically an `.iso`, `.gcm`, or `.ciso` file matching retail **GZ2E01** (North America). Do not use European, Japanese, or Wii editions unless you know the port has been updated for that build.

If you have used the **Animal Crossing** GameCube PC port, the workflow here is the same in principle: keep that **single disc image** on disk and point the game at it. Twilight Princess loads data through the port’s “DVD” layer, which reads from that file as if it were a GameCube disc.

**Practical layout:**

- Create a directory named `rom/` in the **repository root** and place your North American image there, for example `rom/GZ2E01.iso`.
- You can also put the image under `orig/`, or pass the path on the command line (see [Run](#run)).

If you start the executable **without** a path, it searches the current directory and then, in order: `rom/`, `orig/`, `build/rom/`, and `../build/rom/` for a suitable image. Running from the repo root with `rom/GZ2E01.iso` present is usually enough.

## Dependencies

- **CMake** 3.16+
- **SDL2** development libraries
- **OpenGL** development packages where your OS separates them from the base system
- A **C++ toolchain** (GCC, Clang, or MSVC)

| OS | Notes |
|----|--------|
| **macOS** | `brew install cmake sdl2` |
| **Linux** | `cmake`, C++ toolchain, `libsdl2-dev` (or `SDL2-devel`), OpenGL dev packages as needed |
| **Windows** | CMake, a C++ toolchain (Visual Studio or MinGW), and SDL2 (see `pc/CMakeLists.txt` for `SDL2_DIR` if using bundled hints) |

## Build

Configure and compile the PC executable under **`pc/build`** (do not use the repository root `build/` folder for this):

```sh
cmake -S pc -B pc/build
cmake --build pc/build --parallel
```

The binary is written to **`pc/build/bin/TwilightPrincess`** (`.exe` on Windows).

## Run

From the **repository root** (so `rom/` and `orig/` resolve predictably):

```sh
./pc/build/bin/TwilightPrincess /path/to/GZ2E01.iso
```

Or rely on auto-discovery after placing an image under `rom/`:

```sh
./pc/build/bin/TwilightPrincess
```

Useful flags:

- `--verbose` or `-v` — extra diagnostic output
- `--no-framelimit` — disable the frame limiter
- `--headless` — skip video init (useful for boot tests)
- `--disc /path/to/GZ2E01.iso` — explicit disc path (alternative to the positional argument)

## Environment variables (startup)

These are read from the process environment (not command-line flags). Combine with the usual shell syntax, for example:

```sh
TP_SKIP_LOGO=3 ./pc/build/bin/TwilightPrincess
```

| Variable | Meaning |
|----------|---------|
| **`TP_SKIP_LOGO`** | Skips part of the opening boot sequence so you reach the main flow faster while debugging. Integer **0** (default): full boot from the start of the logo chain. **1**: resume at the Nintendo logo segment. **2**: resume at the Dolby segment. **3**: resume at the DVD wait screen (furthest skip). |
| **`TP_SKIP_WARNING`** | Set to **`1`** to preset the internal “warning already shown” state at startup (PC-only helper for iteration). |

Other `TP_*` variables exist for graphics debugging and crash handling (for example shader fallbacks on the OpenGL path); see the source under `pc/` and `src/` if you need them.
