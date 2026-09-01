#include "platform/ps2/renderer/Ps2Gl.h"

#include <cmath>
#include <cstring>
#include <ctime>
#include <malloc.h>

#include <GL/gl.h>
#include <GL/ps2gl.h>

#include "core/EngineDebug.h"
#include "core/EngineMemory.h"
#include "level/EngineLevel.h"
#include "level/EngineSector.h"
#include "resources/EngineResource.h"
#include "graphics/DrawList.h"
#include "graphics/PrimitiveGeometry.h"
#include "Macros.h"

// SetGsCrt is a PS2 BIOS syscall (libkernel). Forward-declared to avoid pulling
// the full <kernel.h> into this translation unit.
extern "C" int SetGsCrt(short int interlace, short int display_mode, short int field);

namespace
{
    constexpr float GL_RENDERER_DEG_TO_RAD = 0.017453292519943295f;

    // GS texture-modulate treats a colour value of 128, not 255, as "no
    // change" against the sampled texel; passing 255 straight through
    // doubles the sampled brightness (see docs/ps2/renderers/GIFTAG.md).
    // Untextured draws use the full range, since there colour is the result
    // rather than a multiplier.
    constexpr float GL_TEX_MODULATE_UNITY = 128.0f / 255.0f;

    // GS pixel storage modes (raw hardware codes; match ps2stuff GS::tPSM values).
    constexpr unsigned int GS_PSMCT32 = 0;
    constexpr unsigned int GS_PSMCT24 = 1;
    constexpr unsigned int GS_PSMZ24 = 49;
    constexpr unsigned int GS_PSMT8 = 19;

    // GIF control register — OSDSYS leaves PATH3 busy; writing 1 resets the GIF so
    // our PATH1/PATH2 transfers are not ignored. (See ps2gl glut/raylib init.)
    volatile uint32_t* const GIF_CTRL = reinterpret_cast<volatile uint32_t*>(0x10003000);

    // SetGsCrt display-mode values (ps2sdk GRAPH_MODE_*).
    constexpr short GS_MODE_NTSC = 2;
    constexpr short GS_MODE_PAL = 3;

    // Vertex count of the display list compiled for each primitive shape (used for
    // the throughput stats — the DList handle itself carries no size).
    uint32_t VertexCountForType(Primitive3D type)
    {
        switch (type)
        {
        case Primitive3D::Sphere:
            return PRIMITIVE_SPHERE_VERTEX_COUNT;
        case Primitive3D::Cylinder:
            return PRIMITIVE_CYLINDER_VERTEX_COUNT;
        default:
            return PRIMITIVE_CUBE_VERTEX_COUNT;
        }
    }
} // namespace

static void AddPrimitive(DrawLists& lists, Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, Color3 color, int32_t textureId)
{
    PrimitiveDrawEntry entry;
    entry.transform = Transform3D(position, rotation, scale);
    entry.color = color;
    entry.type = primitive;
    entry.textureId = textureId;
    lists.AddPrimitive(entry);
}

// ---------------------------------------------------------------------------
// GS memory initialisation (replaces raylib's initGsMemoryForRaylib).
// Frame + depth buffers, display/draw buffer binding, and a bank of texture
// slots whose total page count matches GFX_GS_TEXTURE_PAGE_BUDGET.
// ---------------------------------------------------------------------------
void Ps2GlRenderer::InitGsMemory(bool pal)
{
    pgl_slot_handle_t frame_slot_0, frame_slot_1, depth_slot;
    if (pal)
    {
        frame_slot_0 = pglAddGsMemSlot(0, 80, GS_PSMCT32);
        frame_slot_1 = pglAddGsMemSlot(80, 80, GS_PSMCT32);
        depth_slot = pglAddGsMemSlot(160, 80, GS_PSMZ24);
    }
    else
    {
        frame_slot_0 = pglAddGsMemSlot(0, 70, GS_PSMCT32);
        frame_slot_1 = pglAddGsMemSlot(70, 70, GS_PSMCT32);
        depth_slot = pglAddGsMemSlot(140, 70, GS_PSMZ24);
    }

    pglLockGsMemSlot(frame_slot_0);
    pglLockGsMemSlot(frame_slot_1);
    pglLockGsMemSlot(depth_slot);

    pgl_area_handle_t frame_area_0, frame_area_1, depth_area;
    if (pal)
    {
        frame_area_0 = pglCreateGsMemArea(640, 256, GS_PSMCT24);
        frame_area_1 = pglCreateGsMemArea(640, 256, GS_PSMCT24);
        depth_area = pglCreateGsMemArea(640, 256, GS_PSMZ24);
    }
    else
    {
        frame_area_0 = pglCreateGsMemArea(640, 224, GS_PSMCT24);
        frame_area_1 = pglCreateGsMemArea(640, 224, GS_PSMCT24);
        depth_area = pglCreateGsMemArea(640, 224, GS_PSMZ24);
    }

    pglBindGsMemAreaToSlot(frame_area_0, frame_slot_0);
    pglBindGsMemAreaToSlot(frame_area_1, frame_slot_1);
    pglBindGsMemAreaToSlot(depth_area, depth_slot);

    pglSetDrawBuffers(PGL_INTERLACED, frame_area_0, frame_area_1, depth_area);
    pglSetDisplayBuffers(PGL_INTERLACED, frame_area_0, frame_area_1);

    // Texture VRAM slots. Layout mirrors the reference ps2gl setup so the total
    // resident texture pages match GFX_GS_TEXTURE_PAGE_BUDGET (see PlatformConstants.h).
    if (pal)
    {
        pglAddGsMemSlot(240, 2, GS_PSMT8);
        for (int p = 242; p <= 249; ++p) // 8 * 64x32
            pglAddGsMemSlot(p, 1, GS_PSMCT32);
        for (int p = 250; p <= 264; p += 2) // 8 * 64x64
            pglAddGsMemSlot(p, 2, GS_PSMCT32);
        for (int p = 266; p <= 306; p += 8) // 6 * 128x128
            pglAddGsMemSlot(p, 8, GS_PSMCT32);
        pglAddGsMemSlot(314, 32, GS_PSMCT32); // 2 * 256x256
        pglAddGsMemSlot(346, 32, GS_PSMCT32);
        pglAddGsMemSlot(378, 64, GS_PSMCT32); // 2 * 512x256
        pglAddGsMemSlot(442, 64, GS_PSMCT32);
    }
    else
    {
        pglAddGsMemSlot(210, 2, GS_PSMT8);
        for (int p = 212; p <= 219; ++p) // 8 * 64x32
            pglAddGsMemSlot(p, 1, GS_PSMCT32);
        for (int p = 220; p <= 234; p += 2) // 8 * 64x64
            pglAddGsMemSlot(p, 2, GS_PSMCT32);
        for (int p = 236; p <= 276; p += 8) // 6 * 128x128
            pglAddGsMemSlot(p, 8, GS_PSMCT32);
        pglAddGsMemSlot(284, 32, GS_PSMCT32); // 2 * 256x256
        pglAddGsMemSlot(316, 32, GS_PSMCT32);
        pglAddGsMemSlot(348, 64, GS_PSMCT32); // 2 * 512x256
        pglAddGsMemSlot(412, 64, GS_PSMCT32);
    }
}

