#include "platform/psp/renderer/Gu.h"

#include <cstdlib>
#include <cstring>
#include <malloc.h>

#include "Macros.h"
#include "PlatformConstants.h"
#include "core/EngineDebug.h"
#include "core/EngineMemory.h"
#include "graphics/TextureExpand.h"
#include "platform/Platform.h"
#include "platform/psp/UtilityDialog.h"

extern "C" {
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspkernel.h>
}

namespace
{
    // Video memory is handed out by bumping an offset: the three surfaces are
    // placed once, at construction, and never released.
    uint32_t s_VramOffset = 0;

    uint32_t SurfaceBytes(uint32_t bytesPerPixel) { return GFX_PSP_BUFFER_STRIDE * GFX_SCREEN_HEIGHT * bytesPerPixel; }

    void* TakeVram(uint32_t bytesPerPixel)
    {
        void* result = reinterpret_cast<void*>(static_cast<uintptr_t>(s_VramOffset));
        s_VramOffset += SurfaceBytes(bytesPerPixel);
        return result;
    }

    uint32_t PackColor(float r, float g, float b, float a)
    {
        auto channel = [](float v) -> uint32_t
        {
            const float scaled = v * 255.0f;
            if (scaled <= 0.0f)
                return 0u;
            if (scaled >= 255.0f)
                return 255u;
            return static_cast<uint32_t>(scaled);
        };
        return channel(r) | (channel(g) << 8) | (channel(b) << 16) | (channel(a) << 24);
    }

    uint32_t BytesPerPixelFor(PixelFormat format)
    {
        if (format == PixelFormat::PAL8)
            return 1u;
        if (format == PixelFormat::RGBA16)
            return 2u;
        return 4u;
    }

    int GuFormatFor(PixelFormat format)
    {
        if (format == PixelFormat::PAL8)
            return GU_PSM_T8;
        if (format == PixelFormat::RGBA16)
            return GU_PSM_5551;
        return GU_PSM_8888;
    }
} // namespace

GuRenderer::GuRenderer(const EngineConfig& config) :
    m_drawBuffer(nullptr), m_dispBuffer(nullptr), m_depthBuffer(nullptr), m_listIndex(0), m_vertices(nullptr), m_vertexCapacity(0), m_count3D(0), m_count2D(0), m_whiteTexture(0), m_geometry(),
    m_clearColor(Color3{0.0f, 0.0f, 0.0f}), m_width(GFX_SCREEN_WIDTH), m_height(GFX_SCREEN_HEIGHT), m_frameStats(), m_initialized(false), m_imageDisplayList(nullptr), m_imageColorBuffer(nullptr),
    m_imageDepthBuffer(nullptr), m_imageWidth(0), m_imageHeight(0), m_imageTextureSlot(-1)
{
    UNUSED_VAR(config);
    memset(m_textures, 0, sizeof(m_textures));
    memset(m_displayList, 0, sizeof(m_displayList));

    float* arena = static_cast<float*>(Engine_GetSlot(ARENA_RENDERER, 0));
    if (!arena)
    {
        Engine_LogError("GuRenderer: failed to retrieve ARENA_RENDERER slot 0");
        return;
    }
    m_drawLists.Init(arena);

    if (!AllocateBuffers())
        return;

    s_VramOffset = 0;
    m_drawBuffer = TakeVram(GFX_PSP_DISPLAY_BPP);
    m_dispBuffer = TakeVram(GFX_PSP_DISPLAY_BPP);
    m_depthBuffer = TakeVram(GFX_PSP_DEPTH_BPP);

    sceGuInit();
    sceGuStart(GU_DIRECT, m_displayList[0]);

    sceGuDrawBuffer(GU_PSM_8888, m_drawBuffer, GFX_PSP_BUFFER_STRIDE);
    sceGuDispBuffer(GFX_SCREEN_WIDTH, GFX_SCREEN_HEIGHT, m_dispBuffer, GFX_PSP_BUFFER_STRIDE);
    sceGuDepthBuffer(m_depthBuffer, GFX_PSP_BUFFER_STRIDE);

    sceGuOffset(2048 - (GFX_SCREEN_WIDTH / 2), 2048 - (GFX_SCREEN_HEIGHT / 2));
    sceGuViewport(2048, 2048, GFX_SCREEN_WIDTH, GFX_SCREEN_HEIGHT);
    sceGuScissor(0, 0, GFX_SCREEN_WIDTH, GFX_SCREEN_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);

    // The hardware depth range runs backwards relative to every other platform
    // here, which is why the comparison is GEQUAL rather than LEQUAL.
    sceGuDepthRange(65535, 0);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);

    sceGuFrontFace(GU_CCW);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuEnable(GU_BLEND);

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    if (!CreateWhiteTexture())
    {
        Engine_LogError("GuRenderer: could not create the untextured fallback");
        return;
    }

    m_initialized = true;
    Engine_LogInfo("GuRenderer: ready (%ux%u, %u KB video memory free)", m_width, m_height, (GFX_PSP_VRAM_BYTES - s_VramOffset) / 1024u);
}

