#include "platform/ps2/renderer/GifTag.h"

#include <cmath>
#include <cstring>
#include <ctime>
#include <malloc.h>

extern "C" {
#include <dma.h>
#include <gif_tags.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include <kernel.h>
#include <math3d.h>
#include <packet2_utils.h>
}

#include "core/EngineDebug.h"
#include "core/EngineMemory.h"
#include "level/EngineLevel.h"
#include "level/EngineSector.h"
#include "resources/EngineResource.h"
#include "Macros.h"
#include "graphics/Frustum.h"
#include "graphics/PrimitiveGeometry.h"

namespace
{
    constexpr float TAG_DEG_TO_RAD = 0.017453292519943295f;
    constexpr float TAG_W_EPS = 0.001f;
    constexpr float TAG_BACKFACE_SIGN = 1.0f;
    constexpr float TAG_Z_MAX = 16777215.0f;

    constexpr float TAG_GS_OFFSET_X = GFX_GIFTAG_GS_ORIGIN - GFX_SCREEN_WIDTH * 0.5f;
    constexpr float TAG_GS_OFFSET_Y = GFX_GIFTAG_GS_ORIGIN - GFX_SCREEN_HEIGHT * 0.5f;

    inline bool GsCoordVisible(float x, float y) { return x >= 0.0f && x <= GFX_GIFTAG_GS_COORD_MAX && y >= 0.0f && y <= GFX_GIFTAG_GS_COORD_MAX; }

    inline uint16_t GsFix4(float v)
    {
        if (v <= 0.0f)
            return 0u;
        if (v >= GFX_GIFTAG_GS_COORD_MAX)
            return static_cast<uint16_t>(GFX_GIFTAG_GS_COORD_MAX * 16.0f);
        return static_cast<uint16_t>(v * 16.0f);
    }

    inline uint32_t GsDepth(float ndcZ)
    {
        const float z = (1.0f - ndcZ) * TAG_Z_MAX;
        if (z <= 0.0f)
            return 0u;
        if (z >= TAG_Z_MAX)
            return static_cast<uint32_t>(TAG_Z_MAX);
        return static_cast<uint32_t>(z);
    }

    constexpr uint64_t GSREG_RGBAQ = 0x01;
    constexpr uint64_t GSREG_ST = 0x02;
    constexpr uint64_t GSREG_UV = 0x03;
    constexpr uint64_t GSREG_XYZ2 = 0x05;

    constexpr uint64_t GIFTAG_NLOOP_MASK = 0x7FFFull;
    constexpr uint64_t GIFTAG_EOP = 1ull << 15;
    constexpr uint64_t GIFTAG_FLG_REGLIST = 1ull << 58;
    constexpr int GIFTAG_NREG_SHIFT = 60;
    constexpr uint64_t GIFTAG_NREG_MASK = 0xFull;

    constexpr uint32_t PRIM_BIT_IIP = 1u << 3;
    constexpr uint32_t PRIM_BIT_TME = 1u << 4;
    constexpr uint32_t PRIM_BIT_ABE = 1u << 6;
    constexpr uint32_t PRIM_BIT_FST = 1u << 8;

    inline uint64_t GifTagAd(uint32_t nloop) { return (static_cast<uint64_t>(nloop) & GIFTAG_NLOOP_MASK) | GIFTAG_EOP | (1ull << GIFTAG_NREG_SHIFT); }

    inline uint64_t GifTagLo(uint32_t nloop, uint32_t nreg)
    {
        return (static_cast<uint64_t>(nloop) & GIFTAG_NLOOP_MASK) | GIFTAG_EOP | GIFTAG_FLG_REGLIST | ((static_cast<uint64_t>(nreg) & GIFTAG_NREG_MASK) << GIFTAG_NREG_SHIFT);
    }

    inline uint64_t FloatBits(float f)
    {
        uint32_t b;
        std::memcpy(&b, &f, sizeof(b));
        return b;
    }
} // namespace

namespace
{
    void BuildViewProj(const Camera3D& camera, float vp[16])
    {
        float view[16], proj[16];
        Frustum_BuildLookAt(view, camera);
        const float aspect = GFX_DISPLAY_ASPECT;
        Frustum_BuildPerspective(proj, camera.fovy, aspect, GFX_NEAR_PLANE, GFX_FAR_PLANE);
        Frustum_Mult4x4(vp, proj, view);
    }
} // namespace

void GifTagRenderer::BuildModelMatrix(float out[16], const Vector3& pos, const Vector3& rot, const Vector3& scl)
{
    const float rx = rot.x * TAG_DEG_TO_RAD;
    const float ry = rot.y * TAG_DEG_TO_RAD;
    const float rz = rot.z * TAG_DEG_TO_RAD;
    const float cx = std::cos(rx), sx = std::sin(rx);
    const float cy = std::cos(ry), sy = std::sin(ry);
    const float cz = std::cos(rz), sz = std::sin(rz);

    const float r00 = cy * cz;
    const float r01 = cy * sz;
    const float r02 = -sy;
    const float r10 = sx * sy * cz - cx * sz;
    const float r11 = sx * sy * sz + cx * cz;
    const float r12 = sx * cy;
    const float r20 = cx * sy * cz + sx * sz;
    const float r21 = cx * sy * sz - sx * cz;
    const float r22 = cx * cy;

    out[0] = r00 * scl.x;
    out[1] = r10 * scl.x;
    out[2] = r20 * scl.x;
    out[3] = 0.0f;
    out[4] = r01 * scl.y;
    out[5] = r11 * scl.y;
    out[6] = r21 * scl.y;
    out[7] = 0.0f;
    out[8] = r02 * scl.z;
    out[9] = r12 * scl.z;
    out[10] = r22 * scl.z;
    out[11] = 0.0f;
    out[12] = pos.x;
    out[13] = pos.y;
    out[14] = pos.z;
    out[15] = 1.0f;
}

static void AddPrimitive(DrawLists& lists, Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, Color3 color, int32_t textureId)
{
    PrimitiveDrawEntry entry;
    entry.transform = Transform3D(position, rotation, scale);
    entry.color = color;
    entry.type = primitive;
    entry.textureId = textureId;
    lists.AddPrimitive(entry);
}