Ps2GlRenderer::Ps2GlRenderer(const EngineConfig& config)
{
    UNUSED_VAR(config);
    Engine_LogInfo("Ps2GlRenderer: Initializing ps2gl renderer (%dx%d %s)", GFX_SCREEN_WIDTH, GFX_SCREEN_HEIGHT, GFX_SCREEN_REGION_STR);

    const bool pal = (GFX_SCREEN_HEIGHT == GFX_SCREEN_PAL_HEIGHT);

    if (!pglHasLibraryBeenInitted())
    {
        // Reset the GIF (OSDSYS leaves PATH3 busy) and set the CRTC video mode
        // BEFORE pglInit, exactly as the ps2gl reference init does.
        *GIF_CTRL = 1;
        SetGsCrt(1 /* interlaced */, pal ? GS_MODE_PAL : GS_MODE_NTSC, 1 /* frame */);

        // immBufferVertexSize bounds glBegin/glEnd geometry per frame (skybox +
        // queued 2D rects only — the scene uses display lists / vertex arrays).
        const int immBufferVertexSize = 64 * 1024;
        if (pglInit(immBufferVertexSize, 1000) == 0)
        {
            Engine_LogError("Ps2GlRenderer: pglInit failed.");
            return;
        }
    }

    if (!pglHasGsMemBeenInitted())
        InitGsMemory(pal);

    glViewport(0, 0, GFX_SCREEN_WIDTH, GFX_SCREEN_HEIGHT);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    // ps2gl inverts depth in glFrustum/glOrtho (near→maxdepth); GL_LEQUAL maps to
    // the GS kGEqual comparison, which is the correct test for that inversion.
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);

    // Separated primitive geometry lives in the renderer arena; DrawLists fills
    // it, then we compile ps2gl display lists from it (GL context is ready now).
    m_megaBatch = static_cast<float*>(Engine_GetSlot(ARENA_RENDERER, 0));
    if (!m_megaBatch)
    {
        Engine_Panic("Ps2GlRenderer: Failed to retrieve ARENA_RENDERER slot 0!");
    }

    const size_t cubeBytes = PRIMITIVE_CUBE_VERTEX_COUNT * (3 + 3 + 2) * sizeof(float);
    const size_t sphereBytes = PRIMITIVE_SPHERE_VERTEX_COUNT * (3 + 3 + 2) * sizeof(float);
    const size_t cylBytes = PRIMITIVE_CYLINDER_VERTEX_COUNT * (3 + 3 + 2) * sizeof(float);
    const size_t totalBytes = cubeBytes + sphereBytes + cylBytes;

    Engine_LoadToSlot(ARENA_RENDERER, 0, nullptr, totalBytes);
    Engine_LogInfo("Ps2GlRenderer: Separated geometry arrays: %zu bytes in ARENA_RENDERER", totalBytes);

    m_drawLists.Init(m_megaBatch);
    CompilePrimitiveDLists();

    m_initialized = true;
}

void Ps2GlRenderer::Shutdown()
{
    ClearModelDListCache();
    if (m_dlCube)
        glDeleteLists(m_dlCube, 1);
    if (m_dlSphere)
        glDeleteLists(m_dlSphere, 1);
    if (m_dlCylinder)
        glDeleteLists(m_dlCylinder, 1);
    m_dlCube = m_dlSphere = m_dlCylinder = 0;
    m_drawLists.Shutdown();
    pglFinish();
    m_initialized = false;
}

RendererType Ps2GlRenderer::GetRendererType() const { return RendererType::Ps2Gl; }