bool GuRenderer::AllocateBuffers()
{
    for (uint32_t i = 0; i < GFX_PSP_DISPLAY_LIST_BUFFERS; ++i)
    {
        m_displayList[i] = memalign(16, GFX_PSP_DISPLAY_LIST_BYTES);
        if (!m_displayList[i])
        {
            Engine_LogError("GuRenderer: could not allocate display list %u (%u KB)", i, GFX_PSP_DISPLAY_LIST_BYTES / 1024u);
            return false;
        }
    }

    // Screen-space work gets its own span sized from the interface budget,
    // rather than a fraction of the world span it would otherwise compete with.
    m_vertexCapacity = GFX_PSP_MAX_FRAME_VERTICES + GFX_PSP_MAX_2D_VERTICES;
    m_vertices = static_cast<GuVertex*>(memalign(16, m_vertexCapacity * sizeof(GuVertex)));
    if (!m_vertices)
    {
        Engine_LogError("GuRenderer: could not allocate %u KB of vertex storage", static_cast<unsigned>(m_vertexCapacity * sizeof(GuVertex) / 1024u));
        return false;
    }
    return true;
}

bool GuRenderer::CreateWhiteTexture()
{
    TextureUpload upload;
    memset(&upload, 0, sizeof(upload));

    static const uint32_t white[16] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
                                       0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};

    upload.levelPtr[0] = white;
    upload.mipCount = 1;
    upload.width = 4;
    upload.height = 4;
    upload.format = PixelFormat::RGBA32;
    upload.filter = TextureFilter::Nearest;
    upload.clut = nullptr;

    m_whiteTexture = UploadTexture(upload);
    return m_whiteTexture != 0;
}

