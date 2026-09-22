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
#include "scene_pbr_frag_dksh.h"
#include "scene_pbr_vert_dksh.h"
#include "scene_shadow_frag_dksh.h"
#include "scene_shadow_vert_dksh.h"

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

    // Mirrors scene_pbr.vert.glsl/scene_pbr.frag.glsl's FrameUniforms block
    // field-for-field, in the same order -- std140 layout, every member
    // already a multiple of 16 bytes. sizeof(FrameUniformsCpu) is the single
    // source of truth for this buffer's size, used both where it is
    // allocated (CreateBuffers) and where it is written (EndFrame). Byte-for-
    // byte identical to OpenGl.cpp (nx)'s own copy of this struct, since both
    // backends compile the same GLSL source (see tools/nx_shader.py) and must
    // agree on the uniform block it declares.
    struct FrameUniformsCpu
    {
        float viewProj[16];
        float lightViewProj[16];
        float cameraPos[4];
        float ambient[4];
        float lightPosOrDir[4 * 4];
        float lightColorIntensity[4 * 4];
        float lightRange[4 * 4];
        float shadowCaster[4];
    };

    // Mirrors MaterialUniform likewise.
    struct MaterialUniformCpu
    {
        float baseColor[4];
        float emissive[4];
        float mrna[4];
        float matFlags[4];
    };

    const uint32_t DEKO3D_PBR_FRAME_BINDING = 0;
    const uint32_t DEKO3D_PBR_MATERIAL_BINDING = 1;
    const uint32_t DEKO3D_SHADOW_BINDING = 0;

    // MaterialUniform std140 layout: vec4 * 4 = 64 bytes, strided to deko3d's
    // own uniform-buffer alignment (the same 256 bytes WebGpu.cpp/OpenGl.cpp
    // (nx)'s dynamic-offset material buffers use, for the same reason).
    const uint32_t DEKO3D_PBR_MATERIAL_STRIDE = AlignUp(static_cast<uint32_t>(sizeof(MaterialUniformCpu)), DK_UNIFORM_BUF_ALIGNMENT);

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
    m_swapchain(nullptr), m_cropWidth(0), m_cropHeight(0), m_shaderMemory(nullptr),
    m_pbrFrameUniformMemory(nullptr), m_pbrMaterialUniformMemory(nullptr), m_shadowUniformMemory(nullptr), m_shadowColorMemory(nullptr), m_shadowDepthMemory(nullptr), m_shadowTextureSlot(-1),
    m_defaultNormalTexture(0), m_defaultOrmTexture(0), m_shadowActive(false), m_shadowCasterIndex(-1),
    m_uniformMemory(nullptr), m_descriptorMemory(nullptr), m_whiteTexture(0), m_clearColor{0.0f, 0.0f, 0.0f},
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
    memset(&m_pbrVertexShader, 0, sizeof(m_pbrVertexShader));
    memset(&m_pbrFragmentShader, 0, sizeof(m_pbrFragmentShader));
    memset(&m_shadowVertexShader, 0, sizeof(m_shadowVertexShader));
    memset(&m_shadowFragmentShader, 0, sizeof(m_shadowFragmentShader));
    memset(&m_shadowColorImage, 0, sizeof(m_shadowColorImage));
    memset(&m_shadowDepthImage, 0, sizeof(m_shadowDepthImage));
    memset(m_lastLightViewProj, 0, sizeof(m_lastLightViewProj));
    memset(m_vertexMemory, 0, sizeof(m_vertexMemory));
    memset(m_textures, 0, sizeof(m_textures));
    memset(&m_imageColor, 0, sizeof(m_imageColor));
    memset(&m_imageDepth, 0, sizeof(m_imageDepth));

    static_assert(GFX_MAX_LIGHTS == 4, "scene_pbr.frag.glsl hardcodes a 4-light loop");
    static_assert(GFX_SHADOW_MAP_SIZE == 1024, "scene_pbr.frag.glsl hardcodes texel = 1.0/1024.0 for shadow PCF");

    Engine_GetPlatform()->GetFramebufferSize(&m_width, &m_height);
    Engine_LogInfo("Deko3dRenderer: initializing (%ux%u)", m_width, m_height);

    float* arena = static_cast<float*>(Engine_GetSlot(ARENA_RENDERER, 0));
    if (!arena)
    {
        Engine_LogError("Deko3dRenderer: failed to retrieve ARENA_RENDERER slot 0");
        return;
    }
    m_drawLists.Init(arena);

    if (!CreateDevice() || !CreateDisplay() || !CreateShaders() || !CreateBuffers() || !CreateSamplers() || !CreateWhiteTexture() || !CreateDefaultMaterialTextures() || !CreateShadowTarget())
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
    // All three shader pairs (flat, PBR, depth-only shadow) share one code
    // block, laid out back to back -- the same "compute each one's aligned
    // offset, allocate once" shape the original flat-only version of this
    // function used for its own two shaders, just generalised to six.
    const uint8_t* blobs[6] = {g_SceneVertexDksh, g_SceneFragmentDksh, g_ScenePbrVertexDksh, g_ScenePbrFragmentDksh, g_SceneShadowVertexDksh, g_SceneShadowFragmentDksh};
    const uint32_t sizes[6] = {g_SceneVertexDksh_size, g_SceneFragmentDksh_size, g_ScenePbrVertexDksh_size, g_ScenePbrFragmentDksh_size, g_SceneShadowVertexDksh_size, g_SceneShadowFragmentDksh_size};
    DkShader* const outs[6] = {&m_vertexShader, &m_fragmentShader, &m_pbrVertexShader, &m_pbrFragmentShader, &m_shadowVertexShader, &m_shadowFragmentShader};

    uint32_t offsets[6];
    uint32_t cursor = 0;
    for (int i = 0; i < 6; ++i)
    {
        DkshHeader header;
        memcpy(&header, blobs[i], sizeof(header));
        offsets[i] = cursor;
        cursor = AlignUp(cursor + header.codeSize, DK_SHADER_CODE_ALIGNMENT);
    }
    const uint32_t codeBytes = cursor + DK_SHADER_CODE_UNUSABLE_SIZE;

    m_shaderMemory = CreateBlock(codeBytes, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);
    if (!m_shaderMemory)
        return false;

    for (int i = 0; i < 6; ++i)
    {
        if (!LoadShader(blobs[i], sizes[i], offsets[i], outs[i]))
        {
            Engine_LogError("Deko3dRenderer: shader initialisation failed (index %d)", i);
            return false;
        }
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

    m_pbrFrameUniformMemory = CreateBlock(AlignUp(static_cast<uint32_t>(sizeof(FrameUniformsCpu)), DK_UNIFORM_BUF_ALIGNMENT), DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    m_pbrMaterialUniformMemory = CreateBlock(DEKO3D_PBR_MATERIAL_STRIDE * GFX_MAX_DRAW_RUNS, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    m_shadowUniformMemory = CreateBlock(AlignUp(DEKO3D_UNIFORM_BYTES, DK_UNIFORM_BUF_ALIGNMENT), DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);

    return m_uniformMemory && m_descriptorMemory && m_pbrFrameUniformMemory && m_pbrMaterialUniformMemory && m_shadowUniformMemory;
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

bool Deko3dRenderer::CreateDefaultMaterialTextures()
{
    static uint8_t flatNormal[4] __attribute__((aligned(16))) = {128, 128, 255, 255};
    TextureUpload normalUpload;
    memset(&normalUpload, 0, sizeof(normalUpload));
    normalUpload.width = 1;
    normalUpload.height = 1;
    normalUpload.format = PixelFormat::RGBA32;
    normalUpload.filter = TextureFilter::Linear;
    normalUpload.levelPtr[0] = flatNormal;
    normalUpload.mipCount = 1;
    m_defaultNormalTexture = UploadTexture(normalUpload);

    static uint8_t neutralOrm[4] __attribute__((aligned(16))) = {255, 255, 0, 255};
    TextureUpload ormUpload;
    memset(&ormUpload, 0, sizeof(ormUpload));
    ormUpload.width = 1;
    ormUpload.height = 1;
    ormUpload.format = PixelFormat::RGBA32;
    ormUpload.filter = TextureFilter::Linear;
    ormUpload.levelPtr[0] = neutralOrm;
    ormUpload.mipCount = 1;
    m_defaultOrmTexture = UploadTexture(ormUpload);

    return m_defaultNormalTexture != 0 && m_defaultOrmTexture != 0;
}

bool Deko3dRenderer::CreateShadowTarget()
{
    DkImageLayoutMaker colorMaker;
    dkImageLayoutMakerDefaults(&colorMaker, m_device);
    colorMaker.flags = DkImageFlags_UsageRender;
    colorMaker.format = DkImageFormat_RGBA8_Unorm;
    colorMaker.dimensions[0] = GFX_SHADOW_MAP_SIZE;
    colorMaker.dimensions[1] = GFX_SHADOW_MAP_SIZE;
    DkImageLayout colorLayout;
    dkImageLayoutInitialize(&colorLayout, &colorMaker);

    DkImageLayoutMaker depthMaker;
    dkImageLayoutMakerDefaults(&depthMaker, m_device);
    depthMaker.flags = DkImageFlags_UsageRender;
    depthMaker.format = DkImageFormat_Z24S8;
    depthMaker.dimensions[0] = GFX_SHADOW_MAP_SIZE;
    depthMaker.dimensions[1] = GFX_SHADOW_MAP_SIZE;
    DkImageLayout depthLayout;
    dkImageLayoutInitialize(&depthLayout, &depthMaker);

    m_shadowColorMemory =
        CreateBlock(AlignUp(static_cast<uint32_t>(dkImageLayoutGetSize(&colorLayout)), dkImageLayoutGetAlignment(&colorLayout)), DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    m_shadowDepthMemory =
        CreateBlock(AlignUp(static_cast<uint32_t>(dkImageLayoutGetSize(&depthLayout)), dkImageLayoutGetAlignment(&depthLayout)), DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    if (!m_shadowColorMemory || !m_shadowDepthMemory)
        return false;

    dkImageInitialize(&m_shadowColorImage, &colorLayout, m_shadowColorMemory, 0);
    dkImageInitialize(&m_shadowDepthImage, &depthLayout, m_shadowDepthMemory, 0);

    m_shadowTextureSlot = FindFreeTextureSlot();
    if (m_shadowTextureSlot < 0)
        return false;

    BeginTransfer();
    WriteImageDescriptor(m_transferCmdbuf, static_cast<uint32_t>(m_shadowTextureSlot), m_shadowColorImage);
    FinishTransfer();

    Texture& texture = m_textures[m_shadowTextureSlot];
    texture.image = m_shadowColorImage;
    texture.memory = nullptr; // owned by m_shadowColorMemory, released directly in Destroy()
    texture.sampler = DEKO3D_SAMPLER_NEAREST;
    texture.used = true;
    return true;
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
    if (!texture.used || static_cast<int>(handle - 1u) == m_imageTextureSlot || static_cast<int>(handle - 1u) == m_shadowTextureSlot)
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

void Deko3dRenderer::RenderShadowMap(const DrawLists& lists)
{
    m_shadowActive = false;

    const LightID casterId = lists.GetShadowCasterLight();
    if (casterId < 0 || casterId >= GFX_MAX_LIGHTS)
        return; // no caster designated this frame

    const Light3D& caster = lists.GetLights()[casterId];
    // Only a directional light can cast the shadow map -- see the member
    // comment on SetShadowCasterLight in Renderer.h and BuildLightViewProjection
    // in StagedGeometry.h.
    if (caster.intensity <= 0.0f || caster.type != LightType::Directional)
        return;

    const uint32_t dynStart = m_geometry.DynamicVertexStart();
    if (dynStart >= m_frame3DVertices)
        return; // nothing dynamic uploaded this frame; leave the map unsampled (see scene_pbr.frag.glsl)
    uint32_t dynCount = m_frame3DVertices - dynStart;
    dynCount -= dynCount % 3u; // defensive: an overflow clamp could have cut mid-triangle
    if (dynCount == 0)
        return;

    // A frustum centred on the camera, not the whole level: this pass only
    // ever covers dynamic (model/primitive) geometry, which clusters near
    // wherever the camera is looking, not the static world.
    const float kShadowHalfExtent = 24.0f;
    const float kShadowDepthExtent = 120.0f;
    StagedGeometry::BuildLightViewProjection(caster.direction, lists.GetCamera3D().position, kShadowHalfExtent, kShadowDepthExtent, true, m_lastLightViewProj);

    DkImageView colorView;
    dkImageViewDefaults(&colorView, &m_shadowColorImage);
    DkImageView depthView;
    dkImageViewDefaults(&depthView, &m_shadowDepthImage);

    // A dedicated scene into the shadow target via the transfer command
    // buffer, exactly like RenderToImage3D's own one-off render: deko3d
    // forbids a nested scene, and this keeps the shadow pass fully
    // independent of whatever BeginPass/BindPassState state the main frame's
    // own command buffer ends up in afterwards.
    BeginTransfer();
    // Cleared to far (1.0, encoded into every channel -- see
    // scene_shadow.frag.glsl) rather than the scene's own background colour:
    // an uncovered shadow-map texel must read back as "nothing occludes
    // here", not whatever the sky happens to be this frame.
    BeginPass(m_transferCmdbuf, colorView, depthView, GFX_SHADOW_MAP_SIZE, GFX_SHADOW_MAP_SIZE, Color3{1.0f, 1.0f, 1.0f}, m_frameSlice);

    DkShader const* shadowShaders[] = {&m_shadowVertexShader, &m_shadowFragmentShader};
    dkCmdBufBindShaders(m_transferCmdbuf, DkStageFlag_GraphicsMask, shadowShaders, 2);
    ApplyDepthBlendState(m_transferCmdbuf, PassKind::World);

    const DkGpuAddr shadowAddr = dkMemBlockGetGpuAddr(m_shadowUniformMemory);
    dkCmdBufPushConstants(m_transferCmdbuf, shadowAddr, DEKO3D_UNIFORM_BYTES, 0, DEKO3D_UNIFORM_BYTES, m_lastLightViewProj);
    dkCmdBufBindUniformBuffer(m_transferCmdbuf, DkStage_Vertex, DEKO3D_SHADOW_BINDING, shadowAddr, DEKO3D_UNIFORM_BYTES);

    // One draw over every dynamic vertex: no per-material texture binding to
    // change between runs (no alpha-mask cutout support yet -- see
    // scene_shadow.frag.glsl), so there is nothing run boundaries buy it.
    dkCmdBufDraw(m_transferCmdbuf, DkPrimitive_Triangles, dynCount, 1, dynStart, 0);
    FinishTransfer();

    m_shadowActive = true;
    m_shadowCasterIndex = casterId;
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

void Deko3dRenderer::ApplyDepthBlendState(DkCmdBuf cmdbuf, PassKind kind)
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
}

void Deko3dRenderer::BindPassState(DkCmdBuf cmdbuf, PassKind kind, const float matrix[16])
{
    ApplyDepthBlendState(cmdbuf, kind);
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

void Deko3dRenderer::DrawPbrRuns(DkCmdBuf cmdbuf, const StagedGeometry::DrawRun* runs, uint32_t runCount, uint32_t uploaded)
{
    const DkGpuAddr materialBase = dkMemBlockGetGpuAddr(m_pbrMaterialUniformMemory);
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

        const StagedGeometry::RunMaterial& mat = runs[i].material;
        MaterialUniformCpu material;
        memcpy(material.baseColor, mat.baseColor, sizeof(material.baseColor));
        material.emissive[0] = mat.emissive[0];
        material.emissive[1] = mat.emissive[1];
        material.emissive[2] = mat.emissive[2];
        material.emissive[3] = 0.0f;
        material.mrna[0] = mat.metallic;
        material.mrna[1] = mat.roughness;
        material.mrna[2] = mat.normalScale;
        material.mrna[3] = mat.alphaCutoff;
        material.matFlags[0] = (mat.flags & MATERIAL_FLAG_ALPHA_MASK) ? 1.0f : 0.0f;
        material.matFlags[1] = material.matFlags[2] = material.matFlags[3] = 0.0f;

        const DkGpuAddr materialAddr = materialBase + i * DEKO3D_PBR_MATERIAL_STRIDE;
        dkCmdBufPushConstants(cmdbuf, materialAddr, sizeof(MaterialUniformCpu), 0, sizeof(MaterialUniformCpu), &material);
        dkCmdBufBindUniformBuffer(cmdbuf, DkStage_Fragment, DEKO3D_PBR_MATERIAL_BINDING, materialAddr, sizeof(MaterialUniformCpu));

        const uint32_t albedoHandle = runs[i].texture ? runs[i].texture : m_whiteTexture;
        const uint32_t albedoSlot = (albedoHandle && albedoHandle <= DEKO3D_MAX_RESIDENT_TEXTURES && m_textures[albedoHandle - 1u].used) ? albedoHandle - 1u : m_whiteTexture - 1u;
        const uint32_t normalHandle = mat.normalTexture ? mat.normalTexture : m_defaultNormalTexture;
        const uint32_t normalSlot = (normalHandle && normalHandle <= DEKO3D_MAX_RESIDENT_TEXTURES && m_textures[normalHandle - 1u].used) ? normalHandle - 1u : m_defaultNormalTexture - 1u;
        const uint32_t ormHandle = mat.ormTexture ? mat.ormTexture : m_defaultOrmTexture;
        const uint32_t ormSlot = (ormHandle && ormHandle <= DEKO3D_MAX_RESIDENT_TEXTURES && m_textures[ormHandle - 1u].used) ? ormHandle - 1u : m_defaultOrmTexture - 1u;

        dkCmdBufBindTexture(cmdbuf, DkStage_Fragment, 0, dkMakeTextureHandle(albedoSlot, m_textures[albedoSlot].sampler));
        dkCmdBufBindTexture(cmdbuf, DkStage_Fragment, 1, dkMakeTextureHandle(normalSlot, m_textures[normalSlot].sampler));
        dkCmdBufBindTexture(cmdbuf, DkStage_Fragment, 2, dkMakeTextureHandle(ormSlot, m_textures[ormSlot].sampler));

        dkCmdBufDraw(cmdbuf, DkPrimitive_Triangles, count, 1, first, 0);
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

    RenderShadowMap(m_drawLists);

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

        DkShader const* pbrShaders[] = {&m_pbrVertexShader, &m_pbrFragmentShader};
        dkCmdBufBindShaders(m_frameCmdbuf, DkStageFlag_GraphicsMask, pbrShaders, 2);
        ApplyDepthBlendState(m_frameCmdbuf, PassKind::World);

        // Frame-constant uniforms (camera, ambient, lights, shadow caster),
        // assembled into one std140-laid-out buffer and uploaded once here
        // rather than per run.
        FrameUniformsCpu frame;
        memcpy(frame.viewProj, matrix, sizeof(frame.viewProj));
        memcpy(frame.lightViewProj, m_lastLightViewProj, sizeof(frame.lightViewProj));

        const Camera3D& camera = m_drawLists.GetCamera3D();
        frame.cameraPos[0] = camera.position.x;
        frame.cameraPos[1] = camera.position.y;
        frame.cameraPos[2] = camera.position.z;
        frame.cameraPos[3] = 0.0f;

        const Color3& ambient = m_drawLists.GetAmbientLight();
        frame.ambient[0] = ambient.r;
        frame.ambient[1] = ambient.g;
        frame.ambient[2] = ambient.b;
        frame.ambient[3] = 0.0f;

        const Light3D* lights = m_drawLists.GetLights();
        for (uint32_t i = 0; i < GFX_MAX_LIGHTS; ++i)
        {
            const Light3D& l = lights[i];
            const bool directional = (l.type == LightType::Directional);
            frame.lightPosOrDir[i * 4 + 0] = directional ? l.direction.x : l.position.x;
            frame.lightPosOrDir[i * 4 + 1] = directional ? l.direction.y : l.position.y;
            frame.lightPosOrDir[i * 4 + 2] = directional ? l.direction.z : l.position.z;
            frame.lightPosOrDir[i * 4 + 3] = directional ? 0.0f : 1.0f;
            frame.lightColorIntensity[i * 4 + 0] = l.color.r;
            frame.lightColorIntensity[i * 4 + 1] = l.color.g;
            frame.lightColorIntensity[i * 4 + 2] = l.color.b;
            frame.lightColorIntensity[i * 4 + 3] = l.intensity; // <= 0 means "off"; the shader skips it
            frame.lightRange[i * 4 + 0] = l.range;
            frame.lightRange[i * 4 + 1] = 0.0f;
            frame.lightRange[i * 4 + 2] = 0.0f;
            frame.lightRange[i * 4 + 3] = 0.0f;
        }
        frame.shadowCaster[0] = m_shadowActive ? static_cast<float>(m_shadowCasterIndex) : -1.0f;
        frame.shadowCaster[1] = frame.shadowCaster[2] = frame.shadowCaster[3] = 0.0f;

        const DkGpuAddr frameAddr = dkMemBlockGetGpuAddr(m_pbrFrameUniformMemory);
        dkCmdBufPushConstants(m_frameCmdbuf, frameAddr, sizeof(FrameUniformsCpu), 0, sizeof(FrameUniformsCpu), &frame);
        dkCmdBufBindUniformBuffer(m_frameCmdbuf, DkStage_Vertex, DEKO3D_PBR_FRAME_BINDING, frameAddr, sizeof(FrameUniformsCpu));
        dkCmdBufBindUniformBuffer(m_frameCmdbuf, DkStage_Fragment, DEKO3D_PBR_FRAME_BINDING, frameAddr, sizeof(FrameUniformsCpu));

        const uint32_t shadowSlot = (m_shadowTextureSlot >= 0) ? static_cast<uint32_t>(m_shadowTextureSlot) : (m_whiteTexture - 1u);
        dkCmdBufBindTexture(m_frameCmdbuf, DkStage_Fragment, 3, dkMakeTextureHandle(shadowSlot, m_textures[shadowSlot].sampler));

        const StagedGeometry::DrawRun* runs = m_geometry.Runs();
        const uint32_t runCount = (m_geometry.RunCount() < GFX_MAX_DRAW_RUNS) ? m_geometry.RunCount() : GFX_MAX_DRAW_RUNS;
        DrawPbrRuns(m_frameCmdbuf, runs, runCount, m_frame3DVertices);
    }
    if (m_frame2DVertices > 0)
    {
        StagedGeometry::BuildOrtho2D(m_width, m_height, false, matrix);

        DkShader const* flatShaders[] = {&m_vertexShader, &m_fragmentShader};
        dkCmdBufBindShaders(m_frameCmdbuf, DkStageFlag_GraphicsMask, flatShaders, 2);
        // The PBR pass above may have repointed vertex-stage binding 0 at its
        // own frame-uniforms buffer; the flat vertex shader's SceneTransform
        // block also declares binding=0, so it must be pointed back at the
        // flat uniform buffer before this draws (mirrors OpenGl.cpp (nx)'s
        // identical restore for the same reason).
        dkCmdBufBindUniformBuffer(m_frameCmdbuf, DkStage_Vertex, 0, dkMemBlockGetGpuAddr(m_uniformMemory), DEKO3D_UNIFORM_BYTES);
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
void Deko3dRenderer::SetLight3D(LightID id, const Light3D& light) { m_drawLists.SetLight3D(id, light); }
void Deko3dRenderer::SetAmbientLight(const Color3& color) { m_drawLists.SetAmbientLight(color); }
void Deko3dRenderer::SetShadowCasterLight(LightID id) { m_drawLists.SetShadowCasterLight(id); }

bool Deko3dRenderer::IsInitialized() const { return m_initialized; }

void Deko3dRenderer::Destroy()
{
    if (m_queue)
        dkQueueWaitIdle(m_queue);

    if (m_swapchain)
        dkSwapchainDestroy(m_swapchain);
    m_swapchain = nullptr;

    DestroyImageTarget();

    if (m_shadowColorMemory)
        dkMemBlockDestroy(m_shadowColorMemory);
    if (m_shadowDepthMemory)
        dkMemBlockDestroy(m_shadowDepthMemory);
    m_shadowColorMemory = nullptr;
    m_shadowDepthMemory = nullptr;

    for (int i = 0; i < DEKO3D_MAX_RESIDENT_TEXTURES; ++i)
    {
        if (m_textures[i].used && m_textures[i].memory)
            dkMemBlockDestroy(m_textures[i].memory);
    }
    memset(m_textures, 0, sizeof(m_textures));
    m_imageTextureSlot = -1;
    m_shadowTextureSlot = -1;
    m_whiteTexture = 0;
    m_defaultNormalTexture = 0;
    m_defaultOrmTexture = 0;

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

    const DkMemBlock* blocks[] = {
        &m_uniformMemory, &m_pbrFrameUniformMemory, &m_pbrMaterialUniformMemory, &m_shadowUniformMemory, &m_descriptorMemory, &m_shaderMemory, &m_frameCommandMemory, &m_transferCommandMemory};
    for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); ++i)
    {
        if (*blocks[i])
            dkMemBlockDestroy(*blocks[i]);
    }
    m_uniformMemory = nullptr;
    m_pbrFrameUniformMemory = nullptr;
    m_pbrMaterialUniformMemory = nullptr;
    m_shadowUniformMemory = nullptr;
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