// ---------------------------------------------------------------------------
// Primitive display-list compilation (moved here from DrawLists — ps2gl-only).
// ---------------------------------------------------------------------------
void Ps2GlRenderer::CompilePrimitiveDLists()
{
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    struct
    {
        Primitive3D type;
        unsigned int* dl;
    } prims[] = {
        {Primitive3D::Cube, &m_dlCube},
        {Primitive3D::Sphere, &m_dlSphere},
        {Primitive3D::Cylinder, &m_dlCylinder},
    };

    for (auto& p : prims)
    {
        const PrimitiveArrays arr = m_drawLists.GetPrimitiveArrays(p.type);
        glVertexPointer(3, GL_FLOAT, 0, arr.verts);
        glNormalPointer(GL_FLOAT, 0, arr.norms);
        glTexCoordPointer(2, GL_FLOAT, 0, arr.uvs);

        *p.dl = glGenLists(1);
        glNewList(*p.dl, GL_COMPILE);
        glDrawArrays(GL_TRIANGLES, 0, arr.vertexCount);
        glEndList();
    }

    Engine_LogInfo("Ps2GlRenderer: Primitive DLists compiled (cube=%u, sphere=%u, cyl=%u)", m_dlCube, m_dlSphere, m_dlCylinder);
}

unsigned int Ps2GlRenderer::GetListForType(Primitive3D type) const
{
    switch (type)
    {
    case Primitive3D::Sphere:
        return m_dlSphere;
    case Primitive3D::Cylinder:
        return m_dlCylinder;
    default:
        return m_dlCube;
    }
}

void Ps2GlRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale)
{
    AddPrimitive(m_drawLists, primitive, position, rotation, scale, {1.0f, 1.0f, 1.0f}, -1);
}

void Ps2GlRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, Color3 color)
{
    AddPrimitive(m_drawLists, primitive, position, rotation, scale, color, -1);
}

void Ps2GlRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, int32_t textureId)
{
    AddPrimitive(m_drawLists, primitive, position, rotation, scale, {1.0f, 1.0f, 1.0f}, textureId);
}

void Ps2GlRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, Color3 color, int32_t textureId)
{
    AddPrimitive(m_drawLists, primitive, position, rotation, scale, color, textureId);
}

void Ps2GlRenderer::AddLevelToDrawList(const Level& level) { UNUSED_VAR(level); }

void Ps2GlRenderer::AddModelToDrawList(int32_t modelId, const Vector3& position, const Vector3& rotation, const Vector3& scale)
{
    ModelDrawEntry entry;
    entry.resourceId = modelId;
    entry.transform = Transform3D(position, rotation, scale);
    m_drawLists.AddModel(entry);
}

void Ps2GlRenderer::AddSkyToDrawList(int32_t resourceId) { m_drawLists.SetSkyboxTexture(resourceId); }

void Ps2GlRenderer::ClearDrawLists() { m_drawLists.Reset(false); }