uint32_t GuRenderer::UploadTexture(const TextureUpload& upload)
{
    const uint32_t width = static_cast<uint32_t>(upload.width);
    const uint32_t height = static_cast<uint32_t>(upload.height);
    if (width == 0 || height == 0 || !upload.levelPtr[0])
        return 0;

    if (width > GFX_MAX_TEXTURE_WIDTH || height > GFX_MAX_TEXTURE_HEIGHT)
    {
        Engine_LogError("GuRenderer: %ux%u exceeds the %dx%d hardware sampler limit", width, height, GFX_MAX_TEXTURE_WIDTH, GFX_MAX_TEXTURE_HEIGHT);
        return 0;
    }

    int slot = -1;
    for (int i = 0; i < GU_MAX_RESIDENT_TEXTURES; ++i)
    {
        if (!m_textures[i].used)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
    {
        Engine_LogError("GuRenderer: texture registry full (%d)", GU_MAX_RESIDENT_TEXTURES);
        return 0;
    }

    const uint32_t bytes = width * height * BytesPerPixelFor(upload.format);
    void* pixels = memalign(16, bytes);
    if (!pixels)
    {
        Engine_LogError("GuRenderer: out of memory storing a %ux%u texture (%u KB)", width, height, bytes / 1024u);
        return 0;
    }
    memcpy(pixels, upload.levelPtr[0], bytes);

    // Cooked textures carry the console convention where 0x80 is fully opaque.
    // Uploaded verbatim they would every one of them draw at half alpha, which
    // with blending on renders the whole world semi-transparent.
    if (upload.format == PixelFormat::RGBA32)
    {
        uint8_t* texel = static_cast<uint8_t*>(pixels);
        for (uint32_t i = 0; i < width * height; ++i)
            texel[i * 4u + 3u] = Gfx_ExpandPs2Alpha(texel[i * 4u + 3u]);
    }

    sceKernelDcacheWritebackRange(pixels, bytes);

    void* clut = nullptr;
    if (upload.format == PixelFormat::PAL8)
    {
        if (!upload.clut)
        {
            free(pixels);
            Engine_LogError("GuRenderer: a palettised %ux%u texture arrived with no palette", width, height);
            return 0;
        }
        clut = memalign(16, 256u * 4u);
        if (!clut)
        {
            free(pixels);
            Engine_LogError("GuRenderer: out of memory storing a palette");
            return 0;
        }
        memcpy(clut, upload.clut, 256u * 4u);

        // A palettised texture carries its alpha only in the table, so the
        // rescale above costs 256 entries here however large the image is.
        uint8_t* entry = static_cast<uint8_t*>(clut);
        for (uint32_t i = 0; i < 256u; ++i)
            entry[i * 4u + 3u] = Gfx_ExpandPs2Alpha(entry[i * 4u + 3u]);

        sceKernelDcacheWritebackRange(clut, 256u * 4u);
    }

    m_textures[slot].pixels = pixels;
    m_textures[slot].clut = clut;
    m_textures[slot].width = static_cast<uint16_t>(width);
    m_textures[slot].height = static_cast<uint16_t>(height);
    m_textures[slot].format = static_cast<uint8_t>(upload.format);
    m_textures[slot].filter = static_cast<uint8_t>(upload.filter);
    m_textures[slot].used = true;

    return static_cast<uint32_t>(slot) + 1u;
}

void GuRenderer::ReleaseTexture(uint32_t handle)
{
    if (handle == 0 || handle > GU_MAX_RESIDENT_TEXTURES)
        return;

    TextureRecord& record = m_textures[handle - 1u];
    if (!record.used)
        return;

    free(record.pixels);
    if (record.clut)
        free(record.clut);
    memset(&record, 0, sizeof(record));
}

void GuRenderer::BindTexture(uint32_t handle)
{
    const uint32_t effective = (handle != 0 && handle <= GU_MAX_RESIDENT_TEXTURES && m_textures[handle - 1u].used) ? handle : m_whiteTexture;
    if (effective == 0)
        return;

    const TextureRecord& record = m_textures[effective - 1u];
    const PixelFormat format = static_cast<PixelFormat>(record.format);

    if (format == PixelFormat::PAL8)
    {
        sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
        sceGuClutLoad(32, record.clut);
    }

    sceGuTexMode(GuFormatFor(format), 0, 0, GU_FALSE);
    sceGuTexImage(0, record.width, record.height, record.width, record.pixels);
    const int filter = (static_cast<TextureFilter>(record.filter) == TextureFilter::Nearest) ? GU_NEAREST : GU_LINEAR;
    sceGuTexFilter(filter, filter);

    // The graphics engine caches texels and does not notice that the image
    // behind them changed. Without this the run keeps sampling whatever the
    // previous run bound, which is why the wrong texture appears on an object
    // only once the camera moves and reorders the runs.
    sceGuTexFlush();
}

uint32_t GuRenderer::ConvertSpan(const StagedGeometry::Vertex* src, uint32_t count, GuVertex* dst, uint32_t dstCapacity) const
{
    const uint32_t n = (count < dstCapacity) ? count : dstCapacity;
    for (uint32_t i = 0; i < n; ++i)
    {
        dst[i].u = src[i].u;
        dst[i].v = src[i].v;
        dst[i].color = PackColor(src[i].r, src[i].g, src[i].b, src[i].a);
        dst[i].x = src[i].x;
        dst[i].y = src[i].y;
        dst[i].z = src[i].z;
    }
    return n;
}

void GuRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale)
{
    AddPrimitiveToDrawList(primitive, position, rotation, scale, Color3{1.0f, 1.0f, 1.0f}, -1);
}

void GuRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, Color3 color)
{
    AddPrimitiveToDrawList(primitive, position, rotation, scale, color, -1);
}

void GuRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, int32_t textureId)
{
    AddPrimitiveToDrawList(primitive, position, rotation, scale, Color3{1.0f, 1.0f, 1.0f}, textureId);
}

void GuRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, Color3 color, int32_t textureId)
{
    PrimitiveDrawEntry entry;
    entry.transform = Transform3D(position, rotation, scale);
    entry.color = color;
    entry.type = primitive;
    entry.textureId = textureId;
    m_drawLists.AddPrimitive(entry);
}

void GuRenderer::AddLevelToDrawList(const Level& level) { UNUSED_VAR(level); }

void GuRenderer::AddModelToDrawList(int32_t modelId, const Vector3& position, const Vector3& rotation, const Vector3& scale)
{
    ModelDrawEntry entry;
    entry.resourceId = modelId;
    entry.transform = Transform3D(position, rotation, scale);
    m_drawLists.AddModel(entry);
}

void GuRenderer::AddSkyToDrawList(int32_t resourceId) { m_drawLists.SetSkyboxTexture(resourceId); }

void GuRenderer::ClearDrawLists() { m_drawLists.Reset(false); }

void GuRenderer::ClearFrame(const Color3& color) { m_clearColor = color; }

void GuRenderer::DrawQuad2D(const Quad2D& quad) { m_geometry.AddQuad2D(quad); }

void GuRenderer::DrawGrid(int32_t slices, float spacing)
{
    UNUSED_VAR(slices);
    UNUSED_VAR(spacing);
}

void GuRenderer::BeginFrame()
{
    Platform* platform = Engine_GetPlatform();
    platform->GetFramebufferSize(&m_width, &m_height);

    m_geometry.SetFrameBudget(GFX_PSP_MAX_FRAME_VERTICES, m_width, m_height);
    m_geometry.BeginFrame();
    m_frameStats = DrawStats{};
}

void GuRenderer::Render() { m_geometry.BuildFrame(m_drawLists, &m_frameStats); }

void GuRenderer::DrawStagedGeometry()
{
    const int vertexFormat = GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D;

    float matrix[16];
    ScePspFMatrix4 identity;
    memset(&identity, 0, sizeof(identity));
    identity.x.x = 1.0f;
    identity.y.y = 1.0f;
    identity.z.z = 1.0f;
    identity.w.w = 1.0f;

    sceGuSetMatrix(GU_MODEL, &identity);
    sceGuSetMatrix(GU_VIEW, &identity);

    if (m_count3D > 0)
    {
        sceGuEnable(GU_DEPTH_TEST);
        StagedGeometry::BuildViewProjection(m_drawLists.GetCamera3D(), m_width, m_height, false, matrix);
        sceGuSetMatrix(GU_PROJECTION, reinterpret_cast<const ScePspFMatrix4*>(matrix));

        const StagedGeometry::DrawRun* runs = m_geometry.Runs();
        for (uint32_t i = 0; i < m_geometry.RunCount(); ++i)
        {
            if (runs[i].first + runs[i].count > m_count3D)
                continue;
            BindTexture(runs[i].texture);
            sceGuDrawArray(GU_TRIANGLES, vertexFormat, static_cast<int>(runs[i].count), nullptr, m_vertices + runs[i].first);
        }
    }

    if (m_count2D > 0)
    {
        // Screen space blends against what is already there and never tests
        // depth, or world geometry that wrote depth underneath occludes it.
        sceGuDisable(GU_DEPTH_TEST);
        StagedGeometry::BuildOrtho2D(m_width, m_height, false, matrix);
        sceGuSetMatrix(GU_PROJECTION, reinterpret_cast<const ScePspFMatrix4*>(matrix));

        const uint32_t base = GFX_PSP_MAX_FRAME_VERTICES;
        const StagedGeometry::DrawRun* runs2D = m_geometry.Runs2D();
        for (uint32_t i = 0; i < m_geometry.RunCount2D(); ++i)
        {
            if (runs2D[i].first + runs2D[i].count > m_count2D)
                continue;
            BindTexture(runs2D[i].texture);
            sceGuDrawArray(GU_TRIANGLES, vertexFormat, static_cast<int>(runs2D[i].count), nullptr, m_vertices + base + runs2D[i].first);
        }
    }
}