GifTagRenderer::GifTagRenderer(const EngineConfig& config)
{
    UNUSED_VAR(config);
    Engine_LogInfo("GifTagRenderer: Initializing GIFTAG renderer (%dx%d %s)", GFX_SCREEN_WIDTH, GFX_SCREEN_HEIGHT, GFX_SCREEN_REGION_STR);

    const bool pal = (GFX_SCREEN_HEIGHT == GFX_SCREEN_PAL_HEIGHT);

    for (int i = 0; i < 2; ++i)
    {
        m_frame[i].width = GFX_SCREEN_WIDTH;
        m_frame[i].height = GFX_SCREEN_HEIGHT;
        m_frame[i].mask = 0;
        m_frame[i].psm = GS_PSM_32;
        const int frameAddr = graph_vram_allocate(GFX_SCREEN_WIDTH, GFX_SCREEN_HEIGHT, GS_PSM_32, GRAPH_ALIGN_PAGE);
        if (frameAddr < 0)
        {
            Engine_LogError("GifTagRenderer: no GS VRAM for frame buffer %d (%dx%d).", i, GFX_SCREEN_WIDTH, GFX_SCREEN_HEIGHT);
            return;
        }
        m_frame[i].address = frameAddr;
    }

    m_z.enable = DRAW_ENABLE;
    m_z.method = ZTEST_METHOD_GREATER_EQUAL;
    m_z.zsm = GS_ZBUF_32;
    m_z.mask = 0;
    const int depthAddr = graph_vram_allocate(GFX_SCREEN_WIDTH, GFX_SCREEN_HEIGHT, GS_ZBUF_32, GRAPH_ALIGN_PAGE);
    if (depthAddr < 0)
    {
        Engine_LogError("GifTagRenderer: no GS VRAM for the depth buffer (%dx%d).", GFX_SCREEN_WIDTH, GFX_SCREEN_HEIGHT);
        return;
    }
    m_z.address = depthAddr;

    // RenderToImage3D's scratch target, reserved here rather than negotiated
    // per call so GetTextureBudgetBytes() below already excludes it -- a
    // permanently-pinned region taken from the texture heap after the fact
    // would let the resource manager's shadow-tracked budget over-commit
    // against VRAM this renderer has already spent.
    const int imgColorWords = graph_vram_size(GFX_PS2_IMAGE_TARGET_SIZE, GFX_PS2_IMAGE_TARGET_SIZE, GS_PSM_32, GRAPH_ALIGN_PAGE);
    const int imgDepthWords = graph_vram_size(GFX_PS2_IMAGE_TARGET_SIZE, GFX_PS2_IMAGE_TARGET_SIZE, GS_ZBUF_32, GRAPH_ALIGN_PAGE);
    const int imgColorAddr = graph_vram_allocate(GFX_PS2_IMAGE_TARGET_SIZE, GFX_PS2_IMAGE_TARGET_SIZE, GS_PSM_32, GRAPH_ALIGN_PAGE);
    const int imgDepthAddr = (imgColorAddr >= 0) ? graph_vram_allocate(GFX_PS2_IMAGE_TARGET_SIZE, GFX_PS2_IMAGE_TARGET_SIZE, GS_ZBUF_32, GRAPH_ALIGN_PAGE) : -1;
    if (imgColorAddr >= 0 && imgDepthAddr >= 0)
    {
        m_imageColorAddr = static_cast<uint32_t>(imgColorAddr);
        m_imageDepthAddr = static_cast<uint32_t>(imgDepthAddr);
    }
    else
    {
        Engine_LogError("GifTagRenderer: no GS VRAM for the %dx%d image target; Ui_Image3D will report unsupported.", GFX_PS2_IMAGE_TARGET_SIZE, GFX_PS2_IMAGE_TARGET_SIZE);
    }

    {
        const int fbWords = graph_vram_size(GFX_SCREEN_WIDTH, GFX_SCREEN_HEIGHT, GS_PSM_32, GRAPH_ALIGN_PAGE);
        const int zWords = graph_vram_size(GFX_SCREEN_WIDTH, GFX_SCREEN_HEIGHT, GS_ZBUF_32, GRAPH_ALIGN_PAGE);
        const int imgWords = (m_imageColorAddr != 0) ? (imgColorWords + imgDepthWords) : 0;
        int remaining = GRAPH_VRAM_MAX_WORDS - (2 * fbWords + zWords + imgWords) - GRAPH_ALIGN_PAGE;
        if (remaining < 0)
            remaining = 0;
        const int heapH = remaining / 64;
        const int heapBase = (heapH > 0) ? graph_vram_allocate(64, heapH, GS_PSM_32, GRAPH_ALIGN_PAGE) : -1;
        m_texHeapBase = (heapBase >= 0) ? static_cast<uint32_t>(heapBase) : 0u;
        m_texHeapWords = (heapBase >= 0) ? static_cast<uint32_t>(heapH * 64) : 0u;
        if (m_texHeapWords > 0)
        {
            m_vramExtents[0] = VramExtent{m_texHeapBase, m_texHeapWords, false};
            m_vramExtentCount = 1;
            Engine_LogInfo("GifTagRenderer: texture VRAM heap %u words (~%u KB) at 0x%X", m_texHeapWords, (m_texHeapWords * 4) / 1024, m_texHeapBase);
        }
        else
        {
            Engine_LogError("GifTagRenderer: no GS VRAM left for texture heap.");
        }
    }

    graph_set_mode(GRAPH_MODE_INTERLACED, pal ? GRAPH_MODE_PAL : GRAPH_MODE_NTSC, GRAPH_MODE_FIELD, GRAPH_DISABLE);
    graph_set_screen(0, 0, GFX_SCREEN_WIDTH, GFX_SCREEN_HEIGHT);
    graph_set_bgcolor(0, 0, 0);
    graph_set_framebuffer_filtered(m_frame[1].address, m_frame[1].width, m_frame[1].psm, 0, 0);
    graph_enable_output();
    m_drawBuffer = 0;
    m_displayBuffer = 1;

    dma_channel_initialize(DMA_CHANNEL_GIF, nullptr, 0);
    dma_channel_fast_waits(DMA_CHANNEL_GIF);

    for (int i = 0; i < GFX_GIFTAG_PACKET_BUFFERS; ++i)
        m_geomBuf[i] = packet2_create(GFX_GIFTAG_PACKET_QWORDS, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
    m_geomIndex = 0;
    m_geom = m_geomBuf[0];
    m_env = packet2_create(GFX_GIFTAG_ENV_PACKET_QWORDS, P2_TYPE_NORMAL, P2_MODE_CHAIN, 0);
    if (!m_geomBuf[0] || !m_geomBuf[1] || !m_env)
    {
        Engine_LogError("GifTagRenderer: failed to create packets.");
        return;
    }

    m_xyz = static_cast<xyz_t*>(memalign(16, sizeof(xyz_t) * GFX_GIFTAG_MAX_VERTS));
    m_srcIdx = static_cast<uint32_t*>(memalign(16, sizeof(uint32_t) * GFX_GIFTAG_MAX_VERTS));
    m_q = static_cast<float*>(memalign(16, sizeof(float) * GFX_GIFTAG_MAX_VERTS));
    m_clipBatch = memalign(16, sizeof(VECTOR) * GFX_GIFTAG_XFORM_BATCH);
    m_vecBatch = memalign(16, sizeof(VECTOR) * GFX_GIFTAG_XFORM_BATCH);
    if (!m_xyz || !m_srcIdx || !m_q || !m_clipBatch || !m_vecBatch)
    {
        Engine_Panic("GifTagRenderer: out of memory for transform scratch");
    }

    SelfTestVu0Transform();

    float* megaBatch = static_cast<float*>(Engine_GetSlot(ARENA_RENDERER, 0));
    if (!megaBatch)
    {
        Engine_Panic("GifTagRenderer: Failed to retrieve ARENA_RENDERER slot 0!");
    }
    const size_t bytes = (PRIMITIVE_CUBE_VERTEX_COUNT + PRIMITIVE_SPHERE_VERTEX_COUNT + PRIMITIVE_CYLINDER_VERTEX_COUNT) * (3 + 3 + 2) * sizeof(float);
    Engine_LoadToSlot(ARENA_RENDERER, 0, nullptr, bytes);
    m_drawLists.Init(megaBatch);

    m_initialized = true;
}

void GifTagRenderer::Shutdown()
{
    if (m_framePending)
    {
        dma_channel_wait(DMA_CHANNEL_GIF, 0);
        draw_wait_finish();
        m_framePending = false;
    }
    for (int i = 0; i < GFX_GIFTAG_PACKET_BUFFERS; ++i)
    {
        if (m_geomBuf[i])
            packet2_free(m_geomBuf[i]);
        m_geomBuf[i] = nullptr;
    }
    if (m_env)
        packet2_free(m_env);
    m_geom = m_env = nullptr;
    free(m_xyz);
    free(m_srcIdx);
    free(m_q);
    free(m_clipBatch);
    free(m_vecBatch);
    m_xyz = nullptr;
    m_srcIdx = nullptr;
    m_q = nullptr;
    m_clipBatch = nullptr;
    m_vecBatch = nullptr;
    m_drawLists.Shutdown();
    graph_shutdown();
    m_initialized = false;
}

RendererType GifTagRenderer::GetRendererType() const { return RendererType::GifTag; }

void GifTagRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale)
{
    AddPrimitive(m_drawLists, primitive, position, rotation, scale, {1.0f, 1.0f, 1.0f}, -1);
}
void GifTagRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, Color3 color)
{
    AddPrimitive(m_drawLists, primitive, position, rotation, scale, color, -1);
}
void GifTagRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, int32_t textureId)
{
    AddPrimitive(m_drawLists, primitive, position, rotation, scale, {1.0f, 1.0f, 1.0f}, textureId);
}
void GifTagRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, Color3 color, int32_t textureId)
{
    AddPrimitive(m_drawLists, primitive, position, rotation, scale, color, textureId);
}
void GifTagRenderer::AddLevelToDrawList(const Level& level) { UNUSED_VAR(level); }
void GifTagRenderer::AddModelToDrawList(int32_t modelId, const Vector3& position, const Vector3& rotation, const Vector3& scale)
{
    ModelDrawEntry entry;
    entry.resourceId = modelId;
    entry.transform = Transform3D(position, rotation, scale);
    m_drawLists.AddModel(entry);
}
void GifTagRenderer::AddSkyToDrawList(int32_t resourceId) { m_drawLists.SetSkyboxTexture(resourceId); }
void GifTagRenderer::ClearDrawLists() { m_drawLists.Reset(false); }

