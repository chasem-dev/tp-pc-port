# Twilight Princess PC Port — Implementation Plan

## Context

We're building a native PC port of the Twilight Princess GameCube decompilation (github.com/zeldaret/tp, cloned to `~/Programming/tp-pc-port/`). The approach is a **source port**: compile the decompiled C++ with GCC/Clang, replace all GameCube hardware calls with a PC platform layer using SDL2 + OpenGL 3.3.

The working Animal Crossing GC PC port (`~/Programming/ACGC-PC-Port/`) provides proven implementations for ~70% of the platform layer (GX graphics, OS stubs, disc I/O, audio, input, etc.) since both are GameCube titles using the same SDK.

**Target**: 64-bit macOS ARM64 first, then Linux/Windows. Render the game in a window.

## Codebase Stats
- 1,085 game C++ files (767 actors) + 282 JSystem lib files + 187 Dolphin SDK files
- Language: C++ (classes, templates, virtuals — but no RTTI, no exceptions, no STL)
- GXCallDisplayList used in only 4 JSystem files (13 calls)
- Paired-singles: 465 uses in 22 files (392 in d_a_movie_player.cpp alone)

---

## Phase 0: Type Safety & Infrastructure

**Goal**: Fix the 64-bit type landmine and create the PC port skeleton.

### 0a. Fix `libs/dolphin/include/dolphin/types.h`
- `typedef signed long s32` → `typedef int32_t s32` (long is 8 bytes on LP64!)
- `typedef unsigned long u32` → `typedef uint32_t u32`
- Same for volatile variants. Add `#include <stdint.h>` and static assertions.
- Guard with `#ifdef TARGET_PC` to preserve decomp matching builds.

### 0b. Fix `include/global.h`
- `ROUND`/`TRUNC` macros cast to `(u32)` — change to `(uintptr_t)` for pointer math
- `POINTER_ADD_TYPE` uses `(unsigned long)` — change to `(uintptr_t)`
- Add C implementations for PPC intrinsics under `#ifdef TARGET_PC`:
  - `__cntlzw(x)` → `x ? __builtin_clz(x) : 32`
  - `__rlwimi(val,ins,shift,mb,me)` → bit rotate-and-insert helper
  - `__dcbf`, `__dcbz`, `__sync` → no-ops
  - `PPCMffpscr()` → return 0; `PPCMtfpscr()` → no-op

### 0c. Create PC headers
- `pc/include/pc_platform.h` — adapted from AC: window title "Twilight Princess", 608x448 default, 96MB arena
- `pc/include/pc_types.h` — from AC (already uses `int32_t`/`uint32_t`)
- `pc/include/pc_bswap.h` — from AC (byte-swap helpers)
- `pc/include/pc_gx_internal.h` — from AC (GX state machine struct, dirty flags)

### Test
`sizeof(u32) == 4` and `sizeof(s32) == 4` verified via static assert. `__cntlzw(0)` returns 32.

---

## Phase 1: CMake Build System