void GuRenderer::EndFrame()
{
    if (!m_initialized)
        return;

    m_count3D = ConvertSpan(m_geometry.Vertices3D(), m_geometry.Count3D(), m_vertices, GFX_PSP_MAX_FRAME_VERTICES);
    m_count2D = ConvertSpan(m_geometry.Vertices2D(), m_geometry.Count2D(), m_vertices + GFX_PSP_MAX_FRAME_VERTICES, GFX_PSP_MAX_2D_VERTICES);

    // The graphics engine reads main memory directly and cannot see dirty cache
    // lines; without this the frame draws from whatever was there before. Only
    // the two spans actually written are flushed - this is the most
    // bandwidth-bound platform here, and the buffer is mostly unused most
    // frames.
    if (m_count3D > 0)
        sceKernelDcacheWritebackRange(m_vertices, m_count3D * sizeof(GuVertex));
    if (m_count2D > 0)
        sceKernelDcacheWritebackRange(m_vertices + GFX_PSP_MAX_FRAME_VERTICES, m_count2D * sizeof(GuVertex));

    sceGuStart(GU_DIRECT, m_displayList[m_listIndex]);

    sceGuClearColor(PackColor(m_clearColor.r, m_clearColor.g, m_clearColor.b, 1.0f));
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

    DrawStagedGeometry();

    sceGuFinish();
    sceGuSync(0, 0);

    PspUtilityDialog_Update();

    Platform* platform = Engine_GetPlatform();
    const double waitStart = platform->GetTimeSeconds();
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
    m_frameStats.presentWaitMs = static_cast<float>((platform->GetTimeSeconds() - waitStart) * 1000.0);

    m_listIndex = (m_listIndex + 1u) % GFX_PSP_DISPLAY_LIST_BUFFERS;

    m_geometry.EndFrame();
    m_drawLists.SetLastStats(m_frameStats);
    m_drawLists.Reset(false);
}

// ---------------------------------------------------------------------------
// Render-to-image (Ui_Image3D)
// ---------------------------------------------------------------------------