void GifTagRenderer::BeginFrame()
{
    m_inFrame = true;
    m_frameVertsUsed = 0;
    m_frameDroppedObjects = 0;
    m_lastBoundTex = 0;
    m_frameStats = DrawStats{};

    packet2_reset(m_geom, 0);

    packet2_update(m_geom, draw_setup_environment(m_geom->next, 0, &m_frame[m_drawBuffer], &m_z));
    packet2_update(m_geom, draw_primitive_xyoffset(m_geom->next, 0, TAG_GS_OFFSET_X, TAG_GS_OFFSET_Y));

    blend_t blend;
    blend.color1 = BLEND_COLOR_SOURCE;
    blend.color2 = BLEND_COLOR_DEST;
    blend.alpha = BLEND_ALPHA_SOURCE;
    blend.color3 = BLEND_COLOR_DEST;
    blend.fixed_alpha = 0x80;
    packet2_update(m_geom, draw_alpha_blending(m_geom->next, 0, &blend));

    EmitClear(m_clearColor);

    packet2_add_u64(m_geom, GifTagAd(1));
    packet2_add_u64(m_geom, GIF_REG_AD);
    packet2_add_u64(m_geom, GS_SET_PRMODECONT(1));
    packet2_add_u64(m_geom, GS_REG_PRMODECONT);
}

void GifTagRenderer::EmitPrim(uint32_t prim)
{
    packet2_add_u64(m_geom, GifTagAd(1));
    packet2_add_u64(m_geom, GIF_REG_AD);
    packet2_add_u64(m_geom, prim);
    packet2_add_u64(m_geom, GS_REG_PRIM);
}

void GifTagRenderer::EmitClear(const Color3& color)
{
    const int cr = static_cast<int>(color.r * 255.0f);
    const int cg = static_cast<int>(color.g * 255.0f);
    const int cb = static_cast<int>(color.b * 255.0f);

    packet2_update(m_geom, draw_disable_tests(m_geom->next, 0, &m_z));
    packet2_update(m_geom, draw_clear(m_geom->next, 0, TAG_GS_OFFSET_X, TAG_GS_OFFSET_Y, GFX_SCREEN_WIDTH, GFX_SCREEN_HEIGHT, cr, cg, cb));
    packet2_update(m_geom, draw_enable_tests(m_geom->next, 0, &m_z));
}

void GifTagRenderer::EndFrame()
{
    FlushQuads2D();

    if (m_frameDroppedObjects > 0)
        Engine_LogError("GifTagRenderer: dropped %u object(s) this frame (geometry budget exceeded).", m_frameDroppedObjects);

    packet2_update(m_geom, draw_finish(m_geom->next));
    m_frameStats.submitBufferCapacityBytes = GFX_GIFTAG_PACKET_QWORDS * 16u;
    m_frameStats.submitBufferUsedBytes = static_cast<uint32_t>(m_geom->next - m_geom->base) * 16u;

    const double waitStart = static_cast<double>(clock()) / CLOCKS_PER_SEC;
    if (m_framePending)
    {
        dma_channel_wait(DMA_CHANNEL_GIF, 0);
        draw_wait_finish();
        graph_wait_vsync();
        graph_set_framebuffer_filtered(m_frame[m_displayBuffer].address, m_frame[m_displayBuffer].width, m_frame[m_displayBuffer].psm, 0, 0);
    }
    m_frameStats.presentWaitMs = static_cast<float>((static_cast<double>(clock()) / CLOCKS_PER_SEC - waitStart) * 1000.0);

    FlushCache(0);
    dma_channel_send_packet2(m_geom, DMA_CHANNEL_GIF, 1);
    m_framePending = true;
    m_displayBuffer = m_drawBuffer;
    m_drawBuffer ^= 1;
    m_geomIndex ^= 1;
    m_geom = m_geomBuf[m_geomIndex];

    m_drawLists.SetLastStats(m_frameStats);
    m_inFrame = false;
}

void GifTagRenderer::ClearFrame(const Color3& color)
{
    m_clearColor = color;
    if (m_inFrame)
        EmitClear(color);
}

void GifTagRenderer::DrawQuad2D(const Quad2D& quad)
{
    if (m_quad2DCount >= TAG_MAX_2D_QUADS)
    {
        ++m_droppedQuads2D;
        return;
    }
    m_quads2D[m_quad2DCount++] = quad;
}

void GifTagRenderer::FlushQuads2D()
{
    if (m_droppedQuads2D > 0)
    {
        Engine_LogError("GifTagRenderer: dropped %u 2D quad(s) this frame (queue capacity %u).", m_droppedQuads2D, TAG_MAX_2D_QUADS);
        m_droppedQuads2D = 0;
    }

    m_reserved2DQw = 0;

    if (m_quad2DCount == 0)
        return;

    packet2_update(m_geom, draw_disable_tests(m_geom->next, 0, &m_z));

    uint16_t dropped = 0;
    for (uint16_t i = 0; i < m_quad2DCount; ++i)
    {
        const Quad2D& q = m_quads2D[i];
        const bool textured = (q.texture != 0) && (q.texture <= TAG_MAX_TEXTURES) && m_textures[q.texture - 1].inUse;

        const uint32_t quadQw = (textured ? 4u : 3u) + GFX_GIFTAG_PRIM_QW + (textured ? BindCostQwords(q.texture) : 0u);
        if (!PacketHasSpace(quadQw))
        {
            dropped = static_cast<uint16_t>(m_quad2DCount - i);
            break;
        }

        if (textured)
            BindTexture(q.texture);

        const uint16_t x0 = GsFix4(q.x + TAG_GS_OFFSET_X);
        const uint16_t y0 = GsFix4(q.y + TAG_GS_OFFSET_Y);
        const uint16_t x1 = GsFix4(q.x + q.w + TAG_GS_OFFSET_X);
        const uint16_t y1 = GsFix4(q.y + q.h + TAG_GS_OFFSET_Y);

        const uint64_t alpha = (static_cast<uint64_t>(q.a) * 0x80u) / 255u;
        const uint32_t colorScale = textured ? 0x80u : 0xFFu;
        const uint64_t cr = (static_cast<uint64_t>(q.r) * colorScale) / 255u;
        const uint64_t cg = (static_cast<uint64_t>(q.g) * colorScale) / 255u;
        const uint64_t cb = (static_cast<uint64_t>(q.b) * colorScale) / 255u;
        const uint64_t rgbaq = cr | (cg << 8) | (cb << 16) | (alpha << 24) | (FloatBits(1.0f) << 32);

        uint32_t prim = PRIM_SPRITE;
        if (textured)
            prim |= PRIM_BIT_TME | PRIM_BIT_FST;
        if (q.a < 255)
            prim |= PRIM_BIT_ABE;


        if (textured)
        {
            const TexEntry& t = m_textures[q.texture - 1];
            const uint32_t tu0 = (static_cast<uint32_t>(q.u0) * static_cast<uint32_t>(t.width) * 16u) / 65535u;
            const uint32_t tv0 = (static_cast<uint32_t>(q.v0) * static_cast<uint32_t>(t.height) * 16u) / 65535u;
            const uint32_t tu1 = (static_cast<uint32_t>(q.u1) * static_cast<uint32_t>(t.width) * 16u) / 65535u;
            const uint32_t tv1 = (static_cast<uint32_t>(q.v1) * static_cast<uint32_t>(t.height) * 16u) / 65535u;

            EmitPrim(prim);
            packet2_add_u64(m_geom, GifTagLo(2, 3));
            packet2_add_u64(m_geom, GSREG_RGBAQ | (GSREG_UV << 4) | (GSREG_XYZ2 << 8));
            packet2_add_u64(m_geom, rgbaq);
            packet2_add_u64(m_geom, static_cast<uint64_t>(tu0) | (static_cast<uint64_t>(tv0) << 16));
            packet2_add_u64(m_geom, static_cast<uint64_t>(x0) | (static_cast<uint64_t>(y0) << 16));
            packet2_add_u64(m_geom, rgbaq);
            packet2_add_u64(m_geom, static_cast<uint64_t>(tu1) | (static_cast<uint64_t>(tv1) << 16));
            packet2_add_u64(m_geom, static_cast<uint64_t>(x1) | (static_cast<uint64_t>(y1) << 16));
        }
        else
        {
            EmitPrim(prim);
            packet2_add_u64(m_geom, GifTagLo(2, 2));
            packet2_add_u64(m_geom, GSREG_RGBAQ | (GSREG_XYZ2 << 4));
            packet2_add_u64(m_geom, rgbaq);
            packet2_add_u64(m_geom, static_cast<uint64_t>(x0) | (static_cast<uint64_t>(y0) << 16));
            packet2_add_u64(m_geom, rgbaq);
            packet2_add_u64(m_geom, static_cast<uint64_t>(x1) | (static_cast<uint64_t>(y1) << 16));
        }
    }

    packet2_update(m_geom, draw_enable_tests(m_geom->next, 0, &m_z));

    if (dropped > 0)
        Engine_LogError("GifTagRenderer: dropped %u 2D quad(s) this frame (transfer packet full).", dropped);

    m_quad2DCount = 0;
}