**Goal**: All source files compile (linking will fail — that's expected).

### 1a. Create `pc/CMakeLists.txt`
Model after AC's `pc/CMakeLists.txt`. Key settings:
- **C++17**, GCC or Clang
- **Flags**: `-O0 -fno-strict-aliasing -fwrapv -fpermissive -Wno-narrowing`
- **Defines**: `TARGET_PC`, `VERSION=0` (GCN USA), `NDEBUG`
- **Source glob**: `src/*.cpp`, `libs/JSystem/src/**/*.cpp`
- **Exclude**: `libs/dolphin/src/*`, `libs/revolution/*`, `libs/PowerPC_EABI_Support/*`, `libs/TRK_MINNOW_DOLPHIN/*`
- **Include paths**: `include/`, `src/`, `libs/JSystem/include/`, `libs/dolphin/include/`, `pc/include/`
- **main() rename**: `-Dmain=tp_main_entry` on `src/m_Do/m_Do_main.cpp`
- SDL2 + GLAD dependency setup

### 1b. Create stub headers for revolution SDK
- `pc/include/revolution/sc.h` — empty stubs for `SCInit()`, `SCCheckStatus()`
- Others as needed (most gated behind `#if PLATFORM_WII`)

### 1c. Replace GXVert.h hardware writes
Under `#ifdef TARGET_PC` in `libs/dolphin/include/dolphin/gx/GXVert.h`:
- Replace `GXWGFifo` FIFO writes with function declarations
- All `GXPosition*`, `GXNormal*`, `GXColor*`, `GXTexCoord*` become `extern` function calls instead of inline FIFO writes

### 1d. Fix paired-singles compilation
- `d_a_movie_player.cpp`: wrap 392 paired-single uses in `#ifdef __MWERKS__` / `#else` with scalar C equivalents (or `#ifdef TARGET_PC` stub the entire file as no-op actor)
- Remaining 21 files: ~73 occurrences, mostly `psq_st`/`psq_l` (quantized load/store) — replace with regular float ops

### Test
```bash
cd pc/build && cmake .. && make -j$(nproc) 2>&1 | grep "error:" | head -20
```
Target: zero compilation errors. Thousands of linker "undefined reference" errors are expected.

---

## Phase 2: Dolphin SDK Stubs

**Goal**: All SDK symbols resolved. Binary links successfully.

### Files to create (all in `pc/src/`), ported from AC equivalents:

| PC file | AC source | What it provides |
|---------|-----------|-----------------|
| `pc_os.cpp` | `pc_os.c` | Arena (mmap 96MB), timers, thread stubs, message queues, mutexes, cache no-ops |
| `pc_dvd.cpp` | `pc_dvd.c` + `pc_disc.c` | Disc image reader (ISO/CISO/GCM), FST, DVD API, Yaz0 decomp |
| `pc_vi.cpp` | `pc_vi.c` | VIInit, VIConfigure, VIWaitForRetrace (SDL frame pacing), VIGetRetraceCount |
| `pc_pad.cpp` | `pc_pad.c` | PADInit, PADRead (SDL gamepad + keyboard) |
| `pc_card.cpp` | `pc_card.c` | CARD API → filesystem (`saves/` directory) |
| `pc_aram.cpp` | `pc_aram.c` | ARInit, ARAlloc, ARStartDMA (malloc'd 16MB buffer) |
| `pc_audio.cpp` | `pc_audio.c` | AI/AX/DSP stubs (no-op initially, SDL audio backend later) |
| `pc_mtx.cpp` | `pc_mtx.c` | PSMTXIdentity, PSMTXConcat, C_MTXPerspective, etc. |
| `pc_stubs.cpp` | `pc_stubs.c` | EXI, SI, GBA, debug, TRK — catch-all no-ops |
| `pc_misc.cpp` | — | OSReport→printf, OSPanic→exit, interrupt no-ops, reset stubs |

### Key adaptations from AC:
- **Arena size**: 96MB (TP needs more heaps than AC's 48MB)
- **Thread stubs**: `OSCreateThread` stores func ptr, `OSResumeThread` calls it directly (single-threaded initially). If deadlocks appear, promote to real SDL_Thread.
- **DVDDiskID**: Return `GZ2E01` game ID
- **OSGetConsoleType**: Return retail
- **OSGetResetCode**: Return 0 (cold boot)
- **OSLink/OSUnlink**: Return TRUE (RELs statically linked)

### Test
```bash
make -j$(nproc) 2>&1 | grep "undefined reference" | wc -l
```
Target: only GX symbols remain undefined.

---

## Phase 3: GX Graphics Layer

**Goal**: Full GX API with OpenGL backend. This is the critical path.

### 3a. Port AC's GX implementation
Copy and adapt from AC:
- `pc_gx.c` (1,938 lines) → `pc_gx.cpp` — 196 GX functions: state machine, vertex buffering, GL draw calls
- `pc_gx_tev.c` → `pc_gx_tev.cpp` — shader loading/compilation
- `pc_gx_texture.c` → `pc_gx_texture.cpp` — all 10 GC texture format decoders, 2048-entry cache
- `pc/shaders/default.vert` + `default.frag` — TEV pipeline in GLSL

### 3b. Implement GXVert functions
In `pc_gx.cpp`, implement all vertex submission functions:
- `GXPosition3f32/2f32/3s16/1x16/1x8` — buffer into CPU-side vertex array
- `GXNormal3f32/3s16/1x16/1x8` — same
- `GXColor4u8/1u32/1x16/1x8` — same
- `GXTexCoord2f32/2s16/1x16/1x8` — same
- Indexed variants (`1x16`, `1x8`) dereference arrays set via `GXSetArray()`

### 3c. Implement GXCallDisplayList interpreter (~800-1200 lines)
Only 4 JSystem files call this (13 total calls). The interpreter parses GX hardware command streams:

| Opcode | Name | Action |
|--------|------|--------|
| `0x08` | LOAD_CP_REG | Update vertex format/descriptor state |
| `0x10` | LOAD_XF_REG | Update transforms/lighting registers |
| `0x61` | LOAD_BP_REG | Update TEV/pixel/texture config |
| `0x80-0xB8` | DRAW_PRIM | Decode inline vertex data per current VAT, submit to GL |
| `0x20-0x38` | LOAD_INDX | Indexed register load from memory |

Reference: Dolphin emulator's `OpcodeDecoder.cpp`, `BPMemory.h`, `XFMemory.h` for register formats.

### 3d. Add TP-specific GX functions not in AC
- `GXSetTexCoordGen2()` — extended texgen with normalize + post-transform
- `GXSetLineWidth()`, `GXSetPointSize()`
- Various `GXGet*()` query functions
- Extend TEV stages from 3 → 16 in shader (TP uses more)

### Test
Standalone test: call `GXBegin(GX_QUADS, ...)`/`GXPosition3f32`/`GXEnd()` → colored quad renders in window.

---

## Phase 4: Boot Sequence

**Goal**: Window opens, game loop runs, black screen (no crash).

### 4a. Create `pc/src/pc_main.cpp`
Adapted from AC's `pc_main.c`:
```
main() → parse args → SDL init → GL 3.3 window → GLAD → arena alloc
       → disc image open → tp_main_entry(0, NULL) [game takes over]
```

### 4b. Stub DynamicLink/REL system
In `src/DynamicLink.cpp` under `#ifdef TARGET_PC`:
- `do_load()` → return (no disc load needed, code is in binary)
- `do_link()` → return TRUE (skip REL prolog call)
- `do_unlink()` / `do_unload()` → no-ops

All 767 actor .cpp files are already compiled into the executable. `f_pc_profile_lst.cpp` has the complete profile list.

### 4c. Wire game loop to SDL
In `m_Do_main.cpp:main01()` the `do {} while(true)` loop needs:
- `pc_platform_poll_events()` call (SDL event pump, quit handling)
- `pc_platform_swap_buffers()` after rendering
- Frame pacing (VIWaitForRetrace already handled by pc_vi.cpp)
- Break condition: `if (!g_pc_running) break;`

### Test
```bash
./TwilightPrincess rom/GZ2E01.iso
```
Window opens. Console shows heap init messages. Black screen. Game loop running (frame counter incrementing). No crash for 10+ seconds.

---

## Phase 5: Asset Loading

**Goal**: Game reads files from disc image. JKRArchive mounts RARC files.

### 5a. DVD file path resolution
JKRDvdRipper calls `DVDOpen("/path/to/file")` → our `pc_dvd.cpp` resolves from disc image FST or extracted files on disk.

### 5b. Byte-swap RARC archive headers
RARC headers are big-endian on disc. Add `#ifdef TARGET_PC` byte-swap in:
- `libs/JSystem/src/JKernel/JKRArchivePri.cpp` — swap magic, section offsets, node counts
- `libs/JSystem/src/JKernel/JKRArchivePub.cpp` — swap file entries

### 5c. JKRAram integration
Wire `pc_aram.cpp` so JKRAramStream DMA transfers work (ARAM↔main RAM memcpy).

### Test
`JKRArchive::mount()` succeeds for `RELS.arc`. File entries listed in verbose output.

---

## Phase 6: First Rendered Frame

**Goal**: Something visible on screen.

### 6a. Debug rendering pipeline
Add printf tracing in `GXBegin`/`GXEnd`/`GXCallDisplayList` to verify draw calls are happening.

### 6b. Fix logo/title screen rendering
The boot sequence goes: Logo scene → Menu scene → Play scene. Logo renders 2D screens (Nintendo logo) via J2D which uses immediate-mode GX calls (not display lists). This should work with Phase 3a/3b alone.

### 6c. Texture loading path
BMD/BDL textures go through: JKRArchive load → J3DModelLoader parse → `GXInitTexObj()` → `GXLoadTexObj()` → pc_gx_texture decodes GC format → GL texture upload.

Add byte-swap in J3D header parsing for model data (BMD magic, section offsets).

### Test
Visible frame: Nintendo logo, colored background, or debug text on screen.

---

## Phase 7: Gameplay

**Goal**: Title screen renders. Potentially playable.

### 7a. Scene transitions
Fix fapGm framework to progress Logo → Menu → Play scenes.

### 7b. Ongoing endian fixes
Each new asset type loaded will surface byte-swap needs: stage data, collision, actor params, save data. Fix iteratively.

### 7c. Movie player
Stub `d_a_movie_player.cpp` entirely (skip FMVs) — 392 of 465 paired-single uses gone.

### 7d. Audio (optional, later)
JAudio2 → AX/DSP → SDL2 audio. Major subsystem, defer until rendering works.

---

## Critical Files Reference

| File | Why it matters |
|------|---------------|
| `libs/dolphin/include/dolphin/types.h` | s32/u32 must be fixed for 64-bit |
| `libs/dolphin/include/dolphin/gx/GXVert.h` | Hardware FIFO writes → PC function calls |
| `include/global.h` | PPC intrinsics, pointer macros, compiler compat |
| `src/m_Do/m_Do_main.cpp` | Entry point, game loop, thread creation |
| `src/m_Do/m_Do_graphic.cpp` | Graphics pipeline setup, direct GX usage |
| `src/DynamicLink.cpp` | REL loading — must stub for static link |
| `src/f_pc/f_pc_profile_lst.cpp` | Actor registry — confirms all actors linkable |
| `src/d/actor/d_a_movie_player.cpp` | 392 paired-single uses — stub entire file |
| AC `pc/CMakeLists.txt` | Template for build system |
| AC `pc/src/pc_gx.c` | GX→GL base implementation (196 functions) |
| AC `pc/src/pc_os.c` | OS stubs template (arena, timers, threads) |
| AC `pc/src/pc_disc.c` | Disc image reader (reuse directly) |
| AC `pc/shaders/default.frag` | TEV pipeline shader (reuse + extend) |

## Estimated Effort

| Phase | Sessions | Key challenge |
|-------|----------|--------------|
| 0 | 1 | Type fixes, intrinsic stubs |
| 1 | 1-2 | CMake + compilation errors |
| 2 | 2-3 | SDK stub coverage |
| 3 | 3-5 | **GX display list interpreter** |
| 4 | 1-2 | Boot sequence wiring |
| 5 | 1-2 | Asset loading + byte-swap |
| 6 | 1-3 | Debug first rendered frame |
| 7 | 2-4+ | Ongoing gameplay fixes |
