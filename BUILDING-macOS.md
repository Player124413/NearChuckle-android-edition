# Building and running on macOS (Apple Silicon)

macOS is a development host for the port, not a release target. The point of the
Mac build is a fast edit/compile/run loop and a visual reference renderer: Apple's
legacy OpenGL 2.1 context still exposes `GL_ARB_vertex_program`,
`GL_ARB_fragment_program` (with `_shadow`), S3TC, two-sided stencil and
`ARB_occlusion_query`, so the original `XRenderOGL` runs unchanged.

## Prerequisites

```
brew install cmake ninja sdl3 openal-soft libvorbis
```

Cg is not needed (and does not exist for arm64): CMake forces `DISABLE_CG` on
macOS and the renderer loads the precompiled ARB assembly from the shader cache.
GLU comes from `OpenGL.framework`.

## Build

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build
```

Output lands in `bin/x64-Debug/` (the directory name comes from the pointer
size, not the CPU). Modules keep the `.so` suffix on macOS on purpose: CrySystem
loads them by hard-coded `lib*.so` names.

## Run

The launcher derives the game root from its own directory: it reads `getcwd()`,
sets `MODULE_PATH` to `./<that dir>/`, then `chdir("../")`. So the binaries must
sit in a **real** directory directly inside the Far Cry install. A symlinked
directory does not work, because `getcwd()` on macOS returns the physical path
and the parent becomes the build tree. Per-file symlinks are fine:

```
mkdir -p "/path/to/FarCry/macos"
for f in bin/x64-Debug/*; do ln -sfn "$PWD/$f" "/path/to/FarCry/macos/$(basename "$f")"; done
```

Put an OpenGL shader cache pak into `FCData/` (see the README; the
`GL_Shaders_*.pak` file is the one for this renderer, the `D3D9_*` files are for
DXVK). Then:

```
cd /path/to/FarCry/macos && ./FarCry -DEVMODE
```

`log.txt` is written to the game root. Two `TemplVFog*` system shaders report
`Fail` at startup; those are NVIDIA vertex-fog templates and fail on any
non-NVIDIA driver.

## What the macOS port changed, and why

Everything is in the `macos-port` history, but the reasoning is worth keeping:

- **`__linux` -> `LINUX`.** The tree used the compiler macro `__linux` to select
  its POSIX code paths and the project macro `LINUX` (defined by CMake for every
  non-MSVC build) for its type layer. Apple clang defines neither compiler macro,
  and defining `__linux` by hand leaks into SDL's headers (they then include
  Linux's `endian.h`). The two macros never meant different things in practice,
  so the first-party sources now test `LINUX` only. Third-party code under the
  tree (FreeType, zlib, Lua, nvparse) was left alone.
- **Clang strictness.** GCC's `-fpermissive` hid several things clang rejects:
  pointer-to-`int` truncations (now explicit `intptr_t`/`uintptr_t` casts, made
  correct rather than silenced), `register`, `new (T*[n])`, an enum assigned
  from `0`, a `std::string` passed through varargs, two `Matrix34_tpl` helpers
  calling members that never existed, and `isneg` overloads declared only for
  `__e2k__`. Taking the address of a temporary passed straight to a callee is
  pervasive and safe, so that one is allowed with `-Wno-address-of-temporary`.
- **Headers macOS lacks.** `<malloc.h>`, `stat64`/`fstat64`, `__finite`, an
  uninitialised static `pthread_mutex_t` (EINVAL on macOS), a C-only
  `wchar_t` typedef, FreeType's Carbon backend (`DARWIN_NO_CARBON`), zlib's
  classic-MacOS `fdopen` stub, and `libc` already providing `strnstr`.
- **No x86 cycle counter.** `GetTicks()` used `rdtsc` inline asm on x86 and
  `SDL_GetTicks()` elsewhere, which dragged SDL into every module. Non-x86 now
  uses `clock_gettime(CLOCK_MONOTONIC)`; it only feeds profilers.
- **Linking.** Modules reference each other's symbols and rely on Linux's lazy
  resolution at `dlopen` time; macOS needs `-Wl,-undefined,dynamic_lookup` for
  the same behaviour.

## 64-bit

The engine has always had a 64-bit configuration (Crytek shipped the AMD64
build; see `SourceCode/AMD64_ProjectFiles_VS2005` and `BinWin64`), and this port
is built and played on x86_64 Linux. The pointer casts fixed above were latent
truncations that happened to survive there.

## Engine fixes found while playing on this port

- **Single player ran the multiplayer fixed-step physics.** `CXGame::Update`
  passed `ESYSUPDATE_MULTIPLAYER` unconditionally (a `#if 1` over the intended
  `IsMultiplayer()` test), so rigid bodies advanced in 10 ms quanta with
  catch-up and no interpolation: at 60 fps a vehicle moved two, two, one
  quanta per frame, and at 300 fps only every third frame. Single player now
  steps physics by the frame time; multiplayer keeps the fixed step.
- **Vehicle passengers' rotation was refused on some bank angles.** The
  living-entity physics rejects a rotation that would put the player capsule
  inside geometry and keeps the previous one; for a passenger that geometry is
  the vehicle, so the glider-following orientation was dropped for a frame and
  the third-person camera target jumped by metres. The player now carries
  `lef_loosen_stuck_checks` while in a vehicle and the check honours it.
- **`g_maxfps` settled near twice the requested rate** because the sleep was
  counted into the next frame's time; it now paces against a deadline.
- **Camera tracing:** `FARCRY_CAMLOG=1` prints the player camera update, the
  third-person camera maths, the eye offset and physics orientation, the
  vehicle's physics state and the view camera handed to the renderer, one
  line each per frame, to stderr.

## Known leftovers

- CMake still passes `-D_AMD64_` on arm64 because `CPUDetect.h` refuses to build
  without it. Harmless for now, wrong in principle.
- `PROC_INTEL` / `DO_ASM` in the renderer CMake are x86-only and already gated.
