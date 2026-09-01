# PS2GL — Implemented & Working Functions

This document catalogues every `gl*` and `pgl*` function that is **concretely implemented** in the
`external/ps2gl` source tree.  
Functions that contain only `mNotImplemented()` or `mError()` as their body are **excluded** from
the implemented tables and listed in the **[Not Implemented](#not-implemented)** section.

> **Build context**: ps2gl targets the GS (Graphics Synthesizer) through VU1 microcode renderers
> and DMA chains. It is an OpenGL 1.1 subset. Not every standard GL call maps 1-to-1 to GS hardware.

---

## ⚠️ VIF1 DMA Frame Packet Budget (Critical — Shared by All Render Paths)

**Hard combined limit: 640 draw calls total per frame** (`GFX_DRAW_CALL_BUDGET` in `Constants.GFX.h`).

### Why

ps2gl allocates a fixed **65,000-qword** main DMA frame packet (`CGLContext::CurPacket`,
`kDmaPacketMaxQwordLength`). `CGLContext::GetVif1Packet()` always returns `*Vif1Packet` which is
initialized to `CurPacket` — so **every rendering path shares this packet**:

- `RenderPrimitives` — one `glCallList` per primitive
- `RenderModels` — one `glCallList` per model mesh
- `RenderSkybox` — one rlgl cube draw via `ImmGeomManager`
- UI / text / `DrawGrid` — rlgl batch flushes

Every `glCallList` that changes any `RendererContextChanged` flag (matrix, color, texture) writes a full
**VU1 renderer context re-upload** into `CurPacket`:

| Write                                                                                  | Source                         |  Qwords |
|:---------------------------------------------------------------------------------------|:-------------------------------|--------:|
| `AddVu1RendererContext` — 77-qword VU1 context (matrices, 8 lights, material, GIF tag) | `CLinearRenderer::InitContext` |      77 |
| DMA CNT tag + VIF codes (STCYCL, FLUSH, UNPACK header, MSCAL, FLUSHE, BASE, OFFSET)    | `InitContext`                  |      ~3 |
| DMA CALL tag to pre-compiled geometry packet + Pad128                                  | `CDrawArraysCmd::Play`         |      ~2 |
| **Total per `glCallList`**                                                             |                                | **~82** |

Safe maximum: `floor(65,000 / 82) = 792`. Fixed overhead from non-draw-call paths
(skybox `rlBegin/rlEnd` ~87 qwords, rlgl/text/UI ~200 qwords, setup ~50 qwords) totals
~400 qwords, leaving `floor(64,600 / 82) = 787` usable draw-call slots.
Budget constant `720` keeps a 45-slot (~3,720 qword) safety margin.

**Multi-mesh model optimisation**: `RenderModels` calls `glColor4f` once per model (not per mesh)
and does NOT reset color between meshes of the same model. After the first mesh of a model clears
the `CurMaterial` dirty flag, subsequent meshes of the **same model instance** only need to update
the texture (GS context) and re-emit the DMA CALL tag (~3–5 qwords instead of ~82).
This means a 4-mesh model costs roughly `82 + 3 + 3 + 3 = 91 qwords`, not `4 × 82 = 328 qwords`.

### In release builds — silent overflow, always fatal

`mErrorIf` (the overflow check inside `CDmaPacket::operator+=`) is **compiled out** when
`_DEBUG` is not defined. Overflow silently advances `pNext` past the end of the 1 MB buffer,
writing into adjacent heap memory. The first victim is usually a live C++ vtable or a ps2gl
state bitfield. Within a few fields the VIF1 unit receives a corrupted byte as a command
(e.g. `0x43`) → `Vif1: Unknown VifCmd! [43]` → subsequent TLB misses at garbage
addresses → EE jumps to `pc=0x0` → unrecoverable crash.

### The fix in the engine

`RenderPrimitives` and `RenderModels` in `RaylibRenderer.cpp` share the counter
`m_frameDrawCallsUsed` (reset at the start of `Render()`) and check it against
`GFX_DRAW_CALL_BUDGET` before every `glCallList`:

- If the combined budget is exhausted, excess primitives/models are **dropped** with
  `Engine_LogError` (loud failure, never a silent corrupt).
- `STRESSDRAW.LUA` defaults (`cube_count=400, sphere_count=220, cylinder_count=100`)
  total 720 — exactly at `GFX_DRAW_CALL_BUDGET`, exercising the full budget.

### Indexed model meshes (NOT supported)

ps2gl's `glDrawElements()` is a hard `mError()`. `pglDrawIndexedArrays()` only handles
unsigned-byte indices (< 256 vertices per mesh). Model meshes with `mesh.indices != nullptr`
are **skipped** by `RenderModels`, logged with `Engine_LogError`, and rendered as invisible.  
**Workaround**: export models with vertex sharing disabled (separate triangles), or generate
meshes programmatically with Raylib's `GenMesh*` functions (these produce unindexed geometry).

---

## Table of Contents

- [1. Library Initialization & Lifecycle](#1-library-initialization--lifecycle)
- [2. Synchronization & Buffer Swap](#2-synchronization--buffer-swap)
- [3. GS Memory — Slots](#3-gs-memory--slots)
- [4. GS Memory — Areas](#4-gs-memory--areas)
- [5. Display & Draw Buffer Configuration](#5-display--draw-buffer-configuration)
- [6. Normal Geometry Pipeline (Frame-Deferred DMA Chain)](#6-normal-geometry-pipeline-frame-deferred-dma-chain)
- [7. Immediate Geometry Pipeline (Synchronous)](#7-immediate-geometry-pipeline-synchronous)
- [8. Custom Renderers & Primitive Types](#8-custom-renderers--primitive-types)
- [9. pgl-Specific Enable / Disable](#9-pgl-specific-enable--disable)
- [10. GL State Enable / Disable](#10-gl-state-enable--disable)
- [11. GL State Query](#11-gl-state-query)
- [12. Matrix Stack](#12-matrix-stack)
- [13. Vertex Arrays](#13-vertex-arrays)
- [14. Immediate Mode (glBegin / glEnd)](#14-immediate-mode-glbegin--glend)
- [15. Lighting](#15-lighting)
- [16. Material](#16-material)
- [17. Textures](#17-textures)
- [18. Draw State](#18-draw-state)
- [19. Framebuffer Clear](#19-framebuffer-clear)
- [Not Implemented](#not-implemented)

---

## 1. Library Initialization & Lifecycle

| Function                                                       | Description                                                                                                                             | Evidence                                             |
|:---------------------------------------------------------------|:----------------------------------------------------------------------------------------------------------------------------------------|:-----------------------------------------------------|
| `pglInit(int immBufferVertexSize, int immDrawBufferQwordSize)` | Initializes the ps2gl library; allocates DMA packets and internal geometry buffers. Must be called before any `gl*` or `pgl*` function. | `From: external/ps2gl/src/glcontext.cpp @ 485:491` |
| `pglHasLibraryBeenInitted(void)`                               | Returns `1` if `pglInit()` has been called, `0` otherwise.                                                                              | `From: external/ps2gl/src/glcontext.cpp @ 497:500` |
| `pglFinish(void)`                                              | Cleans up ps2gl and calls `ps2sFinish()`; must be called when done.                                                                     | `From: external/ps2gl/src/glcontext.cpp @ 505:510` |

---

## 2. Synchronization & Buffer Swap

| Function                                            | Description                                                                                                                     | Evidence                                             |
|:----------------------------------------------------|:--------------------------------------------------------------------------------------------------------------------------------|:-----------------------------------------------------|
| `pglWaitForVU1(void)`                               | Polls COP0 until DMA transfers to VIF1 complete; does not stall the transfer.                                                   | `From: external/ps2gl/src/glcontext.cpp @ 519:522` |
| `pglWaitForVSync(void)`                             | Blocks until the vertical retrace semaphore is signaled; **required** for correct interlacing.                                  | `From: external/ps2gl/src/glcontext.cpp @ 528:531` |
| `pglSwapBuffers(void)`                              | Signals end of the rendering loop; swaps the double-buffered display/draw buffers.                                              | `From: external/ps2gl/src/glcontext.cpp @ 539:544` |
| `pglSetRenderingFinishedCallback(void (*cb)(void))` | Registers a callback invoked from the GS interrupt handler when rendering completes. ⚠️ Runs in interrupt context — be careful. | `From: external/ps2gl/src/glcontext.cpp @ 551:554` |

---

## 3. GS Memory — Slots

GS VRAM is partitioned into *slots* (page-aligned regions). Slots are registered once (usually at level load) and serve
as backing storage for memory areas.

| Function                                                                    | Description                                                                        | Evidence                                            |
|:----------------------------------------------------------------------------|:-----------------------------------------------------------------------------------|:----------------------------------------------------|
| `pglPrintGsMemAllocation(void)`                                             | Prints the current GS VRAM slot allocation map to `stdout`.                        | `From: external/ps2gl/src/gsmemory.cpp @ 40:44`   |
| `pglHasGsMemBeenInitted(void)`                                              | Returns `1` if at least one GS memory slot has been added.                         | `From: external/ps2gl/src/gsmemory.cpp @ 50:53`   |
| `pglAddGsMemSlot(int startingPage, int pageLength, unsigned int pixelMode)` | Registers a contiguous region of GS VRAM as a slot; returns a `pgl_slot_handle_t`. | `From: external/ps2gl/src/gsmemory.cpp @ 117:126` |
| `pglLockGsMemSlot(pgl_slot_handle_t slot)`                                  | Prevents the memory manager from automatically allocating or freeing this slot.    | `From: external/ps2gl/src/gsmemory.cpp @ 132:136` |
| `pglUnlockGsMemSlot(pgl_slot_handle_t slot)`                                | Re-enables automatic management for a previously locked slot.                      | `From: external/ps2gl/src/gsmemory.cpp @ 142:146` |
| `pglRemoveAllGsMemSlots(void)`                                              | Removes all registered GS memory slots.                                            | `From: external/ps2gl/src/gsmemory.cpp @ 151:155` |

---

## 4. GS Memory — Areas

*Areas* are logical allocations within slots (or manually addressed). They represent frame buffers, depth buffers, or
texture regions.

| Function                                                                 | Description                                                                                                    | Evidence                                            |
|:-------------------------------------------------------------------------|:---------------------------------------------------------------------------------------------------------------|:----------------------------------------------------|
| `pglCreateGsMemArea(int width, int height, unsigned int pix_format)`     | Creates a logical GS memory area object; does not allocate GS RAM yet.                                         | `From: external/ps2gl/src/gsmemory.cpp @ 184:189` |
| `pglDestroyGsMemArea(pgl_area_handle_t area)`                            | Destroys the area object and frees any associated memory.                                                      | `From: external/ps2gl/src/gsmemory.cpp @ 193:198` |
| `pglAllocGsMemArea(pgl_area_handle_t area)`                              | Binds the area to the best-fit free slot; uses LRU eviction if no free slot exists. Always succeeds or panics. | `From: external/ps2gl/src/gsmemory.cpp @ 210:214` |
| `pglFreeGsMemArea(pgl_area_handle_t area)`                               | Releases the slot bound to the area; does **not** free main RAM.                                               | `From: external/ps2gl/src/gsmemory.cpp @ 220:224` |
| `pglSetGsMemAreaWordAddr(pgl_area_handle_t area, unsigned int addr)`     | Manually sets the GS VRAM word address (byte address ÷ 4); for legacy compatibility.                           | `From: external/ps2gl/src/gsmemory.cpp @ 231:235` |
| `pglBindGsMemAreaToSlot(pgl_area_handle_t area, pgl_slot_handle_t slot)` | Manually binds an area to a specific slot, bypassing the LRU allocator.                                        | `From: external/ps2gl/src/gsmemory.cpp @ 241:246` |
| `pglUnbindGsMemArea(pgl_area_handle_t area)`                             | Releases the slot currently bound to the area.                                                                 | `From: external/ps2gl/src/gsmemory.cpp @ 251:255` |
| `pglLockGsMemArea(pgl_area_handle_t area)`                               | Prevents the memory manager from touching this area automatically.                                             | `From: external/ps2gl/src/gsmemory.cpp @ 261:265` |
| `pglUnlockGsMemArea(pgl_area_handle_t area)`                             | Re-enables memory manager management for a previously locked area.                                             | `From: external/ps2gl/src/gsmemory.cpp @ 269:273` |
| `pglGsMemAreaIsAllocated(pgl_area_handle_t area)`                        | Returns `1` if the area has an assigned GS VRAM address, `0` otherwise.                                        | `From: external/ps2gl/src/gsmemory.cpp @ 277:281` |
| `pglGetGsMemAreaWordAddr(pgl_area_handle_t area)`                        | Returns the current GS VRAM word address for this area.                                                        | `From: external/ps2gl/src/gsmemory.cpp @ 285:290` |

---

## 5. Display & Draw Buffer Configuration

| Function                                                                                                         | Description                                                                                                        | Evidence                                               |
|:-----------------------------------------------------------------------------------------------------------------|:-------------------------------------------------------------------------------------------------------------------|:-------------------------------------------------------|
| `pglSetDisplayBuffers(int interlaced, pgl_area_handle_t frame0, pgl_area_handle_t frame1)`                       | Configures the GS DISPLAY registers to read from the given frame area(s). `PGL_INTERLACED` or `PGL_NONINTERLACED`. | `From: external/ps2gl/include/GL/ps2gl.h @ 71:72`    |
| `pglSetDrawBuffers(int interlaced, pgl_area_handle_t frame0, pgl_area_handle_t frame1, pgl_area_handle_t depth)` | Sets the GS draw environment (frame + depth buffers) for all subsequent rendering.                                 | `From: external/ps2gl/src/drawcontext.cpp @ 926:934` |
| `pglSetInterlacingOffset(float yPixels)`                                                                         | Sets a sub-pixel vertical offset to correct field alignment in interlaced mode.                                    | `From: external/ps2gl/src/drawcontext.cpp @ 936:939` |

---

## 6. Normal Geometry Pipeline (Frame-Deferred DMA Chain)

This is the primary rendering path. Geometry commands are recorded into a DMA chain and dispatched one frame later for
GS rendering.

| Function                                             | Description                                                                   | Evidence                                             |
|:-----------------------------------------------------|:------------------------------------------------------------------------------|:-----------------------------------------------------|
| `pglBeginGeometry(void)`                             | Resets the current DMA packet to begin recording geometry for the new frame.  | `From: external/ps2gl/src/glcontext.cpp @ 581:584` |
| `pglEndGeometry(void)`                               | Finalizes the DMA packet and inserts a GS signal tag to notify completion.    | `From: external/ps2gl/src/glcontext.cpp @ 585:588` |
| `pglRenderGeometry(void)`                            | Dispatches the **previous** frame's DMA chain to VIF1/GS.                     | `From: external/ps2gl/src/glcontext.cpp @ 589:592` |
| `pglFinishRenderingGeometry(int forceImmediateStop)` | Waits on a semaphore until the GS signals completion of the dispatched frame. | `From: external/ps2gl/src/glcontext.cpp @ 593:596` |

---

## 7. Immediate Geometry Pipeline (Synchronous)

Used for geometry that must be rendered and completed before the next CPU instruction (e.g., debug overlays).

| Function                           | Description                                                        | Evidence                                             |
|:-----------------------------------|:-------------------------------------------------------------------|:-----------------------------------------------------|
| `pglBeginImmediateGeometry(void)`  | Flushes pending geometry and switches to the immediate DMA packet. | `From: external/ps2gl/src/glcontext.cpp @ 560:563` |
| `pglEndImmediateGeometry(void)`    | Finalizes the immediate packet and restores the normal packet.     | `From: external/ps2gl/src/glcontext.cpp @ 564:567` |
| `pglRenderImmediateGeometry(void)` | Sends the immediate packet to VIF1/GS.                             | `From: external/ps2gl/src/glcontext.cpp @ 568:571` |

> ⚠️ `pglFinishRenderingImmediateGeometry()` is declared but calls `mNotImplemented()` internally. Do not call it.  
> `From: external/ps2gl/src/glcontext.cpp @ 280:284`

---

## 8. Custom Renderers & Primitive Types

| Function                                                                             | Description                                                                                                                        | Evidence                                              |
|:-------------------------------------------------------------------------------------|:-----------------------------------------------------------------------------------------------------------------------------------|:------------------------------------------------------|
| `pglBeginRendererDefs(void)`                                                         | Opens a renderer definition block; call before registering custom renderers.                                                       | `From: external/ps2gl/include/GL/ps2gl.h @ 114:116` |
| `pglRegisterRenderer(void* renderer)`                                                | Registers a custom VU1 microcode renderer with the pipeline.                                                                       | `From: external/ps2gl/include/GL/ps2gl.h @ 115:115` |
| `pglEndRendererDefs(void)`                                                           | Closes the renderer definition block.                                                                                              | `From: external/ps2gl/include/GL/ps2gl.h @ 116:116` |
| `pglRegisterCustomPrimType(GLenum primType, pglU64_t req, pglU64_t mask, int merge)` | Registers a custom primitive type. ⚠️ Bit 31 of `primType` must be set. `merge` controls whether contiguous blocks can be batched. | `From: external/ps2gl/src/gmanager.cpp @ 488:493`   |
| `pglEnableCustom(pglU64_t flag)`                                                     | Sets upper-32-bit custom flags in the renderer requirements bitfield. Lower 32 bits are masked to zero.                            | `From: external/ps2gl/src/gmanager.cpp @ 501:505`   |
| `pglDisableCustom(pglU64_t flag)`                                                    | Clears upper-32-bit custom flags in the renderer requirements bitfield.                                                            | `From: external/ps2gl/src/gmanager.cpp @ 510:517`   |

---

## 9. pgl-Specific Enable / Disable

| Function                 | Description                                                                                                          | Evidence                                             |
|:-------------------------|:---------------------------------------------------------------------------------------------------------------------|:-----------------------------------------------------|
| `pglEnable(GLenum cap)`  | Enables a ps2gl-specific capability. ⚠️ Only `PGL_CLIPPING` is currently handled; any other cap triggers `mError()`. | `From: external/ps2gl/src/glcontext.cpp @ 602:611` |
| `pglDisable(GLenum cap)` | Disables a ps2gl-specific capability. ⚠️ Same restriction as `pglEnable`.                                            | `From: external/ps2gl/src/glcontext.cpp @ 613:622` |

---

## 10. GL State Enable / Disable

Only the following capabilities are handled. All others call `mNotImplemented()`.

**Supported caps**: `GL_LIGHT0`–`GL_LIGHT7`, `GL_LIGHTING`, `GL_BLEND`, `GL_COLOR_MATERIAL`,
`GL_RESCALE_NORMAL`, `GL_TEXTURE_2D`, `GL_NORMALIZE`, `GL_CULL_FACE`, `GL_ALPHA_TEST`, `GL_DEPTH_TEST`

| Function                | Description                                                                          | Evidence                                             |
|:------------------------|:-------------------------------------------------------------------------------------|:-----------------------------------------------------|
| `glEnable(GLenum cap)`  | Enables a GL rendering capability. ⚠️ Unsupported caps trigger `mNotImplemented()`.  | `From: external/ps2gl/src/glcontext.cpp @ 632:688` |
| `glDisable(GLenum cap)` | Disables a GL rendering capability. ⚠️ Unsupported caps trigger `mNotImplemented()`. | `From: external/ps2gl/src/glcontext.cpp @ 690:743` |

---

## 11. GL State Query

| Function                                     | Description                                                                                                                                         | Evidence                                             |
|:---------------------------------------------|:----------------------------------------------------------------------------------------------------------------------------------------------------|:-----------------------------------------------------|
| `glGetFloatv(GLenum pname, GLfloat* params)` | Retrieves a floating-point state value. ⚠️ Only `GL_MODELVIEW_MATRIX` and `GL_PROJECTION_MATRIX` are supported; others trigger `mNotImplemented()`. | `From: external/ps2gl/src/glcontext.cpp @ 752:767` |
| `glGetError(void)`                           | Returns `0` always. ⚠️ No real error tracking exists; emits `mWarn`. Do not rely on this for error detection.                                       | `From: external/ps2gl/src/glcontext.cpp @ 776:783` |

---

## 12. Matrix Stack

| Function                                                                                   | Description                                                                                                                                                                                                                                                                 | Evidence                                          |
|:-------------------------------------------------------------------------------------------|:----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|:--------------------------------------------------|
| `glMatrixMode(GLenum mode)`                                                                | Sets the active matrix stack. ⚠️ Only `GL_MODELVIEW` and `GL_PROJECTION` are supported; `GL_TEXTURE` triggers `mNotImplemented()`.                                                                                                                                          | `From: external/ps2gl/src/matrix.cpp @ 101:106` |
| `glLoadIdentity(void)`                                                                     | Replaces the top of the active stack with the identity matrix.                                                                                                                                                                                                              | `From: external/ps2gl/src/matrix.cpp @ 108:117` |
| `glPushMatrix(void)`                                                                       | Pushes a copy of the current top onto the active stack.                                                                                                                                                                                                                     | `From: external/ps2gl/src/matrix.cpp @ 119:125` |
| `glPopMatrix(void)`                                                                        | Pops the top of the active stack.                                                                                                                                                                                                                                           | `From: external/ps2gl/src/matrix.cpp @ 127:133` |
| `glLoadMatrixf(const GLfloat* m)`                                                          | Replaces the top of the stack with the given column-major 4×4 matrix and immediately computes its inverse.                                                                                                                                                                  | `From: external/ps2gl/src/matrix.cpp @ 138:154` |
| `glMultMatrixf(const GLfloat* m)`                                                          | Post-multiplies the current matrix by `m`.                                                                                                                                                                                                                                  | `From: external/ps2gl/src/matrix.cpp @ 284:319` |
| `glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)`                                | Concatenates a rotation matrix (angle in degrees) onto the active stack.                                                                                                                                                                                                    | `From: external/ps2gl/src/matrix.cpp @ 321:334` |
| `glScalef(GLfloat x, GLfloat y, GLfloat z)`                                                | Concatenates a scale matrix onto the active stack.                                                                                                                                                                                                                          | `From: external/ps2gl/src/matrix.cpp @ 336:346` |
| `glTranslatef(GLfloat x, GLfloat y, GLfloat z)`                                            | Concatenates a translation matrix onto the active stack.                                                                                                                                                                                                                    | `From: external/ps2gl/src/matrix.cpp @ 348:359` |
| `glFrustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble zNear, GLdouble zFar)` | Concatenates a perspective projection matrix. ⚠️ **Depth is inverted**: the PS2 GS does not support `GL_LESS`/`GL_LEQUAL`; ps2gl inverts the depth mapping so `near → maxDepth` and `far → 0`. Always pair with `glDepthFunc(GL_LEQUAL)` (maps to GS `kGEqual` internally). | `From: external/ps2gl/src/matrix.cpp @ 156:218` |
| `glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble zNear, GLdouble zFar)`   | Concatenates an orthographic projection matrix. ⚠️ Same depth inversion as `glFrustum`.                                                                                                                                                                                     | `From: external/ps2gl/src/matrix.cpp @ 220:282` |

---

## 13. Vertex Arrays

| Function                                                                                               | Description                                                                                                                                   | Evidence                                            |
|:-------------------------------------------------------------------------------------------------------|:----------------------------------------------------------------------------------------------------------------------------------------------|:----------------------------------------------------|
| `glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* ptr)`                          | Specifies the vertex data array. ⚠️ `type` must be `GL_FLOAT`; `stride` must be `0` — other values trigger `mNotImplemented()`.               | `From: external/ps2gl/src/gmanager.cpp @ 80:97`   |
| `glNormalPointer(GLenum type, GLsizei stride, const GLvoid* ptr)`                                      | Specifies the normal array (3 floats per normal by default). ⚠️ Same `type`/`stride` constraints as `glVertexPointer`.                        | `From: external/ps2gl/src/gmanager.cpp @ 104:110` |
| `glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* ptr)`                        | Specifies the texture-coordinate array. ⚠️ Same `type`/`stride` constraints.                                                                  | `From: external/ps2gl/src/gmanager.cpp @ 118:135` |
| `glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* ptr)`                           | Specifies the per-vertex color array. ⚠️ Same `type`/`stride` constraints.                                                                    | `From: external/ps2gl/src/gmanager.cpp @ 143:160` |
| `glDrawArrays(GLenum mode, GLint first, GLsizei count)`                                                | Draws primitives from the currently enabled arrays. ⚠️ Array data is **NOT copied**; the app must **double-buffer geometry** when it changes. | `From: external/ps2gl/src/gmanager.cpp @ 173:179` |
| `glEnableClientState(GLenum cap)`                                                                      | Activates a vertex array stream. ⚠️ `GL_INDEX_ARRAY` and `GL_EDGE_FLAG_ARRAY` trigger `mNotImplemented()`.                                    | `From: external/ps2gl/src/gmanager.cpp @ 224:250` |
| `glDisableClientState(GLenum cap)`                                                                     | Deactivates a vertex array stream. ⚠️ Same restrictions as `glEnableClientState`.                                                             | `From: external/ps2gl/src/gmanager.cpp @ 252:278` |
| `glFlush(void)`                                                                                        | Flushes the internal geometry buffers.                                                                                                        | `From: external/ps2gl/src/gmanager.cpp @ 214:220` |
| `pglNormalPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* ptr)`                         | Extended `glNormalPointer`; allows 3- or 4-element normals. ⚠️ Same `type`/`stride` constraints.                                              | `From: external/ps2gl/src/gmanager.cpp @ 431:446` |
| `pglDrawIndexedArrays(GLenum primType, int numIndices, const unsigned char* indices, int numVertices)` | Draws indexed geometry using unsigned-byte indices, without requiring `glDrawElements`.                                                       | `From: external/ps2gl/src/gmanager.cpp @ 448:453` |

---

## 14. Immediate Mode (glBegin / glEnd)

| Function                                                 | Description                                                                                                      | Evidence                                            |
|:---------------------------------------------------------|:-----------------------------------------------------------------------------------------------------------------|:----------------------------------------------------|
| `glBegin(GLenum mode)`                                   | Starts a primitive specification block.                                                                          | `From: external/ps2gl/src/gmanager.cpp @ 280:286` |
| `glEnd(void)`                                            | Ends a primitive specification block.                                                                            | `From: external/ps2gl/src/gmanager.cpp @ 409:415` |
| `glVertex2f(GLfloat x, GLfloat y)`                       | Specifies a 2D vertex (`z=0, w=1`).                                                                              | `From: external/ps2gl/src/gmanager.cpp @ 332:337` |
| `glVertex2i(GLint x, GLint y)`                           | Specifies a 2D integer vertex.                                                                                   | `From: external/ps2gl/src/gmanager.cpp @ 339:344` |
| `glVertex2fv(const GLfloat* v)`                          | Specifies a 2D vertex from an array.                                                                             | `From: external/ps2gl/src/gmanager.cpp @ 346:351` |
| `glVertex3f(GLfloat x, GLfloat y, GLfloat z)`            | Specifies a 3D vertex (`w=1`).                                                                                   | `From: external/ps2gl/src/gmanager.cpp @ 318:323` |
| `glVertex3fv(const GLfloat* v)`                          | Specifies a 3D vertex from an array.                                                                             | `From: external/ps2gl/src/gmanager.cpp @ 325:330` |
| `glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)` | Specifies a full 4D homogeneous vertex.                                                                          | `From: external/ps2gl/src/gmanager.cpp @ 303:309` |
| `glVertex4fv(const GLfloat* v)`                          | Specifies a 4D vertex from an array.                                                                             | `From: external/ps2gl/src/gmanager.cpp @ 311:316` |
| `glNormal3f(GLfloat x, GLfloat y, GLfloat z)`            | Sets the current normal vector.                                                                                  | `From: external/ps2gl/src/gmanager.cpp @ 288:294` |
| `glNormal3fv(const GLfloat* v)`                          | Sets the current normal from an array.                                                                           | `From: external/ps2gl/src/gmanager.cpp @ 296:301` |
| `glTexCoord2f(GLfloat u, GLfloat v)`                     | Sets the current texture coordinate.                                                                             | `From: external/ps2gl/src/gmanager.cpp @ 353:359` |
| `glTexCoord2fv(const GLfloat* v)`                        | Sets the current texture coordinate from an array.                                                               | `From: external/ps2gl/src/gmanager.cpp @ 361:366` |
| `glColor3f(GLfloat r, GLfloat g, GLfloat b)`             | Sets the current RGB color (`alpha=1.0`).                                                                        | `From: external/ps2gl/src/gmanager.cpp @ 368:374` |
| `glColor3fv(const GLfloat* v)`                           | Sets the current RGB color from an array.                                                                        | `From: external/ps2gl/src/gmanager.cpp @ 376:381` |
| `glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)`  | Sets the current RGBA color.                                                                                     | `From: external/ps2gl/src/gmanager.cpp @ 383:389` |
| `glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a)` | Sets the current RGBA color from unsigned bytes; converts to float. Added specifically for raylib compatibility. | `From: external/ps2gl/src/gmanager.cpp @ 392:400` |
| `glColor4fv(const GLfloat* v)`                           | Sets the current RGBA color from an array.                                                                       | `From: external/ps2gl/src/gmanager.cpp @ 402:407` |

---

## 15. Lighting

| Function                                                       | Description                                                                                                                                                                                                                                                                    | Evidence                                            |
|:---------------------------------------------------------------|:-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|:----------------------------------------------------|
| `glLightfv(GLenum light, GLenum pname, const GLfloat* params)` | Sets a vector light property. Supported `pname` values: `GL_AMBIENT`, `GL_DIFFUSE`, `GL_SPECULAR`, `GL_POSITION`, `GL_SPOT_DIRECTION`, `GL_SPOT_EXPONENT`, `GL_SPOT_CUTOFF`, `GL_CONSTANT_ATTENUATION`, `GL_LINEAR_ATTENUATION`, `GL_QUADRATIC_ATTENUATION`.                   | `From: external/ps2gl/src/lighting.cpp @ 342:385` |
| `glLightf(GLenum light, GLenum pname, GLfloat param)`          | Sets a scalar light property. Supported: `GL_SPOT_EXPONENT`, `GL_SPOT_CUTOFF`, attenuation factors.                                                                                                                                                                            | `From: external/ps2gl/src/lighting.cpp @ 387:413` |
| `glLightModelfv(GLenum pname, const GLfloat* params)`          | Sets a global light model parameter. ⚠️ Only `GL_LIGHT_MODEL_AMBIENT` is fully implemented. `GL_LIGHT_MODEL_COLOR_CONTROL` (`GL_SEPARATE_SPECULAR_COLOR`), `GL_LIGHT_MODEL_LOCAL_VIEWER` (non-zero), and `GL_LIGHT_MODEL_TWO_SIDE` (non-zero) all trigger `mNotImplemented()`. | `From: external/ps2gl/src/lighting.cpp @ 415:444` |

---

## 16. Material

| Function                                                         | Description                                                                                                                                                                                                                                                  | Evidence                                            |
|:-----------------------------------------------------------------|:-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|:----------------------------------------------------|
| `glMaterialfv(GLenum face, GLenum pname, const GLfloat* params)` | Sets a vector material property. ⚠️ Only `GL_FRONT` face is supported; `GL_BACK` and `GL_FRONT_AND_BACK` trigger `mNotImplemented()`. Supported `pname`: `GL_AMBIENT`, `GL_DIFFUSE`, `GL_SPECULAR`, `GL_EMISSION`, `GL_SHININESS`, `GL_AMBIENT_AND_DIFFUSE`. | `From: external/ps2gl/src/material.cpp @ 196:236` |
| `glMaterialf(GLenum face, GLenum pname, GLfloat param)`          | Sets a scalar material property. ⚠️ Only `GL_FRONT` + `GL_SHININESS` is supported.                                                                                                                                                                           | `From: external/ps2gl/src/material.cpp @ 238:260` |
| `glColorMaterial(GLenum face, GLenum mode)`                      | Configures which material properties are driven by `glColor*` when `GL_COLOR_MATERIAL` is enabled.                                                                                                                                                           | `From: external/ps2gl/src/material.cpp @ 262:267` |

---

## 17. Textures

### GL Texture Management

| Function                                                                                                                                               | Description                                                                                                                                                                                                                                                                                                                                                                                                                                                               | Evidence                                           |
|:-------------------------------------------------------------------------------------------------------------------------------------------------------|:--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|:---------------------------------------------------|
| `glGenTextures(GLsizei n, GLuint* textures)`                                                                                                           | Allocates `n` texture name handles from the internal table.                                                                                                                                                                                                                                                                                                                                                                                                               | `From: external/ps2gl/src/texture.cpp @ 559:565` |
| `glBindTexture(GLenum target, GLuint texture)`                                                                                                         | Binds a texture name to the texture unit. ⚠️ Only `GL_TEXTURE_2D` is a valid target; any other value triggers `mError()`.                                                                                                                                                                                                                                                                                                                                                 | `From: external/ps2gl/src/texture.cpp @ 567:575` |
| `glDeleteTextures(GLsizei n, const GLuint* textures)`                                                                                                  | Deletes named textures and frees their GS VRAM allocation.                                                                                                                                                                                                                                                                                                                                                                                                                | `From: external/ps2gl/src/texture.cpp @ 577:583` |
| `glTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const GLvoid* pixels)` | Uploads a 2D texture image. ⚠️ `level > 0` (mipmaps) silently returns. ⚠️ `border > 0` silently returns. ⚠️ `pixels` must be **16-byte aligned** in main RAM; misalignment triggers `mNotImplemented()`. Supported format/type combos: `GL_RGBA`+`GL_UNSIGNED_BYTE` (32 bpp), `GL_RGBA`+`GL_UNSIGNED_INT_8_8_8_8` (32 bpp), `GL_RGBA`+`GL_UNSIGNED_SHORT_5_5_5_1` (16 bpp), `GL_RGB`+`GL_UNSIGNED_BYTE` (24 bpp), `GL_COLOR_INDEX`+`GL_UNSIGNED_BYTE` (8 bpp palettized). | `From: external/ps2gl/src/texture.cpp @ 585:648` |
| `glColorTable(GLenum target, GLenum internalFormat, GLsizei width, GLenum format, GLenum type, const GLvoid* table)`                                   | Sets the color lookup table for palettized textures. ⚠️ `table` must be **16-byte aligned**; `width` must be `16` or `256`; `format` must be `GL_RGB` or `GL_RGBA`.                                                                                                                                                                                                                                                                                                       | `From: external/ps2gl/src/texture.cpp @ 650:671` |
| `glTexParameteri(GLenum target, GLenum pname, GLint param)`                                                                                            | Sets a texture parameter. ⚠️ Only `GL_TEXTURE_2D` target. Supported `pname`: `GL_TEXTURE_MIN_FILTER`, `GL_TEXTURE_MAG_FILTER`, `GL_TEXTURE_WRAP_S`, `GL_TEXTURE_WRAP_T`. `GL_TEXTURE_WRAP_R` triggers `mNotImplemented()`. `GL_TEXTURE_MIN_LOD`, `GL_TEXTURE_MAX_LOD`, `GL_TEXTURE_BASE_LEVEL`, `GL_TEXTURE_MAX_LEVEL`, `GL_TEXTURE_PRIORITY`, `GL_TEXTURE_BORDER_COLOR` all trigger `mNotImplemented()`.                                                                 | `From: external/ps2gl/src/texture.cpp @ 673:683` |
| `glTexParameterf(GLenum target, GLenum pname, GLfloat param)`                                                                                          | Float variant of `glTexParameteri`; delegates to it.                                                                                                                                                                                                                                                                                                                                                                                                                      | `From: external/ps2gl/src/texture.cpp @ 685:690` |
| `glTexParameteriv(GLenum target, GLenum pname, GLint* param)`                                                                                          | Array-int variant of `glTexParameteri`.                                                                                                                                                                                                                                                                                                                                                                                                                                   | `From: external/ps2gl/src/texture.cpp @ 692:697` |
| `glTexParameterfv(GLenum target, GLenum pname, const GLfloat* param)`                                                                                  | Array-float variant of `glTexParameteri`.                                                                                                                                                                                                                                                                                                                                                                                                                                 | `From: external/ps2gl/src/texture.cpp @ 699:704` |
| `glTexEnvi(GLenum target, GLenum pname, GLint param)`                                                                                                  | Sets the texture environment mode. ⚠️ `GL_MODULATE` and `GL_REPLACE` work correctly. ⚠️ `GL_DECAL` is treated as `GL_REPLACE` and emits `mWarn`. ⚠️ `GL_BLEND` triggers `mNotImplemented()`.                                                                                                                                                                                                                                                                              | `From: external/ps2gl/src/texture.cpp @ 706:725` |
| `glTexEnvf(GLenum target, GLenum pname, GLfloat param)`                                                                                                | Float variant of `glTexEnvi`.                                                                                                                                                                                                                                                                                                                                                                                                                                             | `From: external/ps2gl/src/texture.cpp @ 727:732` |
| `glTexEnvfv(GLenum target, GLenum pname, GLfloat* param)`                                                                                              | Float-array variant of `glTexEnvi`.                                                                                                                                                                                                                                                                                                                                                                                                                                       | `From: external/ps2gl/src/texture.cpp @ 734:739` |
| `glTexEnviv(GLenum target, GLenum pname, GLint* param)`                                                                                                | Int-array variant of `glTexEnvi`.                                                                                                                                                                                                                                                                                                                                                                                                                                         | `From: external/ps2gl/src/texture.cpp @ 741:746` |

### pgl Texture Helpers

| Function                                                     | Description                                                                                                             | Evidence                                           |
|:-------------------------------------------------------------|:------------------------------------------------------------------------------------------------------------------------|:---------------------------------------------------|
| `pglTextureFromGsMemArea(pgl_area_handle_t area)`            | Uses an existing GS memory area directly as the current texture; no upload occurs — only GS register settings are sent. | `From: external/ps2gl/src/texture.cpp @ 814:819` |
| `pglBindTextureToSlot(GLuint texId, pgl_slot_handle_t slot)` | Manually binds a named texture to a specific GS memory slot, bypassing the LRU allocator.                               | `From: external/ps2gl/src/texture.cpp @ 802:807` |
| `pglFreeTexture(GLuint texId)`                               | Frees the GS VRAM held by the named texture without freeing main RAM.                                                   | `From: external/ps2gl/src/texture.cpp @ 790:795` |

---

## 18. Draw State

| Function                                                          | Description                                                                                                                                                                                                                                                                          | Evidence                                               |
|:------------------------------------------------------------------|:-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|:-------------------------------------------------------|
| `glDepthFunc(GLenum func)`                                        | Sets the depth comparison function. ⚠️ Only `GL_NEVER`, `GL_LESS`, `GL_LEQUAL`, `GL_ALWAYS` are supported. `GL_LESS` maps to GS `kGreater` and `GL_LEQUAL` maps to GS `kGEqual` due to **depth inversion**. `GL_EQUAL`, `GL_GREATER`, `GL_NOTEQUAL`, `GL_GEQUAL` trigger `mError()`. | `From: external/ps2gl/src/drawcontext.cpp @ 775:780` |
| `glBlendFunc(GLenum sfactor, GLenum dfactor)`                     | Sets alpha blending mode. ⚠️ Only 3 combinations supported: `(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)`, `(GL_SRC_ALPHA, GL_ONE)`, `(GL_ONE_MINUS_SRC_ALPHA, GL_ONE)`. All others trigger `mNotImplemented()`.                                                                          | `From: external/ps2gl/src/drawcontext.cpp @ 805:810` |
| `glAlphaFunc(GLenum func, GLclampf ref)`                          | Sets the alpha test function and reference value. All 8 GL comparison modes (`GL_NEVER`, `GL_LESS`, `GL_EQUAL`, `GL_LEQUAL`, `GL_GREATER`, `GL_NOTEQUAL`, `GL_GEQUAL`, `GL_ALWAYS`) are supported.                                                                                   | `From: external/ps2gl/src/drawcontext.cpp @ 814:819` |
| `glDepthMask(GLboolean enabled)`                                  | Enables or disables depth buffer writes.                                                                                                                                                                                                                                             | `From: external/ps2gl/src/drawcontext.cpp @ 823:829` |
| `glShadeModel(GLenum mode)`                                       | Sets `GL_FLAT` or `GL_SMOOTH` shading.                                                                                                                                                                                                                                               | `From: external/ps2gl/src/drawcontext.cpp @ 831:837` |
| `glCullFace(GLenum mode)`                                         | Sets which face is culled (`GL_FRONT` or `GL_BACK`). ⚠️ `GL_FRONT_AND_BACK` emits `mWarn` but does not abort; behaviour is undefined.                                                                                                                                                | `From: external/ps2gl/src/drawcontext.cpp @ 839:846` |
| `glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)` | Controls which RGBA channels are written to the framebuffer.                                                                                                                                                                                                                         | `From: external/ps2gl/src/drawcontext.cpp @ 848:864` |
| `glPolygonMode(GLenum face, GLenum mode)`                         | Sets polygon fill mode (`GL_FILL` or `GL_LINE`).                                                                                                                                                                                                                                     | `From: external/ps2gl/src/drawcontext.cpp @ 887:893` |

---

## 19. Framebuffer Clear

| Function                                                       | Description                                                                             | Evidence                                        |
|:---------------------------------------------------------------|:----------------------------------------------------------------------------------------|:------------------------------------------------|
| `glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a)` | Sets the RGBA clear color. Values are clamped to `[0, 1]`.                              | `From: external/ps2gl/src/clear.cpp @ 71:85`  |
| `glClearDepth(GLclampd depth)`                                 | Sets the depth value used when clearing the depth buffer.                               | `From: external/ps2gl/src/clear.cpp @ 87:94`  |
| `glClear(GLbitfield mask)`                                     | Clears the specified buffers. Supports `GL_COLOR_BUFFER_BIT` and `GL_DEPTH_BUFFER_BIT`. | `From: external/ps2gl/src/clear.cpp @ 96:101` |

---

## Not Implemented

These functions exist in the source tree but their bodies call `mNotImplemented()` or `mError()`, making them **hard
failures at runtime**. The "Why" column explains the hardware or architectural reason.

| Function                                            | Why it cannot be implemented                                                                                                                                                       |
|:----------------------------------------------------|:-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `glScissor()`                                       | The GS clipping region is embedded in the draw environment; an independent scissor rectangle is not a separate GS register. `From: external/ps2gl/src/drawcontext.cpp @ 866:871` |
| `glTexSubImage2D()`                                 | GS texture DMA is a contiguous block transfer; partial sub-region updates are not supported. `From: external/ps2gl/src/texture.cpp @ 748:756`                                    |
| `glCopyTexImage2D()`                                | No framebuffer-to-texture readback path exists in ps2gl. `From: external/ps2gl/src/texture.cpp @ 758:766`                                                                        |
| `glCopyTexSubImage2D()`                             | Same reason as `glCopyTexImage2D`. `From: external/ps2gl/src/texture.cpp @ 768:775`                                                                                              |
| `glFogi()` / `glFogf()` / `glFogfv()` / `glFogiv()` | The GS does not implement a programmable per-fragment fog equation. `From: external/ps2gl/src/lighting.cpp @ 460:486`                                                            |
| `glGetString()`                                     | No vendor/renderer/version string registry is maintained in ps2gl. `From: external/ps2gl/src/glcontext.cpp @ 785:791`                                                            |
| `glGetIntegerv()`                                   | Integer state queries are not implemented. `From: external/ps2gl/src/glcontext.cpp @ 769:774`                                                                                    |
| `glGetLightfv()`                                    | Light property readback is not implemented. `From: external/ps2gl/src/lighting.cpp @ 453:458`                                                                                    |
| `glHint()`                                          | The GS driver has no hint mechanism. `From: external/ps2gl/src/glcontext.cpp @ 745:750`                                                                                          |
| `glLightModeli()`                                   | Integer variant of `glLightModelfv`; not implemented. `From: external/ps2gl/src/lighting.cpp @ 446:451`                                                                          |
| `glDrawElements()`                                  | Explicitly marked as a placeholder; **hard errors** (`mError()`) if called. `From: external/ps2gl/src/gmanager.cpp @ 184:189`                                                    |
| `glInterleavedArrays()`                             | Explicitly marked as a placeholder; **hard errors** if called. `From: external/ps2gl/src/gmanager.cpp @ 194:199`                                                                 |
| `glArrayElement()`                                  | Explicitly marked as a placeholder; **hard errors** if called. `From: external/ps2gl/src/gmanager.cpp @ 204:209`                                                                 |
| `glClearAccum()`                                    | The GS does not have an accumulation buffer. `From: external/ps2gl/src/drawcontext.cpp @ 873:878`                                                                                |
| `glClearStencil()`                                  | The GS does not have a stencil buffer. `From: external/ps2gl/src/drawcontext.cpp @ 880:885`                                                                                      |
| `glPolygonOffset()`                                 | No polygon offset hardware exists in the GS. `From: external/ps2gl/src/drawcontext.cpp @ 895:900`                                                                                |
| `glDrawBuffer()`                                    | GS draw buffers are configured via `pglSetDrawBuffers`; GL's draw-buffer selection is not supported. `From: external/ps2gl/src/drawcontext.cpp @ 782:787`                        |
| `glReadBuffer()`                                    | No framebuffer readback path exists. `From: external/ps2gl/src/drawcontext.cpp @ 789:794`                                                                                        |
| `glClipPlane()`                                     | User-defined clip planes are not implemented in any VU1 renderer. `From: external/ps2gl/src/drawcontext.cpp @ 796:801`                                                           |
| `glMatrixMode(GL_TEXTURE)`                          | Texture matrix mode is not supported; triggers `mNotImplemented()`. `From: external/ps2gl/src/glcontext.cpp @ 238:241`                                                           |
| `glTexEnvi(…, GL_BLEND)`                            | Texture env `GL_BLEND` mode is not implemented. `From: external/ps2gl/src/texture.cpp @ 721:724`                                                                                 |
| `pglFinishRenderingImmediateGeometry()`             | The implementation calls `mNotImplemented()`. `From: external/ps2gl/src/glcontext.cpp @ 280:284`                                                                                 |