// ---------------------------------------------------------------------------
// Frame lifecycle. BeginFrame opens the geometry block and applies the deferred
// clear; EndFrame flushes 2D rects, dispatches the DMA chain, and swaps buffers.
// ---------------------------------------------------------------------------
void Ps2GlRenderer::BeginFrame()
{
    pglBeginGeometry();
    m_inFrame = true;
    // Apply the clear color chosen during the scripting phase (or the previous
    // frame's, if the script did not call graphics.clear()).
    glClearColor(m_clearColor.r, m_clearColor.g, m_clearColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Ps2GlRenderer::EndFrame()
{
    FlushQuads2D();

    // Order matches the ps2glut reference loop. ps2gl double-buffers the DMA
    // packet: pglRenderGeometry() sends LastPacket, and pglSwapBuffers() is what
    // moves the packet we just recorded (CurPacket) into LastPacket — so Render
    // must come AFTER Swap. pglFinishRenderingGeometry() waits on the previous
    // frame's completion signal, so it is skipped on the very first frame (nothing
    // has been dispatched yet) to avoid blocking forever.
    pglEndGeometry();
    const double waitStart = static_cast<double>(clock()) / CLOCKS_PER_SEC;
    if (!m_firstFrame)
        pglFinishRenderingGeometry(PGL_DONT_FORCE_IMMEDIATE_STOP);
    m_firstFrame = false;
    pglWaitForVSync();
    m_frameStats.presentWaitMs = static_cast<float>((static_cast<double>(clock()) / CLOCKS_PER_SEC - waitStart) * 1000.0);
    pglSwapBuffers();
    pglRenderGeometry();

    m_drawLists.SetLastStats(m_frameStats);
    m_inFrame = false;
}


void Ps2GlRenderer::ClearFrame(const Color3& color)
{
    // ps2gl records glClear into the active geometry packet, so it is only valid
    // between BeginFrame() and EndFrame(). During the scripting phase (m_inFrame
    // false) we merely stash the color; BeginFrame() applies it. The panic loop
    // calls ClearFrame() while in-frame, so clear immediately there.
    m_clearColor = color;
    if (m_inFrame)
    {
        glClearColor(color.r, color.g, color.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
}

void Ps2GlRenderer::DrawQuad2D(const Quad2D& quad)
{
    if (m_quad2DCount >= GL_MAX_2D_QUADS)
    {
        ++m_droppedQuads2D;
        return;
    }
    m_quads2D[m_quad2DCount++] = quad;
}

void Ps2GlRenderer::FlushQuads2D()
{
    if (m_droppedQuads2D > 0)
    {
        Engine_LogError("Ps2GlRenderer: dropped %u 2D quad(s) this frame (queue capacity %u).", m_droppedQuads2D, GL_MAX_2D_QUADS);
        m_droppedQuads2D = 0;
    }

    if (m_quad2DCount == 0)
        return;

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // Screen-space quads have a fixed winding that is back-facing under the
    // Y-flipped ortho below; with GL_CULL_FACE left enabled (from Init) every rect
    // would be culled and nothing would show. Disable culling for the 2D pass
    // (mirrors RenderSkybox, which does the same for its full-screen quad).
    glDisable(GL_CULL_FACE);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, static_cast<double>(GFX_SCREEN_WIDTH), static_cast<double>(GFX_SCREEN_HEIGHT), 0.0, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    uint32_t boundTexture = 0;
    bool texturing = false;
    for (uint16_t i = 0; i < m_quad2DCount; ++i)
    {
        const Quad2D& q = m_quads2D[i];
        const float x0 = static_cast<float>(q.x);
        const float y0 = static_cast<float>(q.y);
        const float x1 = static_cast<float>(q.x + q.w);
        const float y1 = static_cast<float>(q.y + q.h);

        if (q.texture != boundTexture)
        {
            boundTexture = q.texture;
            if (boundTexture)
            {
                if (!texturing)
                {
                    glEnable(GL_TEXTURE_2D);
                    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                    texturing = true;
                }
                glBindTexture(GL_TEXTURE_2D, boundTexture);
            }
            else if (texturing)
            {
                glDisable(GL_TEXTURE_2D);
                texturing = false;
            }
        }

        const float quadColorScale = texturing ? GL_TEX_MODULATE_UNITY : 1.0f;
        glColor4f(static_cast<float>(q.r) / 255.0f * quadColorScale, static_cast<float>(q.g) / 255.0f * quadColorScale, static_cast<float>(q.b) / 255.0f * quadColorScale, static_cast<float>(q.a) / 255.0f);
        glBegin(GL_QUADS);
        if (texturing)
        {
            const float u0 = static_cast<float>(q.u0) / 65535.0f;
            const float v0 = static_cast<float>(q.v0) / 65535.0f;
            const float u1 = static_cast<float>(q.u1) / 65535.0f;
            const float v1 = static_cast<float>(q.v1) / 65535.0f;
            glTexCoord2f(u0, v0);
            glVertex2f(x0, y0);
            glTexCoord2f(u1, v0);
            glVertex2f(x1, y0);
            glTexCoord2f(u1, v1);
            glVertex2f(x1, y1);
            glTexCoord2f(u0, v1);
            glVertex2f(x0, y1);
        }
        else
        {
            glVertex2f(x0, y0);
            glVertex2f(x1, y0);
            glVertex2f(x1, y1);
            glVertex2f(x0, y1);
        }
        glEnd();
    }

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);

    m_quad2DCount = 0;
}

void Ps2GlRenderer::DrawGrid(int32_t slices, float spacing)
{
    UNUSED_VAR(slices);
    UNUSED_VAR(spacing);
}

void Ps2GlRenderer::Render()
{
    m_frameDrawCallsUsed = 0;
    m_frameStats = DrawStats{};

    m_drawLists.SortForSubmission();

    const Camera3D& camera = m_drawLists.GetCamera3D();

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    ApplyProjection(camera);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    ApplyCameraTransform(camera);

    // Rebuild the CPU cull frustum from the same projection/view fed to GL.
    {
        float proj[16], view[16], vp[16];
        const float aspect = GFX_DISPLAY_ASPECT;
        Frustum_BuildPerspective(proj, camera.fovy, aspect, GFX_NEAR_PLANE, GFX_FAR_PLANE);
        Frustum_BuildLookAt(view, camera);
        Frustum_Mult4x4(vp, proj, view);
        Frustum_FromViewProj(&m_frustum, vp);
    }

    RenderSkybox(m_drawLists);
    RenderPrimitives(m_drawLists);
    RenderModels(m_drawLists);
    RenderLevel();

    m_drawLists.Reset(false);
}

// Draw the resident level sectors. Each sector's geometry is a set of Mesh views
// pointing straight into an arena slot; we frustum-cull whole sectors by their
// world AABB, then draw each mesh immediately (no display-list cache for streamed
// sectors). Textures resolve from the level's pinned material handles at draw time.
void Ps2GlRenderer::RenderLevel()
{
    if (!Engine_Level_Current())
        return;

    uint32_t count = 0;
    const SectorResident* residents = Engine_Sector_GetResidents(&count);

    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    // Compiled level faces are not guaranteed to be wound for GL's front-face
    // convention after the map->engine coordinate transform, so draw both sides
    // (no back-face cull) — like the skybox and the 2D pass.
    glDisable(GL_CULL_FACE);
    int32_t lastTexResId = -2;

    uint32_t renderable = 0, visibleSectors = 0, drawnMeshes = 0;

    for (uint32_t s = 0; s < count; ++s)
    {
        const SectorResident& sec = residents[s];
        if (sec.state != SECTOR_READY || sec.meshCount == 0)
            continue;
        ++renderable;
        if (!Frustum_AabbVisible(&m_frustum, sec.bounds))
        {
            ++m_frameStats.entriesCulled;
            continue;
        }
        ++visibleSectors;

        for (uint32_t m = 0; m < sec.meshCount; ++m)
        {
            if (m_frameDrawCallsUsed >= GFX_DRAW_CALL_BUDGET)
                break;
            const Mesh& mesh = sec.meshes[m];
            if (!mesh.vertices || mesh.vertexCount == 0)
                continue;
            ++drawnMeshes;

            const int32_t texResId = sec.meshTexture[m];
            if (texResId != lastTexResId)
            {
                const auto* tex = (texResId >= 0) ? static_cast<const Texture2D*>(Engine_Resource_Get(texResId)) : nullptr;
                if (tex && tex->id != 0)
                {
                    glEnable(GL_TEXTURE_2D);
                    glBindTexture(GL_TEXTURE_2D, tex->id);
                    ++m_frameStats.texBinds;
                    lastTexResId = texResId;
                }
                else
                {
                    glDisable(GL_TEXTURE_2D);
                    lastTexResId = -2;
                }
            }

            glEnableClientState(GL_VERTEX_ARRAY);
            glEnableClientState(GL_NORMAL_ARRAY);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glVertexPointer((mesh.vertexComponents == 4) ? 4 : 3, GL_FLOAT, 0, mesh.vertices);
            if (mesh.normals)
                glNormalPointer(GL_FLOAT, 0, mesh.normals);
            if (mesh.texcoords)
                glTexCoordPointer(2, GL_FLOAT, 0, mesh.texcoords);

            const GLenum mode = (mesh.topology == MESH_TOPOLOGY_STRIP) ? GL_TRIANGLE_STRIP : GL_TRIANGLES;
            glDrawArrays(mode, 0, mesh.vertexCount);
            ++m_frameDrawCallsUsed;

            const uint32_t verts = static_cast<uint32_t>(mesh.vertexCount);
            m_frameStats.trisSubmitted += (mesh.topology == MESH_TOPOLOGY_STRIP) ? (verts >= 2 ? verts - 2 : 0) : verts / 3;
            m_frameStats.vertsTransformed += verts;
        }
    }
    glEnable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_2D);

    // One-shot diagnostic: how many sectors were renderable / visible / drawn.
    static bool s_LoggedLevelStats = false;
    if (!s_LoggedLevelStats)
    {
        s_LoggedLevelStats = true;
        Engine_LogInfo("RenderLevel: residents=%u renderable=%u visible=%u drawnMeshes=%u", count, renderable, visibleSectors, drawnMeshes);
    }
}

void Ps2GlRenderer::RenderSkybox(const DrawLists& lists)
{
    if (lists.GetSkyboxResourceId() == -1)
        return;

    const auto* tex = static_cast<const Texture2D*>(Engine_Resource_Get(lists.GetSkyboxResourceId()));
    if (!tex || tex->id == 0)
        return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex->id);
    ++m_frameStats.texBinds;
    m_frameStats.trisSubmitted += PRIMITIVE_CUBE_VERTEX_COUNT / 3;
    m_frameStats.vertsTransformed += PRIMITIVE_CUBE_VERTEX_COUNT;

    const Camera3D& camera = lists.GetCamera3D();
    glPushMatrix();
    glTranslatef(camera.position.x, camera.position.y, camera.position.z);
    glScalef(500.0f, 500.0f, 500.0f);

    glBegin(GL_TRIANGLES);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    for (uint32_t i = 0; i < PRIMITIVE_CUBE_VERTEX_COUNT; ++i)
    {
        const float* v = MODEL_CUBE + i * PRIMITIVE_VERTEX_STRIDE;
        glTexCoord2f(v[6], v[7]);
        glNormal3f(v[3], v[4], v[5]);
        glVertex3f(v[0], v[1], v[2]);
    }
    glEnd();

    glPopMatrix();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_2D);
}