void GifTagRenderer::DrawGrid(int32_t slices, float spacing)
{
    UNUSED_VAR(slices);
    UNUSED_VAR(spacing);
}

void GifTagRenderer::Render()
{
    m_reserved2DQw = static_cast<uint32_t>(m_quad2DCount) * GFX_GIFTAG_QUAD2D_QW + GFX_GIFTAG_2D_RESERVE_QW;

    m_drawLists.SortForSubmission();

    const Camera3D& camera = m_drawLists.GetCamera3D();
    float vp[16];
    BuildViewProj(camera, vp);
    FrustumPlanes frustum;
    Frustum_FromViewProj(&frustum, vp);

    RenderSkybox(m_drawLists);

    {
        DrawLists& lists = m_drawLists;
        const uint16_t uCount = lists.GetUntexturedCount();
        const uint16_t tCount = lists.GetTexturedCount();
        const PrimitiveDrawEntry* uPrims = lists.GetUntexturedPrims();
        const PrimitiveDrawEntry* tPrims = lists.GetTexturedPrims();

        auto drawPrim = [&](const PrimitiveDrawEntry& e, uint32_t texId)
        {
            Vector3 wc;
            float wr;
            Frustum_WorldSphere(e.transform.GetPosition(), e.transform.GetScale(), Vector3{0.0f, 0.0f, 0.0f}, lists.GetPrimitiveBaseRadius(e.type), &wc, &wr);
            if (!Frustum_SphereVisible(&frustum, wc, wr))
            {
                ++m_frameStats.entriesCulled;
                return;
            }

            const PrimitiveArrays arr = lists.GetPrimitiveArrays(e.type);
            float model[16], mvp[16];
            BuildModelMatrix(model, e.transform.GetPosition(), e.transform.GetRotation(), e.transform.GetScale());
            Frustum_Mult4x4(mvp, vp, model);
            DrawTriangles(mvp, arr.verts, 3, texId ? arr.uvs : nullptr, arr.vertexCount, e.color, texId);
            ++m_frameStats.primitiveCount;
        };

        for (uint16_t i = 0; i < uCount; ++i)
            drawPrim(uPrims[i], 0);

        for (uint16_t i = 0; i < tCount; ++i)
        {
            uint32_t texId = 0;
            const auto* tex = static_cast<const Texture2D*>(Engine_Resource_Get(tPrims[i].textureId));
            if (tex)
                texId = tex->id;
            drawPrim(tPrims[i], texId);
        }
    }

    RenderModels(m_drawLists);
    RenderLevel(vp, frustum);
    m_drawLists.Reset(false);
}

void GifTagRenderer::RenderLevel(const float vp[16], const FrustumPlanes& frustum)
{
    if (!Engine_Level_Current())
        return;

    uint32_t count = 0;
    const SectorResident* residents = Engine_Sector_GetResidents(&count);
    if (!residents)
        return;

    const bool savedCull = m_backfaceCull;
    m_backfaceCull = false;

    uint32_t renderable = 0, visible = 0, drawnMeshes = 0;

    for (uint32_t s = 0; s < count; ++s)
    {
        const SectorResident& sector = residents[s];
        if (sector.state != SECTOR_READY || sector.meshCount == 0)
            continue;
        ++renderable;
        if (!Frustum_AabbVisible(&frustum, sector.bounds))
        {
            ++m_frameStats.entriesCulled;
            continue;
        }
        ++visible;

        for (uint32_t m = 0; m < sector.meshCount && m < LEVEL_MAX_MESHES_PER_SECTOR; ++m)
        {
            const Mesh& mesh = sector.meshes[m];
            if (!mesh.vertices || mesh.vertexCount == 0)
                continue;

            uint32_t texId = 0;
            const int32_t texResId = sector.meshTexture[m];
            if (texResId >= 0)
            {
                const auto* tex = static_cast<const Texture2D*>(Engine_Resource_Get(texResId));
                if (tex)
                    texId = tex->id;
            }

            ++drawnMeshes;

            const int components = (mesh.vertexComponents == 4) ? 4 : 3;
            const float* uv = texId ? mesh.texcoords : nullptr;
            if (mesh.topology == MESH_TOPOLOGY_STRIP)
                DrawStrip(vp, mesh.vertices, components, uv, static_cast<uint32_t>(mesh.vertexCount), Color3{1.0f, 1.0f, 1.0f}, texId);
            else
                DrawTriangles(vp, mesh.vertices, components, uv, static_cast<uint32_t>(mesh.vertexCount), Color3{1.0f, 1.0f, 1.0f}, texId);
        }
    }

    m_backfaceCull = savedCull;

    static bool s_LoggedLevelStats = false;
    if (!s_LoggedLevelStats)
    {
        s_LoggedLevelStats = true;
        Engine_LogInfo("RenderLevel: residents=%u renderable=%u visible=%u drawnMeshes=%u", count, renderable, visible, drawnMeshes);
    }
}

static inline uint32_t WorstCaseQwords(uint32_t vertexCount) { return 2u + GFX_GIFTAG_PRIM_QW + (vertexCount * 3u + 1u) / 2u; }

bool GifTagRenderer::PacketHasSpace(uint32_t qwNeeded) const
{
    const uint32_t used = static_cast<uint32_t>(m_geom->next - m_geom->base);
    return used + qwNeeded + m_reserved2DQw + GFX_GIFTAG_PACKET_MARGIN_QW <= static_cast<uint32_t>(GFX_GIFTAG_PACKET_QWORDS);
}

uint32_t GifTagRenderer::BindCostQwords(uint32_t textureId) const { return (textureId != 0u && textureId != m_lastBoundTex) ? static_cast<uint32_t>(GFX_GIFTAG_TEXBIND_QW) : 0u; }

void GifTagRenderer::DrawTriangles(const float mvp[16], const float* verts, int components, const float* uvs, uint32_t vertexCount, Color3 color, uint32_t textureId)
{
    if (!verts || vertexCount < 3)
        return;

    if (static_cast<uint32_t>(m_frameVertsUsed) + vertexCount > static_cast<uint32_t>(GFX_GIFTAG_MAX_VERTS) || !PacketHasSpace(WorstCaseQwords(vertexCount) + BindCostQwords(textureId)))
    {
        ++m_frameDroppedObjects;
        m_frameStats.trisCulled += vertexCount / 3;
        return;
    }

    const bool textured = (uvs != nullptr && textureId != 0);
    uint32_t emitted = 0;

    for (uint32_t tri = 0; tri + 2 < vertexCount; tri += 3)
    {
        float sx[3], sy[3], inv3[3];
        uint32_t zc[3];
        bool ok = true;
        for (int j = 0; j < 3; ++j)
        {
            const float* v = verts + (tri + j) * components;
            const float x = v[0], y = v[1], z = v[2];
            const float clipX = mvp[0] * x + mvp[4] * y + mvp[8] * z + mvp[12];
            const float clipY = mvp[1] * x + mvp[5] * y + mvp[9] * z + mvp[13];
            const float clipZ = mvp[2] * x + mvp[6] * y + mvp[10] * z + mvp[14];
            const float clipW = mvp[3] * x + mvp[7] * y + mvp[11] * z + mvp[15];
            if (clipW <= TAG_W_EPS)
            {
                ok = false;
                break;
            }
            const float inv = 1.0f / clipW;
            sx[j] = clipX * inv * (GFX_SCREEN_WIDTH * 0.5f) + GFX_GIFTAG_GS_ORIGIN;
            sy[j] = -clipY * inv * (GFX_SCREEN_HEIGHT * 0.5f) + GFX_GIFTAG_GS_ORIGIN;
            if (!GsCoordVisible(sx[j], sy[j]))
            {
                ok = false;
                break;
            }
            zc[j] = GsDepth(clipZ * inv * 0.5f + 0.5f);
            inv3[j] = inv;
        }
        if (!ok)
            continue;

        if (m_backfaceCull)
        {
            const float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
            if (area * TAG_BACKFACE_SIGN > 0.0f)
                continue;
        }

        for (int j = 0; j < 3; ++j)
        {
            m_xyz[emitted].x = GsFix4(sx[j]);
            m_xyz[emitted].y = GsFix4(sy[j]);
            m_xyz[emitted].z = zc[j];
            m_srcIdx[emitted] = tri + j;
            m_q[emitted] = inv3[j];
            ++emitted;
        }
    }

    const uint32_t trisIn = vertexCount / 3;
    m_frameStats.vertsTransformed += trisIn * 3;
    m_frameStats.trisSubmitted += emitted / 3;
    m_frameStats.trisCulled += trisIn - emitted / 3;

    if (emitted == 0)
        return;

    m_frameVertsUsed += emitted;

    if (textured)
        BindTexture(textureId);

    const float colorScale = textured ? 128.0f : 255.0f;
    const uint8_t r = static_cast<uint8_t>(color.r * colorScale);
    const uint8_t g = static_cast<uint8_t>(color.g * colorScale);
    const uint8_t b = static_cast<uint8_t>(color.b * colorScale);
    const uint64_t rgbaLo = static_cast<uint64_t>(r) | (static_cast<uint64_t>(g) << 8) | (static_cast<uint64_t>(b) << 16) | (static_cast<uint64_t>(0x80) << 24);

    const uint32_t prim = PRIM_TRIANGLE | PRIM_BIT_IIP | (textured ? PRIM_BIT_TME : 0u);
    const uint32_t nreg = textured ? 3u : 2u;
    const uint64_t reglist = textured ? (GSREG_ST | (GSREG_RGBAQ << 4) | (GSREG_XYZ2 << 8)) : (GSREG_RGBAQ | (GSREG_XYZ2 << 4));

    EmitPrim(prim);
    packet2_add_u64(m_geom, GifTagLo(emitted, nreg));
    packet2_add_u64(m_geom, reglist);

    for (uint32_t k = 0; k < emitted; ++k)
    {
        const float q = m_q[k];
        if (textured)
        {
            const float* uv = uvs + m_srcIdx[k] * 2;
            packet2_add_u64(m_geom, FloatBits(uv[0] * q) | (FloatBits(uv[1] * q) << 32));
        }
        packet2_add_u64(m_geom, rgbaLo | (FloatBits(q) << 32));
        uint64_t xyzWord;
        std::memcpy(&xyzWord, &m_xyz[k], sizeof(xyzWord));
        packet2_add_u64(m_geom, xyzWord);
    }

    if (((emitted * nreg) & 1u) != 0u)
        packet2_add_u64(m_geom, 0);
}

