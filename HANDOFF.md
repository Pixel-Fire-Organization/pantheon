# Handoff: finish porting `random-fixes` into `add-material-system`

Untracked note for the next session; delete it when the work is committed. Read
`CLAUDE.md` and `.github/copilot-instructions.md` first, as always.

## Where things stand

- Branch `add-material-system`. `origin/random-fixes` was squash-merged into the
  working tree (`git merge --squash`); **nothing is committed**. The user has not
  asked for a commit yet.
- All merge conflicts are resolved. Python tests: 384 pass; the only 2 failures
  are `tools/tests/test_run_target.py` and they fail identically on HEAD.
- Builds: PS2PAL debug built successfully after the round-2 changes, but **not
  since the last edit** (GifTag wrap fix, below). Win32 was built only after
  round 1, **not after round 2** (StagedGeometry changed since).
- Temporary test hacks (boot into `MainScene`; fog off in `EngineMain.cpp`) have
  been **reverted**. Confirm with `git diff HEAD -- game/src/Game.cpp`: it
  should show nothing beyond what `random-fixes` itself changed.

## What round 2 changed (the user asked to "fix the open issues")

1. **Atlas clamp region removed.** Region repeat couldn't work because ps2gl
   shares the PS2 cook. `BakedMeshEntry` is back to HEAD's 48-byte v3 layout,
   and PSEC is version 2 again. Instead, `compile_level.py` splits atlased
   triangles along integer UV lines (`_clip_soup_to_uv_tiles`), shifts each
   piece into [0, 1], and insets it half a texel inside its cell
   (`ATLAS_TEXEL_INSET`). Each cell's atlased materials merge into one mesh. A
   mesh isn't atlased if clipping would grow it by more than 1.5×
   (`ATLAS_MAX_TRI_GROWTH`). Cells are no longer powers of two.
2. **LOD1 edges.** `mesh.split_long_edges` runs after decimation (`bake_mesh`
   takes `max_edge`). `test_triangle_edges_within_max_edge` passes now.
3. **LOD1 slot-size check.** `LEVEL_LOD1_SECTOR_MAX_BYTES_BY_PLATFORM` makes an
   oversized LOD1 sector fail the cook, and a test mirrors it against the
   headers. An oversized LOD0 sector is a hard error again, as the spec says.
4. **PS2 LOD1 arena is now 4 MB with 24 slots of 160 KB** (was 8 MB / 64). The
   8 MB arena left ps2gl unable to start (EE out of memory, then fallback to
   null). The edge split pushed one LOD1 sector to 135 KB, so 128 KB slots were
   too small.
5. **VISI format** now carries a per-cell byte-offset table
   (`EngineLevelFormat.h`, `levelfmt.pack_visi`/`parse_visi`, and
   `Internal_VisiIsSane` in `EngineLevel.cpp`). The streamer looks up its cell
   directly, and the LOD1 load pass only runs after a recentre
   (`s_Lod1Pending`).
6. **LOD1 drawn by every backend.** A new `Engine_Sector_Lod1Opacity` helper in
   `EngineSector` decides it. Giftag fades by distance. `StagedGeometry` (all
   non-PS2 backends) and Ps2Gl use a hard cutover: LOD1 is drawn until that
   cell's LOD0 sector is fully resolvable. Full-detail sectors are skipped until
   resolvable on every backend.
7. **Named constants.** `LEVEL_LOD1_FADE_START_CELLS`,
   `LEVEL_LOD1_FADE_WIDTH_CELLS` and `LEVEL_LOD1_LOADS_PER_FRAME` are in all five
   platform headers. `GFX_GIFTAG_FOG_NEAR`/`FAR` are in the PS2 header, used via
   the `GsFogDepth` helper in `GifTag.cpp`. Compiler constants (`ATLAS_*`,
   `LOD1_DECIMATE_GRID`, `VISI_RADIUS_CELLS`) sit at the top of
   `compile_level.py`.
8. **512-wide framebuffer.** The user's reason is more texture VRAM. Giftag
   already sized from `GFX_SCREEN_WIDTH`, and its heap is now 888 KB. ps2gl was
   hard-coded to 640 and is now fixed:
   - `InitGsMemory` sizes frame, depth and texture slots from the width and adds
     a 256×256 and a 128×128 slot. `GFX_GS_TEXTURE_PAGE_BUDGET` goes from 264 to
     304.
   - `ApplyDisplayWidth()` rewrites GS `DISPLAY2` (0x120000A0) with ×5
     magnification after `pglSwapBuffers`, because ps2stuff hard-codes ×4.
   - Checked against the GS manual; PCSX2 shows full width.