void Ps2GlRenderer::RenderPrimitives(DrawLists& lists)
{
    const uint16_t uCount = lists.GetUntexturedCount();
    const uint16_t tCount = lists.GetTexturedCount();

    if (uCount == 0 && tCount == 0)
        return;

    const uint16_t totalPrims = uCount + tCount;
    const uint16_t budgetLeft = (GFX_DRAW_CALL_BUDGET > m_frameDrawCallsUsed) ? static_cast<uint16_t>(GFX_DRAW_CALL_BUDGET - m_frameDrawCallsUsed) : 0u;

    if (totalPrims > budgetLeft)
    {
        Engine_LogError("RenderPrimitives: CurPacket budget exceeded (%u used + %u prims > %u).", m_frameDrawCallsUsed, totalPrims, static_cast<uint16_t>(GFX_DRAW_CALL_BUDGET));
    }

    const uint16_t uRender = (uCount <= budgetLeft) ? uCount : budgetLeft;
    const uint16_t tBudget = (uRender < budgetLeft) ? static_cast<uint16_t>(budgetLeft - uRender) : 0u;
    const uint16_t tRender = (tCount <= tBudget) ? tCount : tBudget;

    if (uRender > 0)
    {
        glDisable(GL_TEXTURE_2D);
        const PrimitiveDrawEntry* prims = lists.GetUntexturedPrims();

        for (uint16_t i = 0; i < uRender; ++i)
        {
            const auto& entry = prims[i];
            const Vector3 pos = entry.transform.GetPosition();
            const Vector3 rot = entry.transform.GetRotation();
            const Vector3 scl = entry.transform.GetScale();

            Vector3 wc;
            float wr;
            Frustum_WorldSphere(pos, scl, Vector3{0.0f, 0.0f, 0.0f}, lists.GetPrimitiveBaseRadius(entry.type), &wc, &wr);
            if (!Frustum_SphereVisible(&m_frustum, wc, wr))
            {
                ++m_frameStats.entriesCulled;
                continue;
            }

            glColor4f(entry.color.r, entry.color.g, entry.color.b, 1.0f);

            glPushMatrix();
            glTranslatef(pos.x, pos.y, pos.z);
            if (rot.x != 0.0f)
                glRotatef(rot.x, 1.0f, 0.0f, 0.0f);
            if (rot.y != 0.0f)
                glRotatef(rot.y, 0.0f, 1.0f, 0.0f);
            if (rot.z != 0.0f)
                glRotatef(rot.z, 0.0f, 0.0f, 1.0f);
            glScalef(scl.x, scl.y, scl.z);
            glCallList(GetListForType(entry.type));
            glPopMatrix();
            ++m_frameDrawCallsUsed;
            ++m_frameStats.primitiveCount;

            const uint32_t verts = VertexCountForType(entry.type);
            m_frameStats.trisSubmitted += verts / 3;
            m_frameStats.vertsTransformed += verts;
        }
    }

    if (tRender > 0)
    {
        int32_t lastTexId = -2;
        const PrimitiveDrawEntry* prims = lists.GetTexturedPrims();

        for (uint16_t i = 0; i < tRender; ++i)
        {
            const auto& entry = prims[i];

            const Vector3 pos = entry.transform.GetPosition();
            const Vector3 rot = entry.transform.GetRotation();
            const Vector3 scl = entry.transform.GetScale();

            Vector3 wc;
            float wr;
            Frustum_WorldSphere(pos, scl, Vector3{0.0f, 0.0f, 0.0f}, lists.GetPrimitiveBaseRadius(entry.type), &wc, &wr);
            if (!Frustum_SphereVisible(&m_frustum, wc, wr))
            {
                ++m_frameStats.entriesCulled;
                continue;
            }

            if (entry.textureId != lastTexId)
            {
                const auto* tex = static_cast<const Texture2D*>(Engine_Resource_Get(entry.textureId));
                if (tex && tex->id != 0)
                {
                    glEnable(GL_TEXTURE_2D);
                    glBindTexture(GL_TEXTURE_2D, tex->id);
                    ++m_frameStats.texBinds;
                }
                else
                {
                    Engine_LogError("Tried to access texture ID (%i), but it wasn't initialized!", entry.textureId);
                    glDisable(GL_TEXTURE_2D);
                }
                lastTexId = entry.textureId;
            }

            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

            glPushMatrix();
            glTranslatef(pos.x, pos.y, pos.z);
            if (rot.x != 0.0f)
                glRotatef(rot.x, 1.0f, 0.0f, 0.0f);
            if (rot.y != 0.0f)
                glRotatef(rot.y, 0.0f, 1.0f, 0.0f);
            if (rot.z != 0.0f)
                glRotatef(rot.z, 0.0f, 0.0f, 1.0f);
            glScalef(scl.x, scl.y, scl.z);
            glCallList(GetListForType(entry.type));
            glPopMatrix();
            ++m_frameDrawCallsUsed;
            ++m_frameStats.primitiveCount;

            const uint32_t verts = VertexCountForType(entry.type);
            m_frameStats.trisSubmitted += verts / 3;
            m_frameStats.vertsTransformed += verts;
        }
    }
}