void GifTagRenderer::SelfTestVu0Transform()
{
    m_useVu0 = false;

    Camera3D cam{};
    cam.position = Vector3{3.0f, 4.0f, 5.0f};
    cam.target = Vector3{0.0f, 0.0f, 0.0f};
    cam.up = Vector3{0.0f, 1.0f, 0.0f};
    cam.fovy = 60.0f;
    cam.projection = CAMERA_PERSPECTIVE;

    float vp[16], model[16], mvp[16];
    BuildViewProj(cam, vp);
    BuildModelMatrix(model, Vector3{1.0f, 2.0f, 3.0f}, Vector3{10.0f, 20.0f, 30.0f}, Vector3{1.0f, 1.0f, 1.0f});
    Frustum_Mult4x4(mvp, vp, model);

    static const float testV[3][3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f, 0.5f}};
    MATRIX m;
    std::memcpy(m, mvp, sizeof(MATRIX));
    VECTOR in[3], out[3];
    for (int i = 0; i < 3; ++i)
    {
        in[i][0] = testV[i][0];
        in[i][1] = testV[i][1];
        in[i][2] = testV[i][2];
        in[i][3] = 1.0f;
    }
    calculate_vertices(out, 3, in, m);

    auto close = [](float a, float b)
    {
        float d = a - b;
        if (d < 0.0f)
            d = -d;
        float ma = (a < 0.0f) ? -a : a;
        return d <= 0.01f * (1.0f + ma);
    };
    bool match = true;
    for (int i = 0; i < 3 && match; ++i)
    {
        const float x = testV[i][0], y = testV[i][1], z = testV[i][2];
        const float cx = mvp[0] * x + mvp[4] * y + mvp[8] * z + mvp[12];
        const float cy = mvp[1] * x + mvp[5] * y + mvp[9] * z + mvp[13];
        const float cz = mvp[2] * x + mvp[6] * y + mvp[10] * z + mvp[14];
        const float cw = mvp[3] * x + mvp[7] * y + mvp[11] * z + mvp[15];
        if (cw <= TAG_W_EPS)
        {
            match = false;
            break;
        }
        if (!(close(out[i][0], cx / cw) && close(out[i][1], cy / cw) && close(out[i][2], cz / cw) && close(out[i][3], cw)))
            match = false;
    }
    m_useVu0 = match;
    Engine_LogInfo("GifTagRenderer: VU0 batch transform %s (self-test %s).", match ? "ENABLED" : "disabled", match ? "passed" : "failed");
}

void GifTagRenderer::TransformStrip(const float mvp[16], const float* verts, int components, uint32_t count)
{
    if (m_useVu0)
    {
        MATRIX m;
        std::memcpy(m, mvp, sizeof(MATRIX));
        VECTOR* clip = static_cast<VECTOR*>(m_clipBatch);
        VECTOR* vin = static_cast<VECTOR*>(m_vecBatch);

        for (uint32_t base = 0; base < count; base += GFX_GIFTAG_XFORM_BATCH)
        {
            const uint32_t n = (count - base < GFX_GIFTAG_XFORM_BATCH) ? (count - base) : GFX_GIFTAG_XFORM_BATCH;
            VECTOR* input;
            if (components == 4)
            {
                input = reinterpret_cast<VECTOR*>(const_cast<float*>(verts + static_cast<size_t>(base) * 4));
            }
            else
            {
                for (uint32_t j = 0; j < n; ++j)
                {
                    const float* v = verts + static_cast<size_t>(base + j) * components;
                    vin[j][0] = v[0];
                    vin[j][1] = v[1];
                    vin[j][2] = v[2];
                    vin[j][3] = 1.0f;
                }
                input = vin;
            }

            calculate_vertices(clip, static_cast<int>(n), input, m);

            for (uint32_t j = 0; j < n; ++j)
            {
                const float clipW = clip[j][3];
                if (clipW <= TAG_W_EPS)
                {
                    m_q[base + j] = 0.0f;
                    continue;
                }
                const float inv = 1.0f / clipW;
                const float sx = clip[j][0] * (GFX_SCREEN_WIDTH * 0.5f) + GFX_GIFTAG_GS_ORIGIN;
                const float sy = -clip[j][1] * (GFX_SCREEN_HEIGHT * 0.5f) + GFX_GIFTAG_GS_ORIGIN;
                if (!GsCoordVisible(sx, sy))
                {
                    m_q[base + j] = 0.0f;
                    continue;
                }
                m_xyz[base + j].x = GsFix4(sx);
                m_xyz[base + j].y = GsFix4(sy);
                m_xyz[base + j].z = GsDepth(clip[j][2] * 0.5f + 0.5f);
                m_q[base + j] = inv;
            }
        }
        return;
    }

    for (uint32_t i = 0; i < count; ++i)
    {
        const float* v = verts + static_cast<size_t>(i) * components;
        const float x = v[0], y = v[1], z = v[2];
        const float clipX = mvp[0] * x + mvp[4] * y + mvp[8] * z + mvp[12];
        const float clipY = mvp[1] * x + mvp[5] * y + mvp[9] * z + mvp[13];
        const float clipZ = mvp[2] * x + mvp[6] * y + mvp[10] * z + mvp[14];
        const float clipW = mvp[3] * x + mvp[7] * y + mvp[11] * z + mvp[15];
        if (clipW <= TAG_W_EPS)
        {
            m_q[i] = 0.0f;
            continue;
        }
        const float inv = 1.0f / clipW;
        const float sx = clipX * inv * (GFX_SCREEN_WIDTH * 0.5f) + GFX_GIFTAG_GS_ORIGIN;
        const float sy = -clipY * inv * (GFX_SCREEN_HEIGHT * 0.5f) + GFX_GIFTAG_GS_ORIGIN;
        if (!GsCoordVisible(sx, sy))
        {
            m_q[i] = 0.0f;
            continue;
        }
        m_xyz[i].x = GsFix4(sx);
        m_xyz[i].y = GsFix4(sy);
        m_xyz[i].z = GsDepth(clipZ * inv * 0.5f + 0.5f);
        m_q[i] = inv;
    }
}

