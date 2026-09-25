# Plan: an OpenGL ES 3.0 renderer for Far Cry

Goal: a `libXRenderGLES.so` that renders the whole game on GLES 3.0, developed on
this Mac against ANGLE with the existing `XRenderOGL` (Apple's GL 2.1) as the
visual reference, then shipped in Sigma Touch on Android.

## 1. What we are porting (facts that shape the design)

- `XRenderOGL` is 46k lines. Every GL call goes through one function-pointer
  table: 633 `GL_PROC` entries in `GLFuncs.h`, resolved by `FindProcs()` in
  `GLSystem.cpp` and called through `crygl*` names. Nothing links libGL.
- The programmable path is ARB assembly. Cg is disabled; programs are loaded as
  text from the cache (797 files, 194 vertex + 247 pixel bases). The cache uses
  19 opcodes, 2D/CUBE/RECT samplers, `OPTION ARB_fog_linear` (212 files) and
  `ARB_fragment_program_shadow` (18), `program.env[0..17]` plus fixed c28/c31,
  and no `state.*` references: every matrix is computed on the CPU and uploaded.
- The fixed-function path drives `Layer` scripts, fonts, HUD, particles, sky,
  decals and debug drawing. Used features, from the source: texenv COMBINE with
  MODULATE / REPLACE / ADD / ADD_SIGNED / INTERPOLATE / BLEND and PRIMARY_COLOR /
  PREVIOUS / CONSTANT sources with ONE_MINUS operands and RGB_SCALE; texgen
  OBJECT_LINEAR, EYE_LINEAR, SPHERE_MAP, REFLECTION_MAP, NORMAL_MAP; linear fog
  (one EXP2 site); alpha test; one clip plane; `GL_LIGHTING` with one light at a
  single site; ~70 `glBegin` blocks with ~490 `glVertex/Color/TexCoord` calls;
  `GL_QUADS`; classic client arrays for 16 fixed interleaved vertex formats.
- No FBOs. Render-to-texture is `glCopyTexSubImage2D` from the back buffer
  (cube maps, shadow maps, screen effects, flares). Depth is read back with
  `glReadPixels(GL_DEPTH_COMPONENT)` for the sun/flare visibility probes and the
  shadow-map path. `glGetTexImage` has 23 sites (flare alpha probes, texture
  save/dump, shadow debug).
- Textures: RGB/RGBA8, DXT1/3/5, 3Dc normal maps (`USE_3DC`), BGRA, paletted,
  one 3D texture path, rectangle textures for screen copies, `GL_CLAMP`,
  SGIS_generate_mipmap, LOD bias, anisotropy. NV HILO/DSDT/signed formats only
  appear when `NV_texture_shader` is present, which it never will be.
- Stencil shadow volumes use two-sided stencil; occlusion queries are the NV
  flavour and read pixel counts.
- Everything NVIDIA-specific (register combiners, texture shaders, nvparse,
  VAR/fence) is gated on extensions we will not advertise and never runs.

## 2. Architecture decision: a translation layer inside the renderer, then shrink it

Two ways to do this:

- **(A) Rewrite the renderer's draw paths for GLES directly.** Touches most of
  `GLRendPipeline.cpp` (8k lines), `GLShaders.cpp`, `GLRERender.cpp`,
  `GL_Renderer.cpp`, `GLTextures.cpp`. Months before the first frame.
- **(B) Keep the renderer, replace what the function table points at.** A new
  `GLES/` layer implements the ~200 entry points the renderer actually calls,
  forwarding the GLES-native ones and emulating the legacy ones (immediate mode,
  matrix stacks, texenv/texgen/lighting/fog/alpha test via generated shaders,
  ARB programs via translation). This is what gl4es does generically; ours is
  purpose-built for one caller, so it is a fraction of the size and we control
  both sides.

**Decision: (B), with the explicit intent to shrink it.** Once the game renders,
the hot paths (`EF_FlushHW`, vertex buffers, `EF_SetColorOp`) get converted to
call the layer's "modern" API directly and the legacy emulation behind them is
deleted. The shim is scaffolding for correctness first, not the final shape.

Why not gl4es itself: no macOS backend (kills the ANGLE dev loop), 0-bit
occlusion queries, 3D textures as one layer, RECT unsupported in shaders,
unknown OPTION aborts the program, and we would be tuning someone else's
generic state machine on mobile instead of our own.

## 3. Module layout and selection

- New CMake target **`XRenderGLES`** built from the same source list as
  `XRenderOGL` plus `RenderDll/XRenderOGL/GLES/*.cpp`, with `-DGLES_RENDERER`.
  Both modules coexist so the Mac can A/B them.
- `SystemInit.cpp` learns a third driver string, `r_Driver=GLES`, mapping to
  `libXRenderGLES.so` next to the existing OpenGL/Direct3D9 choices.
- Context: `SDL_GL_SetAttribute(PROFILE_MASK=ES, 3.0)` in `GLSystem.cpp` under
  `GLES_RENDERER`. On macOS set `SDL_HINT_EGL_LIBRARY` / `SDL_HINT_OPENGL_LIBRARY`
  to the staged ANGLE (openQ4's recipe; the staged copy is at
  `~/Android/GIT/Q4/openQ4/.tmp/angle-macos/`). Request RGBA8, depth 24,
  stencil 8. Android needs nothing special.
- `FindProcs()` under `GLES_RENDERER` resolves each `GL_PROC` name from the
  shim's export table first, then `SDL_GL_GetProcAddress`. Names the shim does
  not provide and the driver lacks get a logging stub, so a missed call shows
  up in the log instead of a null jump.
- `CheckOGLExtensions()` under `GLES_RENDERER` reports a fixed feature set:
  ARB vp/fp, VBO, cube maps, S3TC (if the ES driver has it), anisotropy,
  two-sided stencil, occlusion query, texture_env_combine, and nothing NV/ATI.
  This lands on the same `RFT_HW_PS20` / GFFX-class path Mesa users get today.

## 4. The GLES layer, component by component

Each item lists what it replaces, how, and the size class (S < 300 lines,
M < 1000, L > 1000).

1. **State tracker (M).** Enables (`GL_TEXTURE_2D/CUBE/3D`, `GL_LIGHTING`,
   `GL_FOG`, `GL_ALPHA_TEST`, `GL_CLIP_PLANE0`, texgen S/T/R/Q per unit,
   `GL_NORMALIZE`, `GL_VERTEX/FRAGMENT_PROGRAM_ARB`), current color/normal/
   texcoords, texenv per unit, texgen planes, fog params, alpha func/ref, light
   0, material, `glGetIntegerv`/`glGetFloatv` for the legacy enums the renderer
   queries (`GL_MODELVIEW_MATRIX`, `GL_PROJECTION_MATRIX`, `GL_FOG`, `GL_VIEWPORT`,
   `GL_MAX_*`, `GL_CURRENT_COLOR`). Native state (blend, depth, cull, stencil,
   scissor, colour mask, polygon offset, viewport) is forwarded; `GL_CLAMP` maps
   to `CLAMP_TO_EDGE`; two-sided stencil maps to `glStencil*Separate`.
2. **Matrix stacks (S).** Modelview, projection, texture per unit; `glPush/Pop`,
   `glLoad/Mult`, `glOrtho/Frustum/Translate/Rotate/Scale`, `gluPerspective/
   LookAt`, and CPU `gluProject/UnProject`. Only the fixed-function and
   immediate-mode paths need these; ARB programs never read matrix state.
3. **Vertex input (M).** Client arrays (`glVertexPointer` etc. + `glClientActive
   Texture`) and VBOs map to generic attributes at NV-style slots: position 0,
   normal 2, colour 3, secondary colour 4, texcoord n at 8+n. Same slots are used
   by translated ARB programs and generated FFP shaders. `glDrawElements` /
   `glDrawArrays` with `GL_QUADS` expand through a cached index buffer.
   `glLock/UnlockArraysEXT` become no-ops; `glMapBufferARB(WRITE_ONLY)` maps to
   `glMapBufferRange`.
4. **Immediate mode (S/M).** `glBegin..glEnd` collects vertices with the current
   attribute state into a streaming VBO and draws with the current program;
   `GL_QUADS`/`GL_TRIANGLE_FAN`/`GL_LINES` handled. ~70 sites, mostly 2D, debug
   and small effects.
5. **Fixed-function shader generator (L).** A state hash → GLSL ES 3.00 program
   cache. Vertex side: MVP, optional lighting for light 0, texgen per unit
   (object/eye linear, sphere, reflection, normal map), texture matrices, fog
   coordinate. Fragment side: one stage per enabled unit implementing the
   texenv COMBINE program as written by `EF_SetColorOp`, then fog mix, alpha
   test discard, clip-plane discard. Generated programs are cached in memory
   and persisted with `glGetProgramBinary` (core in ES 3.0) for device start-up.
6. **ARB program translator (M).** `glProgramStringARB` → GLSL ES 3.00 at load.
   The instruction set is the 19 opcodes in the cache; `program.env[n]` and
   the fixed c28/c31 become `uniform vec4 env[32]`; `ARL` becomes uniform
   array indexing; `OPTION ARB_fog_linear` emits the fog mix from the layer's
   fog state; `ARB_fragment_program_shadow` + `SHADOW2D` becomes
   `sampler2DShadow`; `RECT` samplers become `sampler2D` with a per-unit
   texture-size uniform to normalise coordinates; `TXP` becomes `textureProj`.
   Precision: `highp` for positions and texcoords, `mediump` elsewhere, and
   every shader is tried on a real device early (see Sigma Touch
   `docs/porting/gotchas.md` on precision).
7. **Program linking (S).** A bound ARB vertex program pairs with a bound ARB
   fragment program, or with the generated FFP half when only one is bound
   (both cases occur: `HW` passes may omit either). Cache by (vp, fp-or-state
   hash). `glProgramEnvParameter4fvARB` writes a dirty range uploaded at draw.
8. **Textures (M).** Format table: RGB/RGBA/RGB8/RGBA8 → sized RGBA8; BGRA →
   swizzle on upload; LUMINANCE/ALPHA → R8 with a swizzle in the generated
   shader or `GL_TEXTURE_SWIZZLE_*` (core in ES 3.0); paletted → expanded on
   upload; DXT1/3/5 → `EXT_texture_compression_s3tc` when present (ANGLE,
   Adreno, Tegra) else CPU decode to RGBA8 (Mali, PowerVR); 3Dc → `RGTC` when
   present (ANGLE has it) else CPU decode to RG8; 1D → 2D with height 1; 3D →
   native; RECT → 2D NPOT. `SGIS_generate_mipmap` → `glGenerateMipmap` after
   upload; LOD bias → `GL_TEXTURE_LOD_BIAS` is absent in ES, so bias moves into
   sampler state emulation or is dropped (it is a quality knob). Anisotropy →
   `EXT_texture_filter_anisotropic`. `glGetTexImage` → keep a CPU copy only for
   textures the renderer reads back (flagged at upload from the 23 call sites),
   otherwise read through a temporary FBO.
9. **Render targets and readback (M).** Keep the back-buffer copy scheme at
   first (works in ES 3.0 for colour). Replace the two depth-reading paths: the
   shadow-map generator renders into an FBO with a depth texture and the
   receiver samples it with `sampler2DShadow`; the 1-pixel sun/flare depth
   probes become either a depth-texture FBO for the main scene or
   `GL_ANY_SAMPLES_PASSED` queries around a point draw. `NV_occlusion_query`
   entry points map to ES 3.0 queries; the engine-side users of pixel counts
   (`CREOclusionQuery`, flares) get a boolean-with-area fallback.
10. **Leftovers (S).** `glPolygonMode` wireframe → ignored; `glLineWidth`,
    `glPointSize` → ignored or point-sprite emulation later; display lists →
    only reachable through NV paths, stubbed; `glDrawBuffer/ReadBuffer` →
    back-buffer only; `glAccum` → dead (PBuffer); `glClipPlane` → uniform for
    the generated shader's discard.

## 5. Phases and milestones

Each phase ends with something visible and a comparison against `XRenderOGL`
on the same Mac.

- **Phase 0 – Scaffolding.** `XRenderGLES` target, `r_Driver=GLES`, ES 3.0
  context through ANGLE, function table wired to the shim with logging stubs,
  fixed feature report, clear to a colour. Milestone: the game boots to a
  cleared window with a log listing every entry point it tried to call.
- **Phase 1 – 2D and menus.** State tracker, matrix stacks, immediate mode,
  client arrays, texture upload for uncompressed and DXT, and the FFP generator
  for the single-stage MODULATE/REPLACE case. Milestone: main menu, fonts,
  console and loading screens render correctly.
- **Phase 2 – World.** ARB translator, program linking, env uploads, cube maps,
  3Dc. Milestone: a level renders with terrain, objects, vegetation, water and
  characters lit as on `XRenderOGL`, minus shadows and effects.
- **Phase 3 – Fixed-function completeness.** Full COMBINE set, texgen modes,
  fog, alpha test, clip plane, light 0. Milestone: sky, particles, decals,
  lightmapped `Layer` materials, HUD effects match the reference.
- **Phase 4 – Targets and probes.** FBO shadow maps + shadow samplers,
  cube-map and screen copies verified, sun/flare visibility, occlusion queries,
  screenshots, `glGetTexImage` users. Milestone: shadows, glare, flares and
  screen effects match; no logging-stub hits during a full level.
- **Phase 5 – Parity and performance.** A screenshot harness (fixed camera
  positions, `r_GetScreenShot`, image diff between the two modules, as openQ4's
  `render_shot.sh` does). Program-binary cache, dirty-state elimination,
  streaming VBO tuning. Milestone: diff within tolerance on a set of reference
  spots; frame time measured on a device.
- **Phase 6 – Shrink the shim.** Convert `EF_FlushHW`, the vertex-buffer path
  and `EF_SetColorOp` to call the layer directly; delete immediate-mode and
  matrix emulation where no caller remains. Optionally replace translated ARB
  programs with hand-written GLSL by family (about 30–40 families; see the
  shader-count discussion) for readability and mobile tuning. Milestone: no
  behaviour change, smaller and faster.
- **Phase 7 – Android.** Sigma Touch `:FarCry` module per
  `docs/porting/phase1.md` and `phase2.md`: CMake against the NDK, `LINUX32`
  type layer for armv7 (or arm64 only), module loading and data paths through
  SAFFAL, touch controls, precision fixes found on device, DXT/3Dc decode
  fallback on Mali. The Android edition's arm64 fixes are reviewed one hunk at
  a time as candidates, never merged wholesale.

## 6. Verification strategy

- The Mac runs both renderers on the same data. Every phase compares against
  `XRenderOGL`, first by eye, then with the screenshot harness.
- The shim logs every entry point that reaches a stub, per level; a clean log
  is a milestone gate.
- The ARB translator has a unit test that translates all 797 cache files and
  compiles them on ANGLE; any compile failure is a test failure.
- Precision and driver differences are checked on a real device from Phase 2
  onward, not left to Phase 7.

## 7. Risks

- **Shader cache coverage.** The cache is one playthrough's permutations. A
  missing permutation on the GL path needs Cg on x86 to regenerate. Mitigation:
  log misses (`MissingShaders.txt` already exists), regenerate offline; Phase 6's
  hand-written families remove the dependency entirely.
- **Rectangle textures in ARB programs** (46 pixel shaders): coordinate
  normalisation must be exact or screen effects smear. Covered by the size
  uniform and tested in Phase 4.
- **Copy-based render-to-texture cost on mobile.** Back-buffer copies are
  bandwidth-heavy on tile-based GPUs. Acceptable for parity; Phase 5/6 may move
  the frequent ones (screen effects, water reflections) to FBOs.
- **Depth-probe replacement changes behaviour** (sun visibility/flares). Keep
  the reference renderer's result as the oracle.
- **`-fpermissive` habits** keep surfacing under clang; each is fixed at source
  as in the macOS port, never with a blanket flag beyond the one already added.

## 8. Rough size

| Component | Size |
|---|---|
| Scaffolding, context, feature report | S |
| State tracker + legacy gets | M |
| Matrix stacks + GLU | S |
| Vertex input + immediate mode | M |
| FFP shader generator | L |
| ARB → GLSL ES translator + tests | M |
| Program linking + uniform upload | S |
| Textures and formats | M |
| Render targets, probes, queries | M |
| Screenshot harness | S |

Total is on the order of 6–9k new lines, most of it in the FFP generator and
the translator, against a 46k-line renderer that stays intact until Phase 6.

## 9. Status

- **Phase 0 done.** `XRenderGLES` module, `-RENDERER:GLES`, ES 3.0 context through ANGLE on macOS, stub logger.
- **Phase 1 done.** Menus and videos match the GL renderer.
- **Phase 2 done.** ARB vertex and fragment programs are translated to GLSL ES at
  `glProgramStringARB` time (`GLES/gles_arb_translate.cpp`) and paired with each
  other or with generated fixed-function halves (`GLES/gles_ffp.cpp`). The Pier
  level renders with terrain, vegetation, water, characters, weapon and HUD, and
  every program in the level translated, compiled and linked without error.
- **Development aids** (all environment variables, all builds):
  `FARCRY_GLES_DEBUG=1` logs entry-point resolution, translated and generated
  shader sources, uploads and GL errors to stderr; `FARCRY_SCREENSHOT_FRAME=n`
  / `FARCRY_SCREENSHOT_EVERY=n` with `FARCRY_SCREENSHOT_FILE=<name>` dump JPEGs
  from either renderer; `FARCRY_RESUME_FRAME=n` keeps the in-game menu off from
  frame n so `./FarCry -DEVMODE -RENDERER:GLES '"map pier"'` reaches gameplay
  unattended. The renderer's own `r_Log=3` (system.cfg) writes a full GL call
  trace to `OpenGLLog.txt` on either module.
- **Phase 3 done (unverified paths).** Fixed-function texgen, lighting and clip planes
  are generated; the Pier view does not exercise them, so they await a scene that
  does (vehicle windows use reflection texgen with GL lighting).
- **Phase 4 done.** Occlusion queries, `glGetTexImage`, `glReadPixels`
  conversions, and CPU S3TC compression with CPU mip chains (needed because the
  renderer asks the driver to compress and to mipmap, and the texture streamer
  reads the blocks back). The frame is drawn into a layer-owned scene
  framebuffer (`GLES/gles_fbo.cpp`: RGBA8 colour, DEPTH24_STENCIL8 texture)
  and blitted to the window from `GLES_SwapWindow`, which the renderer calls
  in place of `SDL_GL_SwapWindow`. That gives ES what the window cannot:
  `glReadPixels(GL_DEPTH_COMPONENT)` runs a full-screen pass that packs the
  depth texture into 24-bit RGBA for a small readable target (the flare and
  sun probes; `FARCRY_DEPTH_PROBE=1` prints three pixels on either renderer
  and they match GL to six decimals), `glCopyTexImage2D` of colour is native
  and of a depth format allocates a DEPTH24_STENCIL8 texture and blits into
  it. The shadow-map depth path also needs the SGIX extension strings; the
  reference GL renderer on macOS has neither, so they are advertised only
  with `FARCRY_GLES_DEPTHMAPS=1`, and that path has not yet met a scene that
  renders a shadow map. The whole Pier run reaches no stub entry point.
- **Phase 5 done.** `tools/gles-compare.sh <game dir> <map> [frame]` renders
  the frame on both renderers (`FARCRY_QUIT_FRAME=n` ends a run cleanly, so
  the layer's shutdown report of stub hits is in the log) and
  `tools/imgdiff.py` reports the mean absolute difference with a
  side-by-side image. Pier at frame 1300: 1.9 to 3.0 depending on the moving
  character, against 0 for identical frames.
- **Performance.** Pier view: 180 to 190 fps on GLES/ANGLE against 188 on the GL
  renderer, after streaming immediate-mode vertices through an unsynchronized
  ring VBO (client arrays cost ~100 us per draw on ANGLE), shadowing
  attribute, buffer, texture and env-upload state, and rendering through the
  scene framebuffer. `FARCRY_FPS_EVERY=n` prints the rate; `FARCRY_GLES_DEBUG`
  prints per-path timing every 300 frames.
- **Level sweep (2026-09-24).** Training, Fort, Carrier, Research, Boat and
  Dam were compared against the GL renderer with the harness. Texgen and GL
  lighting (Phase 3) are exercised by Fort, Carrier, Research, Boat and Dam
  and match; clip planes have not appeared yet. The sweep found and fixed:
  two-sided stencil for shadow volumes (`glActiveStencilFaceEXT` routed to
  the ES `*Separate` calls), cube maps that the renderer assembles one face
  per texture object (ES needs the other faces allocated to attach or
  sample a face), compressed blocks kept per cube face, and signed-byte
  bump-map uploads. Every sweep run ends with zero stub entry points hit.
  Frame rates on GLES against GL: Pier 180/188, Fort 168/207, Carrier
  482/379, Research 183/225, Training 383/267; Fort's gap is layer CPU time
  on ~725 immediate-mode draws and ~170 program switches per frame.
  All 17 multiplayer maps were then compared the same way: every run reaches
  a clean shutdown with zero stub entry points and no layer warnings, and the
  larger image differences (mp_jungle, mp_mangoriver, mp_dune, mp_radio) are
  spawn timing, not rendering. Playing mp_airstrip by hand found the one
  fault screenshots cannot show, the sky turning black on screen through the
  window's alpha channel (fixed at present time; see the sweep commit).
- **Next:** Phase 6 (batching immediate-mode draws and fewer program
  switches, shim clean-up) and Phase 7, the Android module.