void Ps2GlRenderer::RenderModels(const DrawLists& lists)
{
    const uint16_t mCount = lists.GetModelCount();
    if (mCount == 0)
        return;

    const ModelDrawEntry* entries = lists.GetModels();
    uint16_t modelMeshDraws = 0;

    glDisable(GL_TEXTURE_2D);
    // Skip redundant glBindTexture calls across meshes/models. Only a successful
    // bind is remembered; failures force a re-resolve on the next mesh.
    int32_t lastTexResId = -2;

    for (uint16_t i = 0; i < mCount; ++i)
    {
        if (m_frameDrawCallsUsed >= GFX_DRAW_CALL_BUDGET)
        {
            Engine_LogError("RenderModels: CurPacket budget exhausted at model %u of %u.", static_cast<unsigned>(i), static_cast<unsigned>(mCount));
            break;
        }

        const ModelDrawEntry& entry = entries[i];
        const auto* model = static_cast<const Model*>(Engine_Resource_Get(entry.resourceId));
        if (!model)
        {
            Engine_LogError("RenderModels: resource %d not ready, skipping.", entry.resourceId);
            continue;
        }

        // Frustum cull against the model's merged bounding sphere.
        {
            Vector3 wc;
            float wr;
            Frustum_WorldSphere(entry.transform.GetPosition(), entry.transform.GetScale(), model->boundsCenter, model->boundsRadius, &wc, &wr);
            if (!Frustum_SphereVisible(&m_frustum, wc, wr))
            {
                ++m_frameStats.entriesCulled;
                continue;
            }
        }

        ModelDListEntry* dl = FindOrCompileModelDLists(model, entry.resourceId);
        if (!dl)
        {
            Engine_LogError("RenderModels: DList cache miss for resource %d (cache full or all meshes unsupported).", entry.resourceId);
            continue;
        }

        const Vector3 pos = entry.transform.GetPosition();
        const Vector3 rot = entry.transform.GetRotation();
        const Vector3 scl = entry.transform.GetScale();

        glPushMatrix();
        glTranslatef(pos.x, pos.y, pos.z);
        if (rot.x != 0.0f)
            glRotatef(rot.x, 1.0f, 0.0f, 0.0f);
        if (rot.y != 0.0f)
            glRotatef(rot.y, 0.0f, 1.0f, 0.0f);
        if (rot.z != 0.0f)
            glRotatef(rot.z, 0.0f, 0.0f, 1.0f);
        glScalef(scl.x, scl.y, scl.z);

        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

        for (uint8_t meshIdx = 0; meshIdx < dl->meshCount; ++meshIdx)
        {
            if (dl->handles[meshIdx] == 0)
                continue;

            if (m_frameDrawCallsUsed >= GFX_DRAW_CALL_BUDGET)
                break;

            const int matIdx = (model->meshMaterial) ? model->meshMaterial[meshIdx] : 0;
            if (model->materials)
            {
                // Resolve the diffuse texture from its resource handle at draw
                // time (model texture deps stream in asynchronously).
                const int32_t texResId = model->materials[matIdx].maps[MATERIAL_MAP_DIFFUSE].textureResourceId;
                if (texResId != lastTexResId)
                {
                    const auto* tex = (texResId >= 0) ? static_cast<const Texture2D*>(Engine_Resource_Get(texResId)) : nullptr;
                    if (tex && tex->id != 0)
                    {
                        glEnable(GL_TEXTURE_2D);
                        glBindTexture(GL_TEXTURE_2D, tex->id);
                        ++m_frameStats.texBinds;
                        lastTexResId = texResId;
                    }
                    else
                    {
                        glDisable(GL_TEXTURE_2D);
                        lastTexResId = -2;
                    }
                }
            }

            glCallList(dl->handles[meshIdx]);
            ++m_frameDrawCallsUsed;
            ++modelMeshDraws;

            const uint32_t verts = static_cast<uint32_t>(dl->vertexCounts[meshIdx]);
            const uint32_t tris = (dl->topologies[meshIdx] == MESH_TOPOLOGY_STRIP) ? (verts >= 2 ? verts - 2 : 0) : verts / 3;
            m_frameStats.trisSubmitted += tris;
            m_frameStats.vertsTransformed += verts;
        }

        glPopMatrix();
    }

    m_frameStats.modelCount = modelMeshDraws;
    glDisable(GL_TEXTURE_2D);
}