void GifTagRenderer::DrawStrip(const float mvp[16], const float* verts, int components, const float* uvs, uint32_t vertexCount, Color3 color, uint32_t textureId)
{
    if (!verts || vertexCount < 3)
        return;

    if (static_cast<uint32_t>(m_frameVertsUsed) + vertexCount > static_cast<uint32_t>(GFX_GIFTAG_MAX_VERTS) || !PacketHasSpace(WorstCaseQwords(vertexCount) + BindCostQwords(textureId)))
    {
        ++m_frameDroppedObjects;
        m_frameStats.trisCulled += vertexCount - 2;
        return;
    }

    const bool textured = (uvs != nullptr && textureId != 0);

    TransformStrip(mvp, verts, components, vertexCount);
    m_frameStats.vertsTransformed += vertexCount;

    if (textured)
        BindTexture(textureId);

    const float colorScale = textured ? 128.0f : 255.0f;
    const uint8_t r = static_cast<uint8_t>(color.r * colorScale);
    const uint8_t g = static_cast<uint8_t>(color.g * colorScale);
    const uint8_t b = static_cast<uint8_t>(color.b * colorScale);
    const uint64_t rgbaLo = static_cast<uint64_t>(r) | (static_cast<uint64_t>(g) << 8) | (static_cast<uint64_t>(b) << 16) | (static_cast<uint64_t>(0x80) << 24);

    const uint32_t prim = PRIM_TRIANGLE_STRIP | PRIM_BIT_IIP | (textured ? PRIM_BIT_TME : 0u);
    const uint32_t nreg = textured ? 3u : 2u;
    const uint64_t reglist = textured ? (GSREG_ST | (GSREG_RGBAQ << 4) | (GSREG_XYZ2 << 8)) : (GSREG_RGBAQ | (GSREG_XYZ2 << 4));

    uint32_t emittedTotal = 0;
    uint32_t submittedTris = 0;
    uint32_t i = 0;
    while (i < vertexCount)
    {
        while (i < vertexCount && m_q[i] <= 0.0f)
            ++i;
        const uint32_t start = i;
        while (i < vertexCount && m_q[i] > 0.0f)
            ++i;
        const uint32_t runLen = i - start;
        if (runLen < 3)
            continue;

        if (!PacketHasSpace(1u + GFX_GIFTAG_PRIM_QW + (runLen * nreg + 1u) / 2u))
        {
            ++m_frameDroppedObjects;
            break;
        }

        EmitPrim(prim);
        packet2_add_u64(m_geom, GifTagLo(runLen, nreg));
        packet2_add_u64(m_geom, reglist);
        for (uint32_t k = start; k < i; ++k)
        {
            const float q = m_q[k];
            if (textured)
            {
                const float* uv = uvs + k * 2;
                packet2_add_u64(m_geom, FloatBits(uv[0] * q) | (FloatBits(uv[1] * q) << 32));
            }
            packet2_add_u64(m_geom, rgbaLo | (FloatBits(q) << 32));
            uint64_t xyzWord;
            std::memcpy(&xyzWord, &m_xyz[k], sizeof(xyzWord));
            packet2_add_u64(m_geom, xyzWord);
        }
        if (((runLen * nreg) & 1u) != 0u)
            packet2_add_u64(m_geom, 0);

        emittedTotal += runLen;
        submittedTris += runLen - 2;
    }

    m_frameVertsUsed += emittedTotal;
    m_frameStats.trisSubmitted += submittedTris;
    m_frameStats.trisCulled += (vertexCount - 2) - submittedTris;
}

void GifTagRenderer::BindTexture(uint32_t textureId)
{
    if (textureId == 0 || textureId > TAG_MAX_TEXTURES)
        return;
    const TexEntry& t = m_textures[textureId - 1];
    if (!t.inUse)
        return;

    if (textureId == m_lastBoundTex)
        return;
    m_lastBoundTex = textureId;

    ++m_frameStats.texBinds;

    texbuffer_t tb;
    tb.address = t.gsAddr;
    tb.width = (t.width < 64) ? 64 : t.width;
    tb.psm = t.psm;
    tb.info.width = draw_log2(t.width);
    tb.info.height = draw_log2(t.height);
    tb.info.components = TEXTURE_COMPONENTS_RGBA;
    tb.info.function = TEXTURE_FUNCTION_MODULATE;

    const uint8_t maxLevel = (t.mipCount > 4) ? 3 : ((t.mipCount > 0) ? t.mipCount - 1 : 0);
    const bool mipped = (maxLevel > 0);

    lod_t lod;
    lod.calculation = mipped ? LOD_FORMULAIC : LOD_USE_K;
    lod.max_level = maxLevel;
    const bool nearest = (t.filter == TextureFilter::Nearest);
    lod.mag_filter = nearest ? LOD_MAG_NEAREST : LOD_MAG_LINEAR;
    lod.min_filter = nearest ? LOD_MIN_NEAREST : (mipped ? LOD_MIN_LINE_MIPMAP_LINE : LOD_MIN_LINEAR);
    lod.mipmap_select = LOD_MIPMAP_REGISTER;
    lod.l = 0;
    lod.k = 0.0f;

    packet2_update(m_geom, draw_texture_sampling(m_geom->next, 0, &lod));

    texwrap_t wrap;
    wrap.horizontal = WRAP_REPEAT;
    wrap.vertical = WRAP_REPEAT;
    wrap.minu = 0;
    wrap.maxu = 0;
    wrap.minv = 0;
    wrap.maxv = 0;
    packet2_update(m_geom, draw_texture_wrapping(m_geom->next, 0, &wrap));

    clutbuffer_t clut;
    clut.address = 0;
    clut.psm = 0;
    clut.storage_mode = CLUT_STORAGE_MODE1;
    clut.start = 0;
    clut.load_method = CLUT_NO_LOAD;
    if (t.psm == GS_PSM_8 && t.clutAddr != 0u)
    {
        clut.address = t.clutAddr;
        clut.psm = GS_PSM_32;
        clut.load_method = CLUT_LOAD;
    }
    packet2_update(m_geom, draw_texturebuffer(m_geom->next, 0, &tb, &clut));

    packet2_add_u64(m_geom, GifTagAd(1));
    packet2_add_u64(m_geom, GIF_REG_AD);
    packet2_add_u64(m_geom, 0);
    packet2_add_u64(m_geom, GS_REG_TEXFLUSH);

    if (mipped)
    {
        mipmap_t mm;
        mm.address1 = static_cast<int>(t.mipAddr[1]);
        mm.address2 = static_cast<int>((maxLevel >= 2) ? t.mipAddr[2] : t.mipAddr[1]);
        mm.address3 = static_cast<int>((maxLevel >= 3) ? t.mipAddr[3] : t.mipAddr[1]);
        mm.width1 = static_cast<char>(((t.width >> 1) + 63) / 64);
        mm.width2 = static_cast<char>(((t.width >> 2) + 63) / 64);
        mm.width3 = static_cast<char>(((t.width >> 3) + 63) / 64);
        packet2_update(m_geom, draw_mipmap1(m_geom->next, 0, &mm));
    }
}

void GifTagRenderer::RenderSkybox(const DrawLists& lists)
{
    if (lists.GetSkyboxResourceId() == -1)
        return;

    const auto* tex = static_cast<const Texture2D*>(Engine_Resource_Get(lists.GetSkyboxResourceId()));
    if (!tex || tex->id == 0)
        return;

    const Camera3D& camera = lists.GetCamera3D();
    float vp[16], model[16], mvp[16];
    BuildViewProj(camera, vp);
    BuildModelMatrix(model, camera.position, Vector3{0, 0, 0}, Vector3{500.0f, 500.0f, 500.0f});
    Frustum_Mult4x4(mvp, vp, model);

    const bool savedCull = m_backfaceCull;
    m_backfaceCull = false;
    const PrimitiveArrays cube = lists.GetPrimitiveArrays(Primitive3D::Cube);
    DrawTriangles(mvp, cube.verts, 3, cube.uvs, cube.vertexCount, Color3{1.0f, 1.0f, 1.0f}, tex->id);
    m_backfaceCull = savedCull;
}

void GifTagRenderer::RenderPrimitives(DrawLists& lists) { UNUSED_VAR(lists); }