9. **Last edit (not built yet): restored `draw_texture_wrapping(WRAP_REPEAT)` in
   `GifTagRenderer::BindTexture`.** `random-fixes` had removed it in favour of its
   CLAMP writes; with those gone, no wrap mode was set, and giftag drew the tiled
   floor as a flat colour. ps2gl was fine.

## To do

1. Rebuild and verify: `py -3 tools/build.py debug --platforms PS2PAL`, then
   `--platforms WIN32`. Build any other toolchains available too (Vita, PSP, nx),
   since `StagedGeometry`, `EngineSector` and every platform header changed.
2. **Emulator, carefully.** The user's PC went down during emulator runs. Run one
   capture at a time, never `--keep-open`, check for leftover `pcsx2-qt`
   processes, and ask before running it. Command:
   `PCSX2_PATH=/c/PCSX2/pcsx2-qt.exe py -3 tools/ps2/emu_capture.py dist/ps2pal/engine.iso --renderer giftag --native-shot --settle 40 --shot <png> --log <log>`.
   `--press` does not work here: the run ends immediately. To reach the level,
   temporarily change `GameInit` in `game/src/Game.cpp` to start
   `MainScene::GetInstance()` (include `../include/scenes/MainScene.h`) and
   revert afterwards. Check that giftag's floor is now textured, like ps2gl's.
3. Update the specs for round 2, in the same change (project rule):
   - `docs/formats/LEVEL_FORMAT.md`:
     - drop the clamp-region paragraph and the v4/v3 mentions (entry is BKM2 v3,
       PSEC v2)
     - describe tile clipping, the inset, the growth cap and one mesh per atlas
     - add the VISI offset table
     - add LOD1 slot sizes per platform
     - remove the fixed "known limitations" (max-edge, unchecked LOD1 size, clamp
       on giftag only)
     - fix the VISI radius: the code uses 12 cells, the doc says 8
   - `docs/subsystems/SECTOR.md`: failed LOD1 reads now retry on the next
     recentre, not the next frame; the load pass only runs after a recentre;
     remove the "walks the chunk every frame" limit; mention the fade constants
     and the opacity rule (fade vs cutover).
   - `docs/ps2/renderers/GIFTAG.md`: remove the CLAMP region-repeat bullet;
     REPEAT wrap is set per bind; fog uses the named constants; the fade comes
     from the shared helper.
   - `docs/ps2/renderers/PS2GL.md`: draws LOD1 with a hard cutover; VRAM layout
     follows the width; `DISPLAY2` override and why; texture budget 304 pages.
   - `docs/ps2/PLATFORM.md`: LOD1 arena 4 MB / 24 / 160 KB; the 512 width and
     its rationale (giftag heap about 888 KB, ps2gl +40 pages); remove "only
     giftag draws the LOD1 tier"; refresh the memory/texture-budget text.
   - `docs/subsystems/RENDERER.md`, if it describes level drawing: staged
     backends draw LOD1 with a cutover.
   - `.github/copilot-instructions.md`: `ARENA_LEVEL_LOD1` is 4 MB, 24 slots on
     PS2; the "Remaining" line should now say LOD1 is drawn everywhere, with the
     fade on giftag only.
4. Re-run `py -3 -m pytest -q tools/tests -p no:cacheprovider`.
5. Report to the user. Commit only if asked (attribution trailer per the system
   reminder).

## Observations to pass on (not fixed)

- ps2gl's image ends about 30 lines above the bottom (black band). The DISPLAY2
  override keeps ps2stuff's DY/DH, so this is probably pre-existing but was not
  compared against HEAD.
- Giftag lights the level noticeably darker than ps2gl.
- Giftag's texture heap has only 40 KB left on `mainAssetsTest`.
- Giftag's fog colour is the clear colour (0.6 grey), so distant geometry and
  the sky go grey.
- Measured cook for `mainAssetsTest` (PS2): LOD1 totals about 65% of LOD0 bytes.
  The guard-band edge limit caps how far decimation can shrink LOD1.
- Unchanged from round 1: `uint8_t` sector generation (wraps after 256
  evictions); PS2 framebuffer 512 wide by the user's choice (VRAM).

## Handy facts

- PS2 cook output: `dist/cooked/ps2pal/levels/*.PS2R`; list sector sizes with
  `pack_archive.read_toc`.
- Hardware books: `../console-hardware-docs/ps2/*.pdf`; `pdftotext` is on PATH
  under Git Bash.
- Use `py -3` for pytest; the Git Bash `python3` has no pytest.
- Bash heredocs that contain apostrophes break in this harness; write scripts
  with the Write tool. Never emit a literal NUL (`'\0'`) through shell-generated
  Python.