Ps2GlRenderer::ModelDListEntry* Ps2GlRenderer::FindOrCompileModelDLists(const Model* model, int32_t resourceId)
{
    for (uint8_t i = 0; i < m_modelDListCacheCount; ++i)
    {
        if (m_modelDListCache[i].resourceId == resourceId)
            return &m_modelDListCache[i];
    }

    if (m_modelDListCacheCount >= GFX_MAX_CACHED_MODELS)
    {
        Engine_LogError("FindOrCompileModelDLists: model DList cache full (%d entries).", static_cast<int>(GFX_MAX_CACHED_MODELS));
        return nullptr;
    }

    ModelDListEntry& entry = m_modelDListCache[m_modelDListCacheCount];
    entry.resourceId = resourceId;
    entry.meshCount = 0;
    std::memset(entry.handles, 0, sizeof(entry.handles));
    std::memset(entry.vertexCounts, 0, sizeof(entry.vertexCounts));
    std::memset(entry.topologies, 0, sizeof(entry.topologies));

    const int meshesToProcess = (model->meshCount < GFX_MAX_MODEL_MESH_COUNT) ? model->meshCount : GFX_MAX_MODEL_MESH_COUNT;

    for (int meshIdx = 0; meshIdx < meshesToProcess; ++meshIdx)
    {
        const Mesh& mesh = model->meshes[meshIdx];

        if (mesh.indices != nullptr)
        {
            Engine_LogError("FindOrCompileModelDLists: resource %d mesh %d has indices — unsupported.", resourceId, meshIdx);
            entry.handles[meshIdx] = 0;
            ++entry.meshCount;
            continue;
        }

        if (!mesh.vertices || mesh.vertexCount == 0)
        {
            Engine_LogError("FindOrCompileModelDLists: resource %d mesh %d has no vertices, skipping.", resourceId, meshIdx);
            entry.handles[meshIdx] = 0;
            ++entry.meshCount;
            continue;
        }

        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_NORMAL_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);

        const int components = (mesh.vertexComponents == 4) ? 4 : 3;
        glVertexPointer(components, GL_FLOAT, 0, mesh.vertices);
        if (mesh.normals)
            glNormalPointer(GL_FLOAT, 0, mesh.normals);
        if (mesh.texcoords)
            glTexCoordPointer(2, GL_FLOAT, 0, mesh.texcoords);

        const GLenum mode = (mesh.topology == MESH_TOPOLOGY_STRIP) ? GL_TRIANGLE_STRIP : GL_TRIANGLES;

        entry.handles[meshIdx] = glGenLists(1);
        glNewList(entry.handles[meshIdx], GL_COMPILE);
        glDrawArrays(mode, 0, mesh.vertexCount);
        glEndList();
        entry.vertexCounts[meshIdx] = mesh.vertexCount;
        entry.topologies[meshIdx] = mesh.topology;

        ++entry.meshCount;
        Engine_LogInfo("FindOrCompileModelDLists: resource %d mesh %d → DList %u (%d verts)", resourceId, meshIdx, entry.handles[meshIdx], mesh.vertexCount);
    }

    ++m_modelDListCacheCount;
    return &entry;
}

void Ps2GlRenderer::ClearModelDListCache()
{
    for (uint8_t i = 0; i < m_modelDListCacheCount; ++i)
    {
        for (uint8_t meshIdx = 0; meshIdx < m_modelDListCache[i].meshCount; ++meshIdx)
        {
            if (m_modelDListCache[i].handles[meshIdx] != 0)
            {
                glDeleteLists(m_modelDListCache[i].handles[meshIdx], 1);
                m_modelDListCache[i].handles[meshIdx] = 0;
            }
        }
        m_modelDListCache[i].resourceId = -1;
        m_modelDListCache[i].meshCount = 0;
    }
    m_modelDListCacheCount = 0;
    Engine_LogInfo("Ps2GlRenderer: model DList cache cleared.");
}

void Ps2GlRenderer::ApplyProjection(const Camera3D& camera) const
{
    const float aspect = GFX_DISPLAY_ASPECT;
    const float nearPlane = GFX_NEAR_PLANE;
    const float farPlane = GFX_FAR_PLANE;
    const float fovY = camera.fovy * GL_RENDERER_DEG_TO_RAD;
    const float top = nearPlane * std::tan(fovY * 0.5f);
    const float bottom = -top;
    const float right = top * aspect;
    const float left = -right;
    glFrustum(left, right, bottom, top, nearPlane, farPlane);
}

void Ps2GlRenderer::ApplyCameraTransform(const Camera3D& camera) const
{
    const Vector3 forward = Normalize(Subtract(camera.target, camera.position));
    const Vector3 side = Normalize(Cross(forward, camera.up));
    const Vector3 up = Cross(side, forward);

    const float viewMatrix[16] = {
        side.x, up.x, -forward.x, 0.0f, side.y, up.y, -forward.y, 0.0f, side.z, up.z, -forward.z, 0.0f, -Dot(side, camera.position), -Dot(up, camera.position), Dot(forward, camera.position), 1.0f};
    glMultMatrixf(viewMatrix);
}