void GifTagRenderer::RenderModels(const DrawLists& lists)
{
    const uint16_t mCount = lists.GetModelCount();
    if (mCount == 0)
        return;

    float vp[16];
    BuildViewProj(lists.GetCamera3D(), vp);
    FrustumPlanes frustum;
    Frustum_FromViewProj(&frustum, vp);

    const ModelDrawEntry* entries = lists.GetModels();
    uint16_t meshDraws = 0;

    for (uint16_t i = 0; i < mCount; ++i)
    {
        const ModelDrawEntry& entry = entries[i];
        const auto* model = static_cast<const Model*>(Engine_Resource_Get(entry.resourceId));
        if (!model)
            continue;

        Vector3 wc;
        float wr;
        Frustum_WorldSphere(entry.transform.GetPosition(), entry.transform.GetScale(), model->boundsCenter, model->boundsRadius, &wc, &wr);
        if (!Frustum_SphereVisible(&frustum, wc, wr))
        {
            ++m_frameStats.entriesCulled;
            continue;
        }

        float mm[16], mvp[16];
        BuildModelMatrix(mm, entry.transform.GetPosition(), entry.transform.GetRotation(), entry.transform.GetScale());
        Frustum_Mult4x4(mvp, vp, mm);

        for (int meshIdx = 0; meshIdx < model->meshCount; ++meshIdx)
        {
            const Mesh& mesh = model->meshes[meshIdx];
            if (mesh.indices != nullptr || !mesh.vertices || mesh.vertexCount == 0)
                continue;

            uint32_t texId = 0;
            const int matIdx = (model->meshMaterial) ? model->meshMaterial[meshIdx] : 0;
            if (model->materials)
            {
                const int32_t texResId = model->materials[matIdx].maps[MATERIAL_MAP_DIFFUSE].textureResourceId;
                if (texResId >= 0)
                {
                    const auto* t = static_cast<const Texture2D*>(Engine_Resource_Get(texResId));
                    if (t)
                        texId = t->id;
                }
            }

            const int components = (mesh.vertexComponents == 4) ? 4 : 3;
            const float* uv = texId ? mesh.texcoords : nullptr;
            if (mesh.topology == MESH_TOPOLOGY_STRIP)
                DrawStrip(mvp, mesh.vertices, components, uv, static_cast<uint32_t>(mesh.vertexCount), Color3{1.0f, 1.0f, 1.0f}, texId);
            else
                DrawTriangles(mvp, mesh.vertices, components, uv, static_cast<uint32_t>(mesh.vertexCount), Color3{1.0f, 1.0f, 1.0f}, texId);
            ++meshDraws;
        }
    }

    m_frameStats.modelCount = meshDraws;
}

// ---------------------------------------------------------------------------
// Render-to-image (Ui_Image3D)
// ---------------------------------------------------------------------------