bool GuRenderer::EnsureImageTarget(int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;

    if (m_imageColorBuffer)
        return m_imageWidth == width && m_imageHeight == height;

    if (!m_imageDisplayList)
    {
        m_imageDisplayList = memalign(16, GFX_PSP_DISPLAY_LIST_BYTES);
        if (!m_imageDisplayList)
        {
            Engine_LogError("GuRenderer: could not allocate the image-target display list");
            return false;
        }
    }

    if (m_imageTextureSlot < 0)
    {
        for (int i = 0; i < GU_MAX_RESIDENT_TEXTURES; ++i)
        {
            if (!m_textures[i].used)
            {
                m_imageTextureSlot = i;
                break;
            }
        }
        if (m_imageTextureSlot < 0)
        {
            Engine_LogError("GuRenderer: texture registry full (%d), no slot for the image target", GU_MAX_RESIDENT_TEXTURES);
            return false;
        }
    }

    const uint32_t colorBytes = static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 4u;
    const uint32_t depthBytes = static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 2u;
    if (s_VramOffset + colorBytes + depthBytes > GFX_PSP_VRAM_BYTES)
    {
        Engine_LogError("GuRenderer: no VRAM left for a %dx%d image target (%u B needed, %u B free)", width, height, colorBytes + depthBytes, GFX_PSP_VRAM_BYTES - s_VramOffset);
        return false;
    }

    m_imageColorBuffer = reinterpret_cast<void*>(static_cast<uintptr_t>(s_VramOffset));
    s_VramOffset += colorBytes;
    m_imageDepthBuffer = reinterpret_cast<void*>(static_cast<uintptr_t>(s_VramOffset));
    s_VramOffset += depthBytes;

    m_textures[m_imageTextureSlot].pixels = m_imageColorBuffer;
    m_textures[m_imageTextureSlot].clut = nullptr;
    m_textures[m_imageTextureSlot].width = static_cast<uint16_t>(width);
    m_textures[m_imageTextureSlot].height = static_cast<uint16_t>(height);
    m_textures[m_imageTextureSlot].format = static_cast<uint8_t>(PixelFormat::RGBA32);
    m_textures[m_imageTextureSlot].filter = static_cast<uint8_t>(TextureFilter::Linear);
    m_textures[m_imageTextureSlot].used = true;

    m_imageWidth = width;
    m_imageHeight = height;
    Engine_LogInfo("GuRenderer: image target %dx%d ready (%u KB video memory free)", width, height, (GFX_PSP_VRAM_BYTES - s_VramOffset) / 1024u);
    return true;
}

uint32_t GuRenderer::RenderToImage3D(const Renderable3D& what, const Camera3D& camera, int width, int height, const Color3& clearColor)
{
    if (!m_initialized)
        return 0;
    if (!EnsureImageTarget(width, height))
        return 0;
    if (!m_imageGeometry.BuildOne(what, m_drawLists))
        return 0;

    const uint32_t count = ConvertSpan(m_imageGeometry.Vertices3D(), m_imageGeometry.Count3D(), m_vertices, m_vertexCapacity);
    if (count > 0)
        sceKernelDcacheWritebackRange(m_vertices, count * sizeof(GuVertex));

    sceGuStart(GU_DIRECT, m_imageDisplayList);

    sceGuDrawBuffer(GU_PSM_8888, m_imageColorBuffer, width);
    sceGuDepthBuffer(m_imageDepthBuffer, width);
    sceGuOffset(2048 - (width / 2), 2048 - (height / 2));
    sceGuViewport(2048, 2048, width, height);
    sceGuScissor(0, 0, width, height);

    sceGuClearColor(PackColor(clearColor.r, clearColor.g, clearColor.b, 1.0f));
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

    if (count > 0)
    {
        const int vertexFormat = GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D;

        ScePspFMatrix4 identity;
        memset(&identity, 0, sizeof(identity));
        identity.x.x = 1.0f;
        identity.y.y = 1.0f;
        identity.z.z = 1.0f;
        identity.w.w = 1.0f;
        sceGuSetMatrix(GU_MODEL, &identity);
        sceGuSetMatrix(GU_VIEW, &identity);
        sceGuEnable(GU_DEPTH_TEST);

        float matrix[16];
        StagedGeometry::BuildViewProjection(camera, static_cast<uint32_t>(width), static_cast<uint32_t>(height), false, matrix);
        sceGuSetMatrix(GU_PROJECTION, reinterpret_cast<const ScePspFMatrix4*>(matrix));

        const StagedGeometry::DrawRun* runs = m_imageGeometry.Runs();
        for (uint32_t i = 0; i < m_imageGeometry.RunCount(); ++i)
        {
            if (runs[i].first + runs[i].count > count)
                continue;
            BindTexture(runs[i].texture);
            sceGuDrawArray(GU_TRIANGLES, vertexFormat, static_cast<int>(runs[i].count), nullptr, m_vertices + runs[i].first);
        }
    }

    // These registers persist across sceGuStart calls -- EndFrame()'s own pass
    // does not reprogram them every frame, only at construction -- so the main
    // framebuffer's draw context must be put back before this list ends, or
    // the next ordinary frame draws into this scratch target instead.
    sceGuDrawBuffer(GU_PSM_8888, m_drawBuffer, GFX_PSP_BUFFER_STRIDE);
    sceGuDepthBuffer(m_depthBuffer, GFX_PSP_BUFFER_STRIDE);
    sceGuOffset(2048 - (GFX_SCREEN_WIDTH / 2), 2048 - (GFX_SCREEN_HEIGHT / 2));
    sceGuViewport(2048, 2048, GFX_SCREEN_WIDTH, GFX_SCREEN_HEIGHT);
    sceGuScissor(0, 0, GFX_SCREEN_WIDTH, GFX_SCREEN_HEIGHT);

    sceGuFinish();
    // Must complete before returning: the result is sampled by the UI this
    // same frame, which has no later point to pick it up.
    sceGuSync(0, 0);

    return static_cast<uint32_t>(m_imageTextureSlot) + 1u;
}