Vector3 Ps2GlRenderer::Normalize(const Vector3& value)
{
    const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z;
    if (lengthSquared <= 0.0f)
        return Vector3{0.0f, 0.0f, 0.0f};

    const float invLength = 1.0f / std::sqrt(lengthSquared);
    return Vector3{value.x * invLength, value.y * invLength, value.z * invLength};
}

Vector3 Ps2GlRenderer::Subtract(const Vector3& a, const Vector3& b) { return Vector3{a.x - b.x, a.y - b.y, a.z - b.z}; }

Vector3 Ps2GlRenderer::Cross(const Vector3& a, const Vector3& b) { return Vector3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

float Ps2GlRenderer::Dot(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

uint32_t Ps2GlRenderer::UploadTexture(const TextureUpload& upload)
{
    const int width = upload.width;
    const int height = upload.height;
    const size_t texels = static_cast<size_t>(width) * static_cast<size_t>(height);

    // Build a retained level-0 copy in the format ps2gl will sample. PAL8 is
    // depalettized to RGBA32 here (ps2gl's mip/CLUT paths are limited on this
    // backend; the 8-bit VRAM saving is kept only on the GIFTAG backend). Mip
    // levels above 0 are dropped — ps2gl does not implement them.
    void* copy = nullptr;
    GLenum type = GL_UNSIGNED_BYTE;
    if (upload.format == PixelFormat::RGBA16)
    {
        type = GL_UNSIGNED_SHORT_5_5_5_1;
        copy = memalign(16, texels * 2);
        if (copy)
            std::memcpy(copy, upload.levelPtr[0], texels * 2);
    }
    else if (upload.format == PixelFormat::PAL8)
    {
        type = GL_UNSIGNED_BYTE;
        copy = memalign(16, texels * 4);
        if (copy)
        {
            const uint8_t* idx = static_cast<const uint8_t*>(upload.levelPtr[0]);
            const uint32_t* clut = static_cast<const uint32_t*>(upload.clut);
            uint32_t* dst = static_cast<uint32_t*>(copy);
            for (size_t i = 0; i < texels; ++i)
                dst[i] = clut ? clut[idx[i]] : 0xFFFFFFFFu;
        }
    }
    else // RGBA32
    {
        type = GL_UNSIGNED_BYTE;
        copy = memalign(16, texels * 4);
        if (copy)
            std::memcpy(copy, upload.levelPtr[0], texels * 4);
    }

    if (!copy)
    {
        Engine_LogError("Ps2GlRenderer: out of memory copying texture pixels.");
        return 0;
    }

    int slot = -1;
    for (int i = 0; i < GL_MAX_TEXTURES; ++i)
    {
        if (m_texRegistry[i].name == 0)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
    {
        Engine_LogError("Ps2GlRenderer: texture registry full (%u).", GL_MAX_TEXTURES);
        free(copy);
        return 0;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (tex == 0)
    {
        Engine_LogError("Ps2GlRenderer: glGenTextures failed.");
        free(copy);
        return 0;
    }

    glBindTexture(GL_TEXTURE_2D, tex);
    const GLint filter = (upload.filter == TextureFilter::Nearest) ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, type, copy);

    m_texRegistry[slot].name = tex;
    m_texRegistry[slot].pixels = copy;
    return static_cast<uint32_t>(tex);
}

uint32_t Ps2GlRenderer::RenderToImage3D(const Renderable3D& what, const Camera3D& camera, int width, int height, const Color3& clearColor)
{
    UNUSED_VAR(what);
    UNUSED_VAR(camera);
    UNUSED_VAR(width);
    UNUSED_VAR(height);
    UNUSED_VAR(clearColor);

    // Not implemented on this backend: a correct synchronous render-to-image
    // needs pglFinishRenderingImmediateGeometry, which external/ps2gl declares
    // but never implements (mNotImplemented) -- there is no verified way to
    // know the GS has actually finished writing the scratch target before
    // handing its handle to the UI to sample this same frame, and this engine
    // does not patch ps2gl to add it (see the NEVER DO list). GifTag, the
    // sibling PS2 backend, implements this the same way every other GS-
    // generation backend does and is the answer when this capability is
    // needed on PS2. See docs/ps2/renderers/PS2GL.md.
    static bool s_logged = false;
    if (!s_logged)
    {
        s_logged = true;
        Engine_LogInfo("Ps2GlRenderer: RenderToImage3D is not supported on this backend; select giftag if a build needs it.");
    }
    return 0;
}

void Ps2GlRenderer::ReleaseTexture(uint32_t handle)
{
    if (handle == 0)
        return;
    GLuint t = static_cast<GLuint>(handle);
    glDeleteTextures(1, &t);
    for (int i = 0; i < GL_MAX_TEXTURES; ++i)
    {
        if (m_texRegistry[i].name == t)
        {
            free(m_texRegistry[i].pixels);
            m_texRegistry[i].pixels = nullptr;
            m_texRegistry[i].name = 0;
            break;
        }
    }
}

void Ps2GlRenderer::SetCamera3D(CameraID id, const Camera3D& camera) { m_drawLists.SetCamera3D(id, camera); }
void Ps2GlRenderer::SetActiveCamera3D(CameraID id) { m_drawLists.SetActiveCamera3D(id); }
void Ps2GlRenderer::SetActiveCamera2D(const Camera2D& camera) { m_drawLists.SetActiveCamera2D(camera); }

bool Ps2GlRenderer::IsInitialized() const { return m_initialized; }

DrawStats Ps2GlRenderer::GetLastStats() const { return m_drawLists.GetLastStats(); }

Camera3D Ps2GlRenderer::GetActiveCamera3D() const { return m_drawLists.GetCamera3D(); }
