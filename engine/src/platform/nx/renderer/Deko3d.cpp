#include "renderer/Deko3d.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "Macros.h"
#include "core/EngineDebug.h"
#include "core/EngineMemory.h"
#include "graphics/TextureExpand.h"
#include "platform/Platform.h"
#include "scene_frag_dksh.h"
#include "scene_vert_dksh.h"

namespace
{
    const uint32_t DEKO3D_SAMPLER_NEAREST = 0;
    const uint32_t DEKO3D_SAMPLER_LINEAR = 1;
    const uint32_t DEKO3D_SAMPLER_COUNT = 2;
    const uint32_t DEKO3D_UNIFORM_BYTES = 16u * sizeof(float);
    const uint32_t DEKO3D_VERTEX_ATTRIBUTES = 4;
    const uint32_t DEKO3D_WHITE_TEXTURE_SIZE = 4;

    struct DkshHeader
    {
        uint32_t magic;
        uint32_t headerSize;
        uint32_t controlSize;
        uint32_t codeSize;
        uint32_t programsOffset;
        uint32_t programCount;
    };

    uint32_t AlignUp(uint32_t value, uint32_t alignment) { return (value + alignment - 1u) & ~(alignment - 1u); }

    void DebugCallback(void* userData, const char* context, DkResult result, const char* message)
    {
        UNUSED_VAR(userData);
        Engine_LogError("Deko3dRenderer: %s: %s (result %d)", context ? context : "?", message ? message : "?", static_cast<int>(result));
    }

    double Now() { return Engine_GetPlatform()->GetTimeSeconds(); }

    float MillisecondsSince(double start) { return static_cast<float>((Now() - start) * 1000.0); }
} // namespace

Deko3dRenderer::Deko3dRenderer(const EngineConfig& config) :
    m_device(nullptr), m_queue(nullptr), m_frameCmdbuf(nullptr), m_transferCmdbuf(nullptr), m_frameCommandMemory(nullptr), m_transferCommandMemory(nullptr), m_frameSlice(0), m_depthMemory(nullptr),
    m_swapchain(nullptr), m_cropWidth(0), m_cropHeight(0), m_shaderMemory(nullptr), m_uniformMemory(nullptr), m_descriptorMemory(nullptr), m_whiteTexture(0), m_clearColor{0.0f, 0.0f, 0.0f},
    m_width(GFX_SCREEN_WIDTH), m_height(GFX_SCREEN_HEIGHT), m_frame3DVertices(0), m_frame2DVertices(0), m_reportedOverflow(0), m_frameStats{}, m_initialized(false), m_imageColorMemory(nullptr),
    m_imageDepthMemory(nullptr), m_imageWidth(0), m_imageHeight(0), m_imageTextureSlot(-1)
{
    UNUSED_VAR(config);
    memset(m_sliceFences, 0, sizeof(m_sliceFences));
    memset(m_displayImages, 0, sizeof(m_displayImages));
    memset(m_displayMemory, 0, sizeof(m_displayMemory));
    memset(&m_depthImage, 0, sizeof(m_depthImage));
    memset(&m_vertexShader, 0, sizeof(m_vertexShader));
    memset(&m_fragmentShader, 0, sizeof(m_fragmentShader));
    memset(m_vertexMemory, 0, sizeof(m_vertexMemory));
    memset(m_textures, 0, sizeof(m_textures));
    memset(&m_imageColor, 0, sizeof(m_imageColor));
    memset(&m_imageDepth, 0, sizeof(m_imageDepth));

    Engine_GetPlatform()->GetFramebufferSize(&m_width, &m_height);
    Engine_LogInfo("Deko3dRenderer: initializing (%ux%u)", m_width, m_height);

    float* arena = static_cast<float*>(Engine_GetSlot(ARENA_RENDERER, 0));
    if (!arena)
    {
        Engine_LogError("Deko3dRenderer: failed to retrieve ARENA_RENDERER slot 0");
        return;
    }
    m_drawLists.Init(arena);

    if (!CreateDevice() || !CreateDisplay() || !CreateShaders() || !CreateBuffers() || !CreateSamplers() || !CreateWhiteTexture())
    {
        Destroy();
        return;
    }

    m_initialized = true;
    Engine_LogInfo("Deko3dRenderer: ready");
}

DkMemBlock Deko3dRenderer::CreateBlock(uint32_t size, uint32_t flags)
{
    DkMemBlockMaker maker;
    dkMemBlockMakerDefaults(&maker, m_device, AlignUp(size ? size : 1u, DK_MEMBLOCK_ALIGNMENT));
    maker.flags = flags;
    DkMemBlock block = dkMemBlockCreate(&maker);
    if (!block)
        Engine_LogError("Deko3dRenderer: could not map a %u KB block (flags 0x%X)", static_cast<unsigned>(maker.size / 1024u), static_cast<unsigned>(flags));
    return block;
}