void GuRenderer::SetCamera3D(CameraID id, const Camera3D& camera) { m_drawLists.SetCamera3D(id, camera); }
void GuRenderer::SetActiveCamera3D(CameraID id) { m_drawLists.SetActiveCamera3D(id); }
void GuRenderer::SetActiveCamera2D(const Camera2D& camera) { m_drawLists.SetActiveCamera2D(camera); }

bool GuRenderer::IsInitialized() const { return m_initialized; }

void GuRenderer::Shutdown()
{
    if (!m_initialized)
        return;

    for (int i = 0; i < GU_MAX_RESIDENT_TEXTURES; ++i)
    {
        if (!m_textures[i].used)
            continue;
        // The image-target slot's "pixels" is a VRAM offset (see
        // EnsureImageTarget), not a memalign'd main-RAM pointer -- freeing it
        // through the C allocator would be undefined behaviour. VRAM has no
        // per-object release in this backend's bump allocator regardless.
        if (i != m_imageTextureSlot)
        {
            free(m_textures[i].pixels);
            if (m_textures[i].clut)
                free(m_textures[i].clut);
        }
        memset(&m_textures[i], 0, sizeof(m_textures[i]));
    }
    m_whiteTexture = 0;
    m_imageTextureSlot = -1;
    m_imageColorBuffer = nullptr;
    m_imageDepthBuffer = nullptr;

    sceGuTerm();

    if (m_vertices)
    {
        free(m_vertices);
        m_vertices = nullptr;
    }
    for (uint32_t i = 0; i < GFX_PSP_DISPLAY_LIST_BUFFERS; ++i)
    {
        if (m_displayList[i])
        {
            free(m_displayList[i]);
            m_displayList[i] = nullptr;
        }
    }
    if (m_imageDisplayList)
    {
        free(m_imageDisplayList);
        m_imageDisplayList = nullptr;
    }

    m_initialized = false;
}

RendererType GuRenderer::GetRendererType() const { return RendererType::Gu; }
DrawStats GuRenderer::GetLastStats() const { return m_drawLists.GetLastStats(); }
Camera3D GuRenderer::GetActiveCamera3D() const { return m_drawLists.GetCamera3D(); }

void GuRenderer::RenderSkybox(const DrawLists& lists) { UNUSED_VAR(lists); }
void GuRenderer::RenderPrimitives(DrawLists& lists) { UNUSED_VAR(lists); }
void GuRenderer::RenderModels(const DrawLists& lists) { UNUSED_VAR(lists); }