uint32_t GifTagRenderer::RenderToImage3D(const Renderable3D& what, const Camera3D& camera, int width, int height, const Color3& clearColor)
{
    if (!m_initialized || m_imageColorAddr == 0)
        return 0;

    const Model* model = nullptr;
    if (what.modelId >= 0)
    {
        model = static_cast<const Model*>(Engine_Resource_Get(what.modelId));
        if (!model)
            return 0;
    }

    if (m_imageTextureSlot < 0)
    {
        for (int i = 0; i < TAG_MAX_TEXTURES; ++i)
        {
            if (!m_textures[i].inUse)
            {
                m_imageTextureSlot = i;
                break;
            }
        }
        if (m_imageTextureSlot < 0)
        {
            Engine_LogError("GifTagRenderer: texture registry full (%u), no slot for the image target", TAG_MAX_TEXTURES);
            return 0;
        }
    }

    // The caller's width/height only shape the camera's aspect -- the render
    // always lands at the fixed GFX_PS2_IMAGE_TARGET_SIZE reservation; see the
    // constant's own comment.
    float view[16], proj[16], vp[16];
    Frustum_BuildLookAt(view, camera);
    const float aspect = (height > 0) ? (static_cast<float>(width) / static_cast<float>(height)) : 1.0f;
    Frustum_BuildPerspective(proj, camera.fovy, aspect, GFX_NEAR_PLANE, GFX_FAR_PLANE);
    Frustum_Mult4x4(vp, proj, view);

    // A dedicated packet, built and drained here rather than through m_geom:
    // the frame's own packet may already hold this frame's ordinary
    // submission, and unlike that one, this must complete before returning.
    packet2_t* scratch = packet2_create(GFX_GIFTAG_IMAGE_PACKET_QWORDS, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
    if (!scratch)
    {
        Engine_LogError("GifTagRenderer: out of memory for the image-target packet");
        return 0;
    }

    framebuffer_t imgFrame;
    imgFrame.address = m_imageColorAddr;
    imgFrame.width = GFX_PS2_IMAGE_TARGET_SIZE;
    imgFrame.height = GFX_PS2_IMAGE_TARGET_SIZE;
    imgFrame.mask = 0;
    imgFrame.psm = GS_PSM_32;

    zbuffer_t imgZ;
    imgZ.enable = DRAW_ENABLE;
    imgZ.method = ZTEST_METHOD_GREATER_EQUAL;
    imgZ.zsm = GS_ZBUF_32;
    imgZ.mask = 0;
    imgZ.address = m_imageDepthAddr;

    const float offsetX = GFX_GIFTAG_GS_ORIGIN - GFX_PS2_IMAGE_TARGET_SIZE * 0.5f;
    const float offsetY = GFX_GIFTAG_GS_ORIGIN - GFX_PS2_IMAGE_TARGET_SIZE * 0.5f;

    packet2_update(scratch, draw_setup_environment(scratch->next, 0, &imgFrame, &imgZ));
    packet2_update(scratch, draw_primitive_xyoffset(scratch->next, 0, offsetX, offsetY));

    blend_t blend;
    blend.color1 = BLEND_COLOR_SOURCE;
    blend.color2 = BLEND_COLOR_DEST;
    blend.alpha = BLEND_ALPHA_SOURCE;
    blend.color3 = BLEND_COLOR_DEST;
    blend.fixed_alpha = 0x80;
    packet2_update(scratch, draw_alpha_blending(scratch->next, 0, &blend));

    packet2_update(scratch, draw_disable_tests(scratch->next, 0, &imgZ));
    packet2_update(scratch, draw_clear(scratch->next, 0, offsetX, offsetY, GFX_PS2_IMAGE_TARGET_SIZE, GFX_PS2_IMAGE_TARGET_SIZE, static_cast<int>(clearColor.r * 255.0f),
                                        static_cast<int>(clearColor.g * 255.0f), static_cast<int>(clearColor.b * 255.0f)));
    packet2_update(scratch, draw_enable_tests(scratch->next, 0, &imgZ));

    // DrawTriangles/DrawStrip/BindTexture all write through m_geom, so
    // redirecting it here reuses their whole transform/clip/bind pipeline
    // unchanged instead of a second copy of it.
    packet2_t* const savedGeom = m_geom;
    const uint32_t savedLastBoundTex = m_lastBoundTex;
    m_geom = scratch;
    m_lastBoundTex = 0;

    float mm[16], mvp[16];
    BuildModelMatrix(mm, Vector3{0.0f, 0.0f, 0.0f}, what.rotation, what.scale);
    Frustum_Mult4x4(mvp, vp, mm);

    if (model)
    {
        for (int meshIdx = 0; meshIdx < model->meshCount; ++meshIdx)
        {
            const Mesh& mesh = model->meshes[meshIdx];
            if (mesh.indices != nullptr || !mesh.vertices || mesh.vertexCount == 0)
                continue;

            uint32_t texId = 0;
            const int matIdx = model->meshMaterial ? model->meshMaterial[meshIdx] : 0;
            if (model->materials && matIdx < model->materialCount)
            {
                const int32_t texResId = model->materials[matIdx].maps[MATERIAL_MAP_DIFFUSE].textureResourceId;
                if (texResId >= 0)
                {
                    const auto* t = static_cast<const Texture2D*>(Engine_Resource_Get(texResId));
                    if (t)
                        texId = t->id;
                }
            }

            const int components = (mesh.vertexComponents == 4) ? 4 : 3;
            const float* uv = texId ? mesh.texcoords : nullptr;
            if (mesh.topology == MESH_TOPOLOGY_STRIP)
                DrawStrip(mvp, mesh.vertices, components, uv, static_cast<uint32_t>(mesh.vertexCount), Color3{1.0f, 1.0f, 1.0f}, texId);
            else
                DrawTriangles(mvp, mesh.vertices, components, uv, static_cast<uint32_t>(mesh.vertexCount), Color3{1.0f, 1.0f, 1.0f}, texId);
        }
    }
    else
    {
        const PrimitiveArrays geo = m_drawLists.GetPrimitiveArrays(what.primitive);
        if (geo.verts && geo.vertexCount > 0)
        {
            uint32_t texId = 0;
            if (what.textureId >= 0)
            {
                const auto* t = static_cast<const Texture2D*>(Engine_Resource_Get(what.textureId));
                if (t)
                    texId = t->id;
            }
            DrawTriangles(mvp, geo.verts, 3, texId ? geo.uvs : nullptr, geo.vertexCount, what.color, texId);
        }
    }

    packet2_update(scratch, draw_finish(scratch->next));

    m_geom = savedGeom;
    m_lastBoundTex = savedLastBoundTex;

    FlushCache(0);
    dma_channel_send_packet2(scratch, DMA_CHANNEL_GIF, 1);
    // Must complete before returning: the result is sampled by the UI this
    // same frame, which has no later point to pick it up. Unlike the frame's
    // own double-buffered packets, this one is not left in flight.
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    draw_wait_finish();
    packet2_free(scratch);

    TexEntry& entry = m_textures[m_imageTextureSlot];
    entry.inUse = true;
    entry.gsAddr = m_imageColorAddr;
    entry.mipCount = 1;
    entry.clutAddr = 0;
    entry.vramBase = 0; // not owned by VramAlloc/VramFree; never released through them
    entry.width = GFX_PS2_IMAGE_TARGET_SIZE;
    entry.height = GFX_PS2_IMAGE_TARGET_SIZE;
    entry.psm = GS_PSM_32;
    entry.filter = TextureFilter::Linear;

    return static_cast<uint32_t>(m_imageTextureSlot) + 1u;
}

namespace
{
    inline uint32_t Align64(uint32_t words) { return (words + 63u) & ~63u; }
} // namespace

uint32_t GifTagRenderer::VramAlloc(uint32_t words)
{
    words = Align64(words);
    if (words == 0)
        return 0;

    for (int i = 0; i < m_vramExtentCount; ++i)
    {
        VramExtent& e = m_vramExtents[i];
        if (e.used || e.words < words)
            continue;

        const uint32_t addr = e.addr;
        if (e.words == words)
        {
            e.used = true;
            return addr;
        }
        if (m_vramExtentCount >= TAG_MAX_VRAM_EXTENTS)
            return 0;
        e.addr += words;
        e.words -= words;
        VramExtent& used = m_vramExtents[m_vramExtentCount++];
        used.addr = addr;
        used.words = words;
        used.used = true;
        return addr;
    }
    return 0;
}

void GifTagRenderer::VramFree(uint32_t addr)
{
    if (addr == 0)
        return;
    for (int i = 0; i < m_vramExtentCount; ++i)
    {
        if (m_vramExtents[i].addr == addr && m_vramExtents[i].used)
        {
            m_vramExtents[i].used = false;
            break;
        }
    }
    bool merged = true;
    while (merged)
    {
        merged = false;
        for (int i = 0; i < m_vramExtentCount; ++i)
        {
            if (m_vramExtents[i].used)
                continue;
            for (int j = 0; j < m_vramExtentCount; ++j)
            {
                if (i == j || m_vramExtents[j].used)
                    continue;
                if (m_vramExtents[i].addr + m_vramExtents[i].words == m_vramExtents[j].addr)
                {
                    m_vramExtents[i].words += m_vramExtents[j].words;
                    m_vramExtents[j] = m_vramExtents[--m_vramExtentCount];
                    merged = true;
                    break;
                }
            }
            if (merged)
                break;
        }
    }
}

uint32_t GifTagRenderer::UploadTexture(const TextureUpload& upload)
{
    int slot = -1;
    for (int i = 0; i < TAG_MAX_TEXTURES; ++i)
    {
        if (!m_textures[i].inUse)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
    {
        Engine_LogError("GifTagRenderer: texture registry full (%u).", TAG_MAX_TEXTURES);
        return 0;
    }

    const int width = upload.width;
    const int height = upload.height;
    int psm;
    switch (upload.format)
    {
    case PixelFormat::RGBA16:
        psm = GS_PSM_16;
        break;
    case PixelFormat::PAL8:
        psm = GS_PSM_8;
        break;
    default:
        psm = GS_PSM_32;
        break;
    }

    uint8_t mipCount = (upload.mipCount == 0) ? 1 : upload.mipCount;
    if (mipCount > TEX_MAX_MIP_LEVELS)
    {
        Engine_LogError("GifTagRenderer: texture declares %u mip levels, capped at %u.", upload.mipCount, TEX_MAX_MIP_LEVELS);
        mipCount = TEX_MAX_MIP_LEVELS;
    }
    while (mipCount > 1 && upload.levelPtr[mipCount - 1] == nullptr)
        --mipCount;
    if (upload.levelPtr[0] == nullptr)
    {
        Engine_LogError("GifTagRenderer: texture upload has no level 0 pixel data.");
        return 0;
    }
    const bool hasClut = (upload.format == PixelFormat::PAL8 && upload.clut != nullptr);

    uint32_t totalWords = 0;
    uint32_t levelWords[TEX_MAX_MIP_LEVELS] = {0};
    for (uint8_t lvl = 0; lvl < mipCount; ++lvl)
    {
        const int w = (width >> lvl) ? (width >> lvl) : 1;
        const int h = (height >> lvl) ? (height >> lvl) : 1;
        levelWords[lvl] = Align64(static_cast<uint32_t>(graph_vram_size(w, h, psm, GRAPH_ALIGN_BLOCK)));
        totalWords += levelWords[lvl];
    }
    const uint32_t clutWords = hasClut ? Align64(static_cast<uint32_t>(graph_vram_size(16, 16, GS_PSM_32, GRAPH_ALIGN_BLOCK))) : 0u;
    totalWords += clutWords;

    const uint32_t base = VramAlloc(totalWords);
    if (base == 0)
    {
        Engine_LogError("GifTagRenderer: texture VRAM heap full (need %u words).", totalWords);
        return 0;
    }

    TexEntry& te = m_textures[slot];
    te = TexEntry{};
    te.vramBase = base;

    dma_channel_wait(DMA_CHANNEL_GIF, 0);

    uint32_t offset = 0;
    for (uint8_t lvl = 0; lvl < mipCount; ++lvl)
    {
        const int w = (width >> lvl) ? (width >> lvl) : 1;
        const int h = (height >> lvl) ? (height >> lvl) : 1;
        const uint32_t addr = base + offset;
        te.mipAddr[lvl] = addr;
        offset += levelWords[lvl];

        packet2_reset(m_env, 0);
        packet2_update(m_env, draw_texture_transfer(m_env->next, const_cast<void*>(upload.levelPtr[lvl]), w, h, psm, static_cast<int>(addr), w));
        packet2_update(m_env, draw_texture_flush(m_env->next));
        dma_channel_send_packet2(m_env, DMA_CHANNEL_GIF, 1);
        dma_channel_wait(DMA_CHANNEL_GIF, 0);
    }

    if (hasClut)
    {
        const uint32_t clutAddr = base + offset;
        const uint32_t* src = static_cast<const uint32_t*>(upload.clut);
        uint32_t swz[256] __attribute__((aligned(16)));
        for (int i = 0; i < 256; ++i)
        {
            const int j = (i & 0xE7) | ((i & 0x10) >> 1) | ((i & 0x08) << 1);
            swz[j] = src[i];
        }
        packet2_reset(m_env, 0);
        packet2_update(m_env, draw_texture_transfer(m_env->next, swz, 16, 16, GS_PSM_32, static_cast<int>(clutAddr), 64));
        packet2_update(m_env, draw_texture_flush(m_env->next));
        dma_channel_send_packet2(m_env, DMA_CHANNEL_GIF, 1);
        dma_channel_wait(DMA_CHANNEL_GIF, 0);
        te.clutAddr = clutAddr;
    }

    te.inUse = true;
    te.gsAddr = te.mipAddr[0];
    te.mipCount = mipCount;
    te.width = width;
    te.height = height;
    te.psm = psm;
    te.filter = upload.filter;

    return static_cast<uint32_t>(slot + 1);
}

void GifTagRenderer::ReleaseTexture(uint32_t handle)
{
    if (handle == 0 || handle > TAG_MAX_TEXTURES)
        return;
    TexEntry& te = m_textures[handle - 1];
    if (te.inUse)
        VramFree(te.vramBase);
    te.inUse = false;
    if (m_lastBoundTex == handle)
        m_lastBoundTex = 0;
}

void GifTagRenderer::SetCamera3D(CameraID id, const Camera3D& camera) { m_drawLists.SetCamera3D(id, camera); }
void GifTagRenderer::SetActiveCamera3D(CameraID id) { m_drawLists.SetActiveCamera3D(id); }
void GifTagRenderer::SetActiveCamera2D(const Camera2D& camera) { m_drawLists.SetActiveCamera2D(camera); }

uint32_t GifTagRenderer::GetTextureBudgetBytes() const { return m_texHeapWords * 4u; }

bool GifTagRenderer::IsInitialized() const { return m_initialized; }
DrawStats GifTagRenderer::GetLastStats() const { return m_drawLists.GetLastStats(); }
Camera3D GifTagRenderer::GetActiveCamera3D() const { return m_drawLists.GetCamera3D(); }