bool Deko3dRenderer::CreateDevice()
{
    DkDeviceMaker deviceMaker;
    dkDeviceMakerDefaults(&deviceMaker);
    deviceMaker.cbDebug = &DebugCallback;
    deviceMaker.flags = DkDeviceFlags_DepthMinusOneToOne | DkDeviceFlags_OriginUpperLeft;
    m_device = dkDeviceCreate(&deviceMaker);
    if (!m_device)
    {
        Engine_LogError("Deko3dRenderer: dkDeviceCreate failed");
        return false;
    }

    DkQueueMaker queueMaker;
    dkQueueMakerDefaults(&queueMaker, m_device);
    queueMaker.flags = DkQueueFlags_Graphics;
    m_queue = dkQueueCreate(&queueMaker);
    if (!m_queue)
    {
        Engine_LogError("Deko3dRenderer: dkQueueCreate failed");
        return false;
    }

    DkCmdBufMaker cmdbufMaker;
    dkCmdBufMakerDefaults(&cmdbufMaker, m_device);
    m_frameCmdbuf = dkCmdBufCreate(&cmdbufMaker);
    m_transferCmdbuf = dkCmdBufCreate(&cmdbufMaker);
    m_frameCommandMemory = CreateBlock(GFX_NX_COMMAND_BYTES * GFX_NX_FRAME_SLICES, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    m_transferCommandMemory = CreateBlock(GFX_NX_TRANSFER_COMMAND_BYTES, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    return m_frameCmdbuf && m_transferCmdbuf && m_frameCommandMemory && m_transferCommandMemory;
}

bool Deko3dRenderer::CreateDisplay()
{
    DkImageLayoutMaker colorMaker;
    dkImageLayoutMakerDefaults(&colorMaker, m_device);
    colorMaker.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression;
    colorMaker.format = DkImageFormat_RGBA8_Unorm;
    colorMaker.dimensions[0] = GFX_NX_DOCKED_WIDTH;
    colorMaker.dimensions[1] = GFX_NX_DOCKED_HEIGHT;
    DkImageLayout colorLayout;
    dkImageLayoutInitialize(&colorLayout, &colorMaker);

    const uint32_t colorAlignment = dkImageLayoutGetAlignment(&colorLayout);
    const uint32_t colorSize = AlignUp(static_cast<uint32_t>(dkImageLayoutGetSize(&colorLayout)), colorAlignment);

    DkImage const* images[GFX_NX_DISPLAY_BUFFERS];
    for (uint32_t i = 0; i < GFX_NX_DISPLAY_BUFFERS; ++i)
    {
        m_displayMemory[i] = CreateBlock(colorSize, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
        if (!m_displayMemory[i])
            return false;
        dkImageInitialize(&m_displayImages[i], &colorLayout, m_displayMemory[i], 0);
        images[i] = &m_displayImages[i];
    }

    DkImageLayoutMaker depthMaker;
    dkImageLayoutMakerDefaults(&depthMaker, m_device);
    depthMaker.flags = DkImageFlags_UsageRender | DkImageFlags_HwCompression;
    depthMaker.format = DkImageFormat_Z24S8;
    depthMaker.dimensions[0] = GFX_NX_DOCKED_WIDTH;
    depthMaker.dimensions[1] = GFX_NX_DOCKED_HEIGHT;
    DkImageLayout depthLayout;
    dkImageLayoutInitialize(&depthLayout, &depthMaker);

    const uint32_t depthAlignment = dkImageLayoutGetAlignment(&depthLayout);
    m_depthMemory = CreateBlock(AlignUp(static_cast<uint32_t>(dkImageLayoutGetSize(&depthLayout)), depthAlignment), DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    if (!m_depthMemory)
        return false;
    dkImageInitialize(&m_depthImage, &depthLayout, m_depthMemory, 0);

    DkSwapchainMaker swapchainMaker;
    dkSwapchainMakerDefaults(&swapchainMaker, m_device, Engine_GetPlatform()->GetNativeWindowHandle(), images, GFX_NX_DISPLAY_BUFFERS);
    m_swapchain = dkSwapchainCreate(&swapchainMaker);
    if (!m_swapchain)
    {
        Engine_LogError("Deko3dRenderer: dkSwapchainCreate failed; is another renderer still attached to the window?");
        return false;
    }
    return true;
}

bool Deko3dRenderer::LoadShader(const uint8_t* blob, uint32_t blobSize, uint32_t codeOffset, DkShader* outShader)
{
    DkshHeader header;
    if (blobSize < sizeof(header))
        return false;
    memcpy(&header, blob, sizeof(header));

    const uint64_t end = static_cast<uint64_t>(header.controlSize) + static_cast<uint64_t>(header.codeSize);
    if (header.controlSize < sizeof(header) || end > blobSize)
    {
        Engine_LogError("Deko3dRenderer: embedded shader is malformed (control %u, code %u, blob %u)", header.controlSize, header.codeSize, blobSize);
        return false;
    }

    memcpy(static_cast<uint8_t*>(dkMemBlockGetCpuAddr(m_shaderMemory)) + codeOffset, blob + header.controlSize, header.codeSize);

    DkShaderMaker maker;
    dkShaderMakerDefaults(&maker, m_shaderMemory, codeOffset);
    maker.control = blob;
    maker.programId = 0;
    dkShaderInitialize(outShader, &maker);
    return dkShaderIsValid(outShader);
}

bool Deko3dRenderer::CreateShaders()
{
    DkshHeader vertexHeader;
    DkshHeader fragmentHeader;
    memcpy(&vertexHeader, g_SceneVertexDksh, sizeof(vertexHeader));
    memcpy(&fragmentHeader, g_SceneFragmentDksh, sizeof(fragmentHeader));

    const uint32_t fragmentOffset = AlignUp(vertexHeader.codeSize, DK_SHADER_CODE_ALIGNMENT);
    const uint32_t codeBytes = fragmentOffset + AlignUp(fragmentHeader.codeSize, DK_SHADER_CODE_ALIGNMENT) + DK_SHADER_CODE_UNUSABLE_SIZE;

    m_shaderMemory = CreateBlock(codeBytes, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);
    if (!m_shaderMemory)
        return false;

    if (!LoadShader(g_SceneVertexDksh, g_SceneVertexDksh_size, 0, &m_vertexShader) || !LoadShader(g_SceneFragmentDksh, g_SceneFragmentDksh_size, fragmentOffset, &m_fragmentShader))
    {
        Engine_LogError("Deko3dRenderer: shader initialisation failed");
        return false;
    }
    return true;
}

bool Deko3dRenderer::CreateBuffers()
{
    const uint32_t vertexBytes = GFX_NX_MAX_FRAME_VERTICES * static_cast<uint32_t>(sizeof(StagedGeometry::Vertex));
    for (uint32_t i = 0; i < GFX_NX_FRAME_SLICES; ++i)
    {
        m_vertexMemory[i] = CreateBlock(vertexBytes, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        if (!m_vertexMemory[i])
            return false;
    }

    m_uniformMemory = CreateBlock(AlignUp(DEKO3D_UNIFORM_BYTES, DK_UNIFORM_BUF_ALIGNMENT), DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    m_descriptorMemory = CreateBlock((DEKO3D_MAX_RESIDENT_TEXTURES + DEKO3D_SAMPLER_COUNT) * DK_IMAGE_DESCRIPTOR_ALIGNMENT, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    return m_uniformMemory && m_descriptorMemory;
}

void Deko3dRenderer::BeginTransfer()
{
    dkCmdBufClear(m_transferCmdbuf);
    dkCmdBufAddMemory(m_transferCmdbuf, m_transferCommandMemory, 0, dkMemBlockGetSize(m_transferCommandMemory));
}

void Deko3dRenderer::FinishTransfer()
{
    dkQueueSubmitCommands(m_queue, dkCmdBufFinishList(m_transferCmdbuf));
    dkQueueWaitIdle(m_queue);
}

void Deko3dRenderer::WriteImageDescriptor(DkCmdBuf cmdbuf, uint32_t slot, const DkImage& image)
{
    DkImageView view;
    dkImageViewDefaults(&view, &image);
    DkImageDescriptor descriptor;
    dkImageDescriptorInitialize(&descriptor, &view, false, false);
    dkCmdBufPushData(cmdbuf, dkMemBlockGetGpuAddr(m_descriptorMemory) + slot * DK_IMAGE_DESCRIPTOR_ALIGNMENT, &descriptor, sizeof(descriptor));
    dkCmdBufBarrier(cmdbuf, DkBarrier_None, DkInvalidateFlags_Descriptors);
}

bool Deko3dRenderer::CreateSamplers()
{
    DkSampler nearest;
    dkSamplerDefaults(&nearest);

    DkSampler linear;
    dkSamplerDefaults(&linear);
    linear.minFilter = DkFilter_Linear;
    linear.magFilter = DkFilter_Linear;

    DkSamplerDescriptor descriptors[DEKO3D_SAMPLER_COUNT];
    dkSamplerDescriptorInitialize(&descriptors[DEKO3D_SAMPLER_NEAREST], &nearest);
    dkSamplerDescriptorInitialize(&descriptors[DEKO3D_SAMPLER_LINEAR], &linear);

    BeginTransfer();
    dkCmdBufPushData(m_transferCmdbuf, dkMemBlockGetGpuAddr(m_descriptorMemory) + DEKO3D_MAX_RESIDENT_TEXTURES * DK_IMAGE_DESCRIPTOR_ALIGNMENT, descriptors, sizeof(descriptors));
    FinishTransfer();
    return true;
}

bool Deko3dRenderer::CreateWhiteTexture()
{
    static uint8_t white[DEKO3D_WHITE_TEXTURE_SIZE * DEKO3D_WHITE_TEXTURE_SIZE * 4] __attribute__((aligned(16)));
    memset(white, 0xFF, sizeof(white));

    TextureUpload upload;
    memset(&upload, 0, sizeof(upload));
    upload.width = DEKO3D_WHITE_TEXTURE_SIZE;
    upload.height = DEKO3D_WHITE_TEXTURE_SIZE;
    upload.format = PixelFormat::RGBA32;
    upload.filter = TextureFilter::Nearest;
    upload.levelPtr[0] = white;
    upload.mipCount = 1;

    m_whiteTexture = UploadTexture(upload);
    return m_whiteTexture != 0;
}

int Deko3dRenderer::FindFreeTextureSlot() const
{
    for (int i = 0; i < DEKO3D_MAX_RESIDENT_TEXTURES; ++i)
    {
        if (!m_textures[i].used)
            return i;
    }
    return -1;
}

uint32_t Deko3dRenderer::UploadTexture(const TextureUpload& upload)
{
    if (!m_device || !m_queue)
        return 0;

    const uint32_t width = static_cast<uint32_t>(upload.width);
    const uint32_t height = static_cast<uint32_t>(upload.height);
    if (width == 0 || height == 0)
        return 0;

    const int slot = FindFreeTextureSlot();
    if (slot < 0)
    {
        Engine_LogError("Deko3dRenderer: texture registry full (%d)", DEKO3D_MAX_RESIDENT_TEXTURES);
        return 0;
    }

    const uint64_t texelBytes = static_cast<uint64_t>(width) * height * 4u;
    if (texelBytes > UINT32_MAX)
        return 0;

    DkMemBlock staging = CreateBlock(static_cast<uint32_t>(texelBytes), DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    if (!staging)
        return 0;

    if (!Gfx_ExpandToRgba8(upload, static_cast<uint8_t*>(dkMemBlockGetCpuAddr(staging)), static_cast<size_t>(texelBytes)))
    {
        dkMemBlockDestroy(staging);
        Engine_LogError("Deko3dRenderer: could not expand a %ux%u texture", width, height);
        return 0;
    }

    DkImageLayoutMaker layoutMaker;
    dkImageLayoutMakerDefaults(&layoutMaker, m_device);
    layoutMaker.format = DkImageFormat_RGBA8_Unorm;
    layoutMaker.dimensions[0] = width;
    layoutMaker.dimensions[1] = height;
    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &layoutMaker);

    Texture& texture = m_textures[slot];
    const uint32_t alignment = dkImageLayoutGetAlignment(&layout);
    texture.memory = CreateBlock(AlignUp(static_cast<uint32_t>(dkImageLayoutGetSize(&layout)), alignment), DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    if (!texture.memory)
    {
        dkMemBlockDestroy(staging);
        return 0;
    }
    dkImageInitialize(&texture.image, &layout, texture.memory, 0);

    DkImageView view;
    dkImageViewDefaults(&view, &texture.image);
    DkCopyBuf source;
    source.addr = dkMemBlockGetGpuAddr(staging);
    source.rowLength = 0;
    source.imageHeight = 0;
    DkImageRect rect;
    rect.x = 0;
    rect.y = 0;
    rect.z = 0;
    rect.width = width;
    rect.height = height;
    rect.depth = 1;

    BeginTransfer();
    dkCmdBufCopyBufferToImage(m_transferCmdbuf, &source, &view, &rect, 0);
    WriteImageDescriptor(m_transferCmdbuf, static_cast<uint32_t>(slot), texture.image);
    FinishTransfer();
    dkMemBlockDestroy(staging);

    texture.sampler = (upload.filter == TextureFilter::Nearest) ? DEKO3D_SAMPLER_NEAREST : DEKO3D_SAMPLER_LINEAR;
    texture.used = true;
    return static_cast<uint32_t>(slot) + 1u;
}

void Deko3dRenderer::ReleaseTexture(uint32_t handle)
{
    if (handle == 0 || handle > DEKO3D_MAX_RESIDENT_TEXTURES || handle == m_whiteTexture)
        return;

    Texture& texture = m_textures[handle - 1u];
    if (!texture.used || static_cast<int>(handle - 1u) == m_imageTextureSlot)
        return;

    dkQueueWaitIdle(m_queue);
    dkMemBlockDestroy(texture.memory);
    memset(&texture, 0, sizeof(texture));
}

void Deko3dRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale)
{
    AddPrimitiveToDrawList(primitive, position, rotation, scale, Color3{1.0f, 1.0f, 1.0f}, -1);
}

void Deko3dRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, Color3 color)
{
    AddPrimitiveToDrawList(primitive, position, rotation, scale, color, -1);
}

void Deko3dRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, int32_t textureId)
{
    AddPrimitiveToDrawList(primitive, position, rotation, scale, Color3{1.0f, 1.0f, 1.0f}, textureId);
}

void Deko3dRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, Color3 color, int32_t textureId)
{
    PrimitiveDrawEntry entry;
    entry.transform = Transform3D(position, rotation, scale);
    entry.color = color;
    entry.type = primitive;
    entry.textureId = textureId;
    m_drawLists.AddPrimitive(entry);
}

void Deko3dRenderer::AddLevelToDrawList(const Level& level) { UNUSED_VAR(level); }

void Deko3dRenderer::AddModelToDrawList(int32_t modelId, const Vector3& position, const Vector3& rotation, const Vector3& scale)
{
    ModelDrawEntry entry;
    entry.resourceId = modelId;
    entry.transform = Transform3D(position, rotation, scale);
    m_drawLists.AddModel(entry);
}

void Deko3dRenderer::AddSkyToDrawList(int32_t resourceId) { m_drawLists.SetSkyboxTexture(resourceId); }

void Deko3dRenderer::ClearDrawLists() { m_drawLists.Reset(false); }

void Deko3dRenderer::ClearFrame(const Color3& color) { m_clearColor = color; }

void Deko3dRenderer::DrawQuad2D(const Quad2D& quad) { m_geometry.AddQuad2D(quad); }

void Deko3dRenderer::DrawGrid(int32_t slices, float spacing)
{
    UNUSED_VAR(slices);
    UNUSED_VAR(spacing);
}

void Deko3dRenderer::BeginFrame()
{
    Engine_GetPlatform()->GetFramebufferSize(&m_width, &m_height);
    m_geometry.SetFrameBudget(GFX_NX_MAX_FRAME_VERTICES, m_width, m_height);
    m_geometry.BeginFrame();
    m_frameStats = DrawStats{};
}

void Deko3dRenderer::Render()
{
    const double start = Now();
    m_geometry.BuildFrame(m_drawLists, &m_frameStats);
    m_frameStats.geometryBuildMs = MillisecondsSince(start);
}

void Deko3dRenderer::UploadVertices(uint32_t slice)
{
    const uint32_t count3D = m_geometry.Count3D();
    const uint32_t count2D = m_geometry.Count2D();
    uint32_t total = count3D + count2D;

    m_frameStats.submitBufferUsedBytes = total * static_cast<uint32_t>(sizeof(StagedGeometry::Vertex));
    m_frameStats.submitBufferCapacityBytes = GFX_NX_MAX_FRAME_VERTICES * static_cast<uint32_t>(sizeof(StagedGeometry::Vertex));

    if (total > GFX_NX_MAX_FRAME_VERTICES)
    {
        if (total != m_reportedOverflow)
        {
            m_reportedOverflow = total;
            Engine_LogError("Deko3dRenderer: frame needs %u vertices, ceiling is %u; dropping the excess", total, GFX_NX_MAX_FRAME_VERTICES);
        }
        total = GFX_NX_MAX_FRAME_VERTICES;
    }
    else
    {
        m_reportedOverflow = 0;
    }

    StagedGeometry::Vertex* dst = static_cast<StagedGeometry::Vertex*>(dkMemBlockGetCpuAddr(m_vertexMemory[slice]));
    const uint32_t take2D = (count2D < total) ? count2D : total;
    const uint32_t take3D = (total - take2D < count3D) ? (total - take2D) : count3D;

    if (take3D)
        memcpy(dst, m_geometry.Vertices3D(), take3D * sizeof(StagedGeometry::Vertex));
    if (take2D)
        memcpy(dst + take3D, m_geometry.Vertices2D(), take2D * sizeof(StagedGeometry::Vertex));

    m_frame3DVertices = take3D;
    m_frame2DVertices = take2D;
}

void Deko3dRenderer::BeginPass(DkCmdBuf cmdbuf, const DkImageView& color, const DkImageView& depth, uint32_t width, uint32_t height, const Color3& clearColor, uint32_t slice)
{
    dkCmdBufBindRenderTarget(cmdbuf, &color, &depth);

    DkViewport viewport;
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.near = 0.0f;
    viewport.far = 1.0f;
    dkCmdBufSetViewports(cmdbuf, 0, &viewport, 1);

    DkScissor scissor;
    scissor.x = 0;
    scissor.y = 0;
    scissor.width = width;
    scissor.height = height;
    dkCmdBufSetScissors(cmdbuf, 0, &scissor, 1);

    dkCmdBufClearColorFloat(cmdbuf, 0, DkColorMask_RGBA, clearColor.r, clearColor.g, clearColor.b, 1.0f);
    dkCmdBufClearDepthStencil(cmdbuf, true, 1.0f, 0xFF, 0);

    DkShader const* shaders[] = {&m_vertexShader, &m_fragmentShader};
    dkCmdBufBindShaders(cmdbuf, DkStageFlag_GraphicsMask, shaders, 2);

    DkRasterizerState rasterizer;
    dkRasterizerStateDefaults(&rasterizer);
    rasterizer.cullMode = DkFace_None;
    dkCmdBufBindRasterizerState(cmdbuf, &rasterizer);

    DkColorWriteState colorWrite;
    dkColorWriteStateDefaults(&colorWrite);
    dkCmdBufBindColorWriteState(cmdbuf, &colorWrite);

    DkBlendState blend;
    dkBlendStateDefaults(&blend);
    dkCmdBufBindBlendState(cmdbuf, 0, &blend);

    const uint32_t stride = static_cast<uint32_t>(sizeof(StagedGeometry::Vertex));
    DkVtxAttribState attributes[DEKO3D_VERTEX_ATTRIBUTES];
    memset(attributes, 0, sizeof(attributes));
    const uint32_t offsets[DEKO3D_VERTEX_ATTRIBUTES] = {static_cast<uint32_t>(offsetof(StagedGeometry::Vertex, x)), static_cast<uint32_t>(offsetof(StagedGeometry::Vertex, nx)),
                                                        static_cast<uint32_t>(offsetof(StagedGeometry::Vertex, u)), static_cast<uint32_t>(offsetof(StagedGeometry::Vertex, r))};
    const DkVtxAttribSize sizes[DEKO3D_VERTEX_ATTRIBUTES] = {DkVtxAttribSize_3x32, DkVtxAttribSize_3x32, DkVtxAttribSize_2x32, DkVtxAttribSize_4x32};
    for (uint32_t i = 0; i < DEKO3D_VERTEX_ATTRIBUTES; ++i)
    {
        attributes[i].bufferId = 0;
        attributes[i].isFixed = 0;
        attributes[i].offset = offsets[i];
        attributes[i].size = sizes[i];
        attributes[i].type = DkVtxAttribType_Float;
        attributes[i].isBgra = 0;
    }
    dkCmdBufBindVtxAttribState(cmdbuf, attributes, DEKO3D_VERTEX_ATTRIBUTES);

    DkVtxBufferState bufferState;
    bufferState.stride = stride;
    bufferState.divisor = 0;
    dkCmdBufBindVtxBufferState(cmdbuf, &bufferState, 1);
    dkCmdBufBindVtxBuffer(cmdbuf, 0, dkMemBlockGetGpuAddr(m_vertexMemory[slice]), dkMemBlockGetSize(m_vertexMemory[slice]));

    dkCmdBufBindImageDescriptorSet(cmdbuf, dkMemBlockGetGpuAddr(m_descriptorMemory), DEKO3D_MAX_RESIDENT_TEXTURES);
    dkCmdBufBindSamplerDescriptorSet(cmdbuf, dkMemBlockGetGpuAddr(m_descriptorMemory) + DEKO3D_MAX_RESIDENT_TEXTURES * DK_IMAGE_DESCRIPTOR_ALIGNMENT, DEKO3D_SAMPLER_COUNT);

    dkCmdBufBindUniformBuffer(cmdbuf, DkStage_Vertex, 0, dkMemBlockGetGpuAddr(m_uniformMemory), DEKO3D_UNIFORM_BYTES);
}

void Deko3dRenderer::BindPassState(DkCmdBuf cmdbuf, PassKind kind, const float matrix[16])
{
    DkColorState color;
    dkColorStateDefaults(&color);
    dkColorStateSetBlendEnable(&color, 0, kind == PassKind::Screen);
    dkCmdBufBindColorState(cmdbuf, &color);

    DkDepthStencilState depth;
    dkDepthStencilStateDefaults(&depth);
    depth.depthTestEnable = (kind == PassKind::World) ? 1u : 0u;
    depth.depthWriteEnable = (kind == PassKind::World) ? 1u : 0u;
    depth.depthCompareOp = DkCompareOp_Lequal;
    dkCmdBufBindDepthStencilState(cmdbuf, &depth);

    dkCmdBufPushConstants(cmdbuf, dkMemBlockGetGpuAddr(m_uniformMemory), DEKO3D_UNIFORM_BYTES, 0, DEKO3D_UNIFORM_BYTES, matrix);
}

void Deko3dRenderer::DrawRuns(DkCmdBuf cmdbuf, const StagedGeometry::DrawRun* runs, uint32_t runCount, uint32_t base, uint32_t uploaded)
{
    for (uint32_t i = 0; i < runCount; ++i)
    {
        const uint32_t first = runs[i].first;
        if (first >= uploaded)
            continue;
        uint32_t count = runs[i].count;
        if (first + count > uploaded)
            count = uploaded - first;
        count -= count % 3u;
        if (!count)
            continue;

        const uint32_t handle = runs[i].texture ? runs[i].texture : m_whiteTexture;
        const uint32_t slot = (handle && handle <= DEKO3D_MAX_RESIDENT_TEXTURES && m_textures[handle - 1u].used) ? handle - 1u : m_whiteTexture - 1u;
        dkCmdBufBindTexture(cmdbuf, DkStage_Fragment, 0, dkMakeTextureHandle(slot, m_textures[slot].sampler));
        dkCmdBufDraw(cmdbuf, DkPrimitive_Triangles, count, 1, base + first, 0);
        ++m_frameStats.texBinds;
    }
}

void Deko3dRenderer::EndFrame()
{
    if (!m_initialized)
        return;

    const uint32_t slice = m_frameSlice;

    const double waitStart = Now();
    dkFenceWait(&m_sliceFences[slice], -1);
    const int imageSlot = dkQueueAcquireImage(m_queue, m_swapchain);
    m_frameStats.presentWaitMs = MillisecondsSince(waitStart);

    const double uploadStart = Now();
    UploadVertices(slice);
    m_frameStats.geometryUploadMs = MillisecondsSince(uploadStart);

    dkCmdBufClear(m_frameCmdbuf);
    dkCmdBufAddMemory(m_frameCmdbuf, m_frameCommandMemory, slice * GFX_NX_COMMAND_BYTES, GFX_NX_COMMAND_BYTES);

    DkImageView colorView;
    dkImageViewDefaults(&colorView, &m_displayImages[imageSlot]);
    DkImageView depthView;
    dkImageViewDefaults(&depthView, &m_depthImage);
    BeginPass(m_frameCmdbuf, colorView, depthView, m_width, m_height, m_clearColor, slice);

    float matrix[16];
    if (m_frame3DVertices > 0)
    {
        StagedGeometry::BuildViewProjection(m_drawLists.GetCamera3D(), m_width, m_height, false, matrix);
        BindPassState(m_frameCmdbuf, PassKind::World, matrix);
        DrawRuns(m_frameCmdbuf, m_geometry.Runs(), m_geometry.RunCount(), 0, m_frame3DVertices);
    }
    if (m_frame2DVertices > 0)
    {
        StagedGeometry::BuildOrtho2D(m_width, m_height, false, matrix);
        BindPassState(m_frameCmdbuf, PassKind::Screen, matrix);
        DrawRuns(m_frameCmdbuf, m_geometry.Runs2D(), m_geometry.RunCount2D(), m_frame3DVertices, m_frame2DVertices);
    }

    dkCmdBufSignalFence(m_frameCmdbuf, &m_sliceFences[slice], false);
    dkQueueSubmitCommands(m_queue, dkCmdBufFinishList(m_frameCmdbuf));

    if (m_width != m_cropWidth || m_height != m_cropHeight)
    {
        dkSwapchainSetCrop(m_swapchain, 0, 0, static_cast<int32_t>(m_width), static_cast<int32_t>(m_height));
        m_cropWidth = m_width;
        m_cropHeight = m_height;
    }
    dkQueuePresentImage(m_queue, m_swapchain, imageSlot);

    m_frameSlice = (slice + 1u) % GFX_NX_FRAME_SLICES;

    m_geometry.EndFrame();
    m_drawLists.SetLastStats(m_frameStats);
    m_drawLists.Reset(false);
}

bool Deko3dRenderer::EnsureImageTarget(int width, int height)
{
    if (width <= 0 || height <= 0 || width > GFX_MAX_TEXTURE_WIDTH || height > GFX_MAX_TEXTURE_HEIGHT)
        return false;
    if (m_imageColorMemory && m_imageWidth == width && m_imageHeight == height)
        return true;

    if (m_imageTextureSlot < 0)
    {
        m_imageTextureSlot = FindFreeTextureSlot();
        if (m_imageTextureSlot < 0)
            return false;
    }

    DestroyImageTarget();

    DkImageLayoutMaker colorMaker;
    dkImageLayoutMakerDefaults(&colorMaker, m_device);
    colorMaker.flags = DkImageFlags_UsageRender;
    colorMaker.format = DkImageFormat_RGBA8_Unorm;
    colorMaker.dimensions[0] = static_cast<uint32_t>(width);
    colorMaker.dimensions[1] = static_cast<uint32_t>(height);
    DkImageLayout colorLayout;
    dkImageLayoutInitialize(&colorLayout, &colorMaker);

    DkImageLayoutMaker depthMaker;
    dkImageLayoutMakerDefaults(&depthMaker, m_device);
    depthMaker.flags = DkImageFlags_UsageRender;
    depthMaker.format = DkImageFormat_Z24S8;
    depthMaker.dimensions[0] = static_cast<uint32_t>(width);
    depthMaker.dimensions[1] = static_cast<uint32_t>(height);
    DkImageLayout depthLayout;
    dkImageLayoutInitialize(&depthLayout, &depthMaker);

    m_imageColorMemory = CreateBlock(AlignUp(static_cast<uint32_t>(dkImageLayoutGetSize(&colorLayout)), dkImageLayoutGetAlignment(&colorLayout)), DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    m_imageDepthMemory = CreateBlock(AlignUp(static_cast<uint32_t>(dkImageLayoutGetSize(&depthLayout)), dkImageLayoutGetAlignment(&depthLayout)), DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    if (!m_imageColorMemory || !m_imageDepthMemory)
    {
        DestroyImageTarget();
        return false;
    }

    dkImageInitialize(&m_imageColor, &colorLayout, m_imageColorMemory, 0);
    dkImageInitialize(&m_imageDepth, &depthLayout, m_imageDepthMemory, 0);

    BeginTransfer();
    WriteImageDescriptor(m_transferCmdbuf, static_cast<uint32_t>(m_imageTextureSlot), m_imageColor);
    FinishTransfer();

    Texture& texture = m_textures[m_imageTextureSlot];
    texture.image = m_imageColor;
    texture.memory = nullptr;
    texture.sampler = DEKO3D_SAMPLER_LINEAR;
    texture.used = true;

    m_imageWidth = width;
    m_imageHeight = height;
    return true;
}

void Deko3dRenderer::DestroyImageTarget()
{
    if (m_queue)
        dkQueueWaitIdle(m_queue);
    if (m_imageColorMemory)
        dkMemBlockDestroy(m_imageColorMemory);
    if (m_imageDepthMemory)
        dkMemBlockDestroy(m_imageDepthMemory);
    m_imageColorMemory = nullptr;
    m_imageDepthMemory = nullptr;
    m_imageWidth = 0;
    m_imageHeight = 0;
}

uint32_t Deko3dRenderer::RenderToImage3D(const Renderable3D& what, const Camera3D& camera, int width, int height, const Color3& clearColor)
{
    if (!m_initialized || !EnsureImageTarget(width, height))
        return 0;
    if (!m_imageGeometry.BuildOne(what, m_drawLists))
        return 0;

    const uint32_t slice = m_frameSlice;
    dkFenceWait(&m_sliceFences[slice], -1);

    uint32_t count = m_imageGeometry.Count3D();
    if (count > GFX_NX_MAX_FRAME_VERTICES)
        count = GFX_NX_MAX_FRAME_VERTICES;
    memcpy(dkMemBlockGetCpuAddr(m_vertexMemory[slice]), m_imageGeometry.Vertices3D(), count * sizeof(StagedGeometry::Vertex));

    DkImageView colorView;
    dkImageViewDefaults(&colorView, &m_imageColor);
    DkImageView depthView;
    dkImageViewDefaults(&depthView, &m_imageDepth);

    BeginTransfer();
    BeginPass(m_transferCmdbuf, colorView, depthView, static_cast<uint32_t>(width), static_cast<uint32_t>(height), clearColor, slice);

    float matrix[16];
    StagedGeometry::BuildViewProjection(camera, static_cast<uint32_t>(width), static_cast<uint32_t>(height), false, matrix);
    BindPassState(m_transferCmdbuf, PassKind::World, matrix);
    DrawRuns(m_transferCmdbuf, m_imageGeometry.Runs(), m_imageGeometry.RunCount(), 0, count);
    FinishTransfer();

    return static_cast<uint32_t>(m_imageTextureSlot) + 1u;
}

void Deko3dRenderer::SetCamera3D(CameraID id, const Camera3D& camera) { m_drawLists.SetCamera3D(id, camera); }
void Deko3dRenderer::SetActiveCamera3D(CameraID id) { m_drawLists.SetActiveCamera3D(id); }
void Deko3dRenderer::SetActiveCamera2D(const Camera2D& camera) { m_drawLists.SetActiveCamera2D(camera); }

bool Deko3dRenderer::IsInitialized() const { return m_initialized; }

void Deko3dRenderer::Destroy()
{
    if (m_queue)
        dkQueueWaitIdle(m_queue);

    if (m_swapchain)
        dkSwapchainDestroy(m_swapchain);
    m_swapchain = nullptr;

    DestroyImageTarget();
    for (int i = 0; i < DEKO3D_MAX_RESIDENT_TEXTURES; ++i)
    {
        if (m_textures[i].used && m_textures[i].memory)
            dkMemBlockDestroy(m_textures[i].memory);
    }
    memset(m_textures, 0, sizeof(m_textures));
    m_imageTextureSlot = -1;
    m_whiteTexture = 0;

    for (uint32_t i = 0; i < GFX_NX_DISPLAY_BUFFERS; ++i)
    {
        if (m_displayMemory[i])
            dkMemBlockDestroy(m_displayMemory[i]);
        m_displayMemory[i] = nullptr;
    }
    if (m_depthMemory)
        dkMemBlockDestroy(m_depthMemory);
    m_depthMemory = nullptr;

    for (uint32_t i = 0; i < GFX_NX_FRAME_SLICES; ++i)
    {
        if (m_vertexMemory[i])
            dkMemBlockDestroy(m_vertexMemory[i]);
        m_vertexMemory[i] = nullptr;
    }

    const DkMemBlock* blocks[] = {&m_uniformMemory, &m_descriptorMemory, &m_shaderMemory, &m_frameCommandMemory, &m_transferCommandMemory};
    for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); ++i)
    {
        if (*blocks[i])
            dkMemBlockDestroy(*blocks[i]);
    }
    m_uniformMemory = nullptr;
    m_descriptorMemory = nullptr;
    m_shaderMemory = nullptr;
    m_frameCommandMemory = nullptr;
    m_transferCommandMemory = nullptr;

    if (m_frameCmdbuf)
        dkCmdBufDestroy(m_frameCmdbuf);
    if (m_transferCmdbuf)
        dkCmdBufDestroy(m_transferCmdbuf);
    m_frameCmdbuf = nullptr;
    m_transferCmdbuf = nullptr;

    if (m_queue)
        dkQueueDestroy(m_queue);
    m_queue = nullptr;

    if (m_device)
        dkDeviceDestroy(m_device);
    m_device = nullptr;
}

void Deko3dRenderer::Shutdown()
{
    if (!m_initialized)
        return;
    Destroy();
    m_initialized = false;
}

RendererType Deko3dRenderer::GetRendererType() const { return RendererType::Deko3d; }
DrawStats Deko3dRenderer::GetLastStats() const { return m_drawLists.GetLastStats(); }
Camera3D Deko3dRenderer::GetActiveCamera3D() const { return m_drawLists.GetCamera3D(); }

void Deko3dRenderer::RenderSkybox(const DrawLists& lists) { UNUSED_VAR(lists); }
void Deko3dRenderer::RenderPrimitives(DrawLists& lists) { UNUSED_VAR(lists); }
void Deko3dRenderer::RenderModels(const DrawLists& lists) { UNUSED_VAR(lists); }
