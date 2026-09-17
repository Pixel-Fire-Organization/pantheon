#include "platform/vita/renderer/Gxm.h"

#include <cstdlib>
#include <cstring>

#include "Macros.h"
#include "PlatformConstants.h"
#include "core/EngineDebug.h"
#include "core/EngineMemory.h"
#include "graphics/TextureExpand.h"
#include "platform/Platform.h"

#include "platform/vita/CommonDialog.h"

extern "C" {
#include <psp2/common_dialog.h>
#include <psp2/display.h>
}

#include "scene_f.h"
#include "scene_v.h"

namespace
{
    const uint32_t kDisplayStride = GFX_SCREEN_WIDTH;
    const uint32_t kDisplayBytes = GFX_SCREEN_WIDTH * GFX_SCREEN_HEIGHT * 4u;

    struct DisplayCallbackData
    {
        void* address;
    };

    const uint32_t kClearQuadVertices = 6;

    // Not 1x1: a linear texture that narrow produces a stride below the
    // hardware minimum, and the upload is refused.
    const uint32_t kWhiteTextureSize = 8;

    uint32_t AlignUp(uint32_t value, uint32_t alignment) { return (value + alignment - 1u) & ~(alignment - 1u); }

    void* GpuAlloc(SceKernelMemBlockType type, uint32_t size, uint32_t alignment, SceGxmMemoryAttribFlags attribs, SceUID* outUid)
    {
        if (type == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW)
            size = AlignUp(size, 256u * 1024u);
        else
            size = AlignUp(size, 4u * 1024u);

        const SceUID uid = sceKernelAllocMemBlock("gxm_gpu", type, size, nullptr);
        if (uid < 0)
        {
            *outUid = -1;
            return nullptr;
        }

        void* base = nullptr;
        if (sceKernelGetMemBlockBase(uid, &base) < 0 || !base)
        {
            sceKernelFreeMemBlock(uid);
            *outUid = -1;
            return nullptr;
        }

        if (sceGxmMapMemory(base, size, attribs) < 0)
        {
            sceKernelFreeMemBlock(uid);
            *outUid = -1;
            return nullptr;
        }

        UNUSED_VAR(alignment);
        *outUid = uid;
        return base;
    }

    void GpuFree(SceUID uid)
    {
        if (uid < 0)
            return;
        void* base = nullptr;
        if (sceKernelGetMemBlockBase(uid, &base) == 0 && base)
            sceGxmUnmapMemory(base);
        sceKernelFreeMemBlock(uid);
    }

    void* FragmentUsseAlloc(uint32_t size, SceUID* outUid, uint32_t* outOffset)
    {
        size = AlignUp(size, 4u * 1024u);
        const SceUID uid = sceKernelAllocMemBlock("gxm_fragment_usse", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size, nullptr);
        if (uid < 0)
        {
            *outUid = -1;
            return nullptr;
        }
        void* base = nullptr;
        if (sceKernelGetMemBlockBase(uid, &base) < 0 || !base || sceGxmMapFragmentUsseMemory(base, size, outOffset) < 0)
        {
            sceKernelFreeMemBlock(uid);
            *outUid = -1;
            return nullptr;
        }
        *outUid = uid;
        return base;
    }

    void* VertexUsseAlloc(uint32_t size, SceUID* outUid, uint32_t* outOffset)
    {
        size = AlignUp(size, 4u * 1024u);
        const SceUID uid = sceKernelAllocMemBlock("gxm_vertex_usse", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size, nullptr);
        if (uid < 0)
        {
            *outUid = -1;
            return nullptr;
        }
        void* base = nullptr;
        if (sceKernelGetMemBlockBase(uid, &base) < 0 || !base || sceGxmMapVertexUsseMemory(base, size, outOffset) < 0)
        {
            sceKernelFreeMemBlock(uid);
            *outUid = -1;
            return nullptr;
        }
        *outUid = uid;
        return base;
    }

    void DisplayCallback(const void* callbackData)
    {
        const DisplayCallbackData* data = static_cast<const DisplayCallbackData*>(callbackData);

        SceDisplayFrameBuf fb;
        memset(&fb, 0, sizeof(fb));
        fb.size = sizeof(fb);
        fb.base = data->address;
        fb.pitch = kDisplayStride;
        fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
        fb.width = GFX_SCREEN_WIDTH;
        fb.height = GFX_SCREEN_HEIGHT;
        sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
        sceDisplayWaitVblankStart();
    }

    void* PatcherHostAlloc(void* userData, unsigned int size)
    {
        UNUSED_VAR(userData);
        return malloc(size);
    }

    void PatcherHostFree(void* userData, void* mem)
    {
        UNUSED_VAR(userData);
        free(mem);
    }
} // namespace

GxmRenderer::GxmRenderer(const EngineConfig& config) :
    m_context(nullptr), m_vdmRing(nullptr), m_vertexRing(nullptr), m_fragmentRing(nullptr), m_fragmentUsseRing(nullptr), m_vdmRingUid(-1), m_vertexRingUid(-1), m_fragmentRingUid(-1),
    m_fragmentUsseRingUid(-1), m_hostMem(nullptr), m_renderTarget(nullptr), m_backBufferIndex(0), m_frontBufferIndex(GFX_GXM_DISPLAY_BUFFERS - 1), m_depthData(nullptr), m_depthUid(-1),
    m_shaderPatcher(nullptr), m_patcherBuffer(nullptr), m_patcherVertexUsse(nullptr), m_patcherFragmentUsse(nullptr), m_patcherBufferUid(-1), m_patcherVertexUsseUid(-1), m_patcherFragmentUsseUid(-1),
    m_vertexProgram(nullptr), m_fragmentProgram(nullptr), m_viewProjParam(nullptr), m_vertexBuffer(nullptr), m_indexBuffer(nullptr), m_vertexBufferUid(-1), m_indexBufferUid(-1), m_whiteTexture(0),
    m_geometry(), m_clearColor(Color3{0.0f, 0.0f, 0.0f}), m_width(GFX_SCREEN_WIDTH), m_height(GFX_SCREEN_HEIGHT), m_frameVertices(0), m_frame3DVertices(0), m_frame2DVertices(0), m_reportedOverflow(0),
    m_frameStats(), m_sceneActive(false), m_initialized(false), m_imageRenderTarget(nullptr), m_imageColorData(nullptr), m_imageColorUid(-1), m_imageDepthData(nullptr), m_imageDepthUid(-1),
    m_imageWidth(0), m_imageHeight(0), m_imageTextureSlot(-1)
{
    UNUSED_VAR(config);
    memset(m_displayBuffers, 0, sizeof(m_displayBuffers));
    memset(m_textures, 0, sizeof(m_textures));
    for (uint32_t i = 0; i < GFX_GXM_DISPLAY_BUFFERS; ++i)
        m_displayBuffers[i].uid = -1;

    if (!InitPrimitives() || !InitGraphics() || !InitRenderTarget() || !InitShaders() || !InitBuffers())
    {
        Engine_LogError("GxmRenderer: initialisation failed");
        DestroyGraphics();
        return;
    }

    m_initialized = true;
    Engine_LogInfo("GxmRenderer: ready (%ux%u, %u vertex ceiling)", m_width, m_height, GFX_GXM_MAX_FRAME_VERTICES);
}

bool GxmRenderer::InitPrimitives()
{
    float* arena = static_cast<float*>(Engine_GetSlot(ARENA_RENDERER, 0));
    if (!arena)
    {
        Engine_LogError("GxmRenderer: failed to retrieve ARENA_RENDERER slot 0");
        return false;
    }
    m_drawLists.Init(arena);
    return true;
}

bool GxmRenderer::InitGraphics()
{
    SceGxmInitializeParams initParams;
    memset(&initParams, 0, sizeof(initParams));
    initParams.flags = 0;
    initParams.displayQueueMaxPendingCount = GFX_GXM_DISPLAY_BUFFERS - 1u;
    initParams.displayQueueCallback = &DisplayCallback;
    initParams.displayQueueCallbackDataSize = sizeof(DisplayCallbackData);
    initParams.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;

    if (sceGxmInitialize(&initParams) < 0)
    {
        Engine_LogError("GxmRenderer: sceGxmInitialize failed");
        return false;
    }

    m_vdmRing = GpuAlloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE, 4u, SCE_GXM_MEMORY_ATTRIB_READ, &m_vdmRingUid);
    m_vertexRing = GpuAlloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE, 4u, SCE_GXM_MEMORY_ATTRIB_READ, &m_vertexRingUid);
    m_fragmentRing = GpuAlloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE, 4u, SCE_GXM_MEMORY_ATTRIB_READ, &m_fragmentRingUid);

    uint32_t fragmentUsseOffset = 0;
    m_fragmentUsseRing = FragmentUsseAlloc(SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE, &m_fragmentUsseRingUid, &fragmentUsseOffset);

    if (!m_vdmRing || !m_vertexRing || !m_fragmentRing || !m_fragmentUsseRing)
    {
        Engine_LogError("GxmRenderer: could not reserve the command ring buffers");
        return false;
    }

    m_hostMem = malloc(SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE);
    if (!m_hostMem)
        return false;

    SceGxmContextParams contextParams;
    memset(&contextParams, 0, sizeof(contextParams));
    contextParams.hostMem = m_hostMem;
    contextParams.hostMemSize = SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE;
    contextParams.vdmRingBufferMem = m_vdmRing;
    contextParams.vdmRingBufferMemSize = SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE;
    contextParams.vertexRingBufferMem = m_vertexRing;
    contextParams.vertexRingBufferMemSize = SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE;
    contextParams.fragmentRingBufferMem = m_fragmentRing;
    contextParams.fragmentRingBufferMemSize = SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE;
    contextParams.fragmentUsseRingBufferMem = m_fragmentUsseRing;
    contextParams.fragmentUsseRingBufferMemSize = SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE;
    contextParams.fragmentUsseRingBufferOffset = fragmentUsseOffset;

    if (sceGxmCreateContext(&contextParams, &m_context) < 0)
    {
        Engine_LogError("GxmRenderer: sceGxmCreateContext failed");
        return false;
    }
    return true;
}

bool GxmRenderer::InitRenderTarget()
{
    SceGxmRenderTargetParams targetParams;
    memset(&targetParams, 0, sizeof(targetParams));
    targetParams.flags = 0;
    targetParams.width = GFX_SCREEN_WIDTH;
    targetParams.height = GFX_SCREEN_HEIGHT;
    targetParams.scenesPerFrame = 1;
    targetParams.multisampleMode = SCE_GXM_MULTISAMPLE_NONE;
    targetParams.multisampleLocations = 0;
    targetParams.driverMemBlock = -1;

    if (sceGxmCreateRenderTarget(&targetParams, &m_renderTarget) < 0)
    {
        Engine_LogError("GxmRenderer: sceGxmCreateRenderTarget failed");
        return false;
    }

    for (uint32_t i = 0; i < GFX_GXM_DISPLAY_BUFFERS; ++i)
    {
        DisplayBuffer& buf = m_displayBuffers[i];
        buf.address = GpuAlloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, kDisplayBytes, SCE_GXM_COLOR_SURFACE_ALIGNMENT, SCE_GXM_MEMORY_ATTRIB_RW, &buf.uid);
        if (!buf.address)
        {
            Engine_LogError("GxmRenderer: could not reserve display buffer %u", i);
            return false;
        }
        memset(buf.address, 0, kDisplayBytes);

        if (sceGxmColorSurfaceInit(&buf.surface, SCE_GXM_COLOR_FORMAT_A8B8G8R8, SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE, SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, GFX_SCREEN_WIDTH,
                                   GFX_SCREEN_HEIGHT, kDisplayStride, buf.address) < 0)
        {
            Engine_LogError("GxmRenderer: sceGxmColorSurfaceInit failed for buffer %u", i);
            return false;
        }
        if (sceGxmSyncObjectCreate(&buf.sync) < 0)
        {
            Engine_LogError("GxmRenderer: sceGxmSyncObjectCreate failed for buffer %u", i);
            return false;
        }
    }

    const uint32_t alignedWidth = AlignUp(GFX_SCREEN_WIDTH, SCE_GXM_TILE_SIZEX);
    const uint32_t alignedHeight = AlignUp(GFX_SCREEN_HEIGHT, SCE_GXM_TILE_SIZEY);
    const uint32_t depthBytes = alignedWidth * alignedHeight * 4u;

    m_depthData = GpuAlloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, depthBytes, SCE_GXM_DEPTHSTENCIL_SURFACE_ALIGNMENT, SCE_GXM_MEMORY_ATTRIB_RW, &m_depthUid);
    if (!m_depthData)
    {
        Engine_LogError("GxmRenderer: could not reserve the depth buffer");
        return false;
    }

    if (sceGxmDepthStencilSurfaceInit(&m_depthSurface, SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24, SCE_GXM_DEPTH_STENCIL_SURFACE_TILED, alignedWidth, m_depthData, nullptr) < 0)
    {
        Engine_LogError("GxmRenderer: sceGxmDepthStencilSurfaceInit failed");
        return false;
    }
    return true;
}

bool GxmRenderer::InitShaders()
{
    static const uint32_t kPatcherBufferSize = 64u * 1024u;
    static const uint32_t kPatcherUsseSize = 64u * 1024u;

    uint32_t vertexUsseOffset = 0;
    uint32_t fragmentUsseOffset = 0;

    m_patcherBuffer = GpuAlloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, kPatcherBufferSize, 4u, SCE_GXM_MEMORY_ATTRIB_RW, &m_patcherBufferUid);
    m_patcherVertexUsse = VertexUsseAlloc(kPatcherUsseSize, &m_patcherVertexUsseUid, &vertexUsseOffset);
    m_patcherFragmentUsse = FragmentUsseAlloc(kPatcherUsseSize, &m_patcherFragmentUsseUid, &fragmentUsseOffset);

    if (!m_patcherBuffer || !m_patcherVertexUsse || !m_patcherFragmentUsse)
    {
        Engine_LogError("GxmRenderer: could not reserve shader patcher memory");
        return false;
    }

    SceGxmShaderPatcherParams patcherParams;
    memset(&patcherParams, 0, sizeof(patcherParams));
    patcherParams.userData = nullptr;
    patcherParams.hostAllocCallback = &PatcherHostAlloc;
    patcherParams.hostFreeCallback = &PatcherHostFree;
    patcherParams.bufferMem = m_patcherBuffer;
    patcherParams.bufferMemSize = kPatcherBufferSize;
    patcherParams.vertexUsseMem = m_patcherVertexUsse;
    patcherParams.vertexUsseMemSize = kPatcherUsseSize;
    patcherParams.vertexUsseOffset = vertexUsseOffset;
    patcherParams.fragmentUsseMem = m_patcherFragmentUsse;
    patcherParams.fragmentUsseMemSize = kPatcherUsseSize;
    patcherParams.fragmentUsseOffset = fragmentUsseOffset;

    if (sceGxmShaderPatcherCreate(&patcherParams, &m_shaderPatcher) < 0)
    {
        Engine_LogError("GxmRenderer: sceGxmShaderPatcherCreate failed");
        return false;
    }

    const SceGxmProgram* vertexGxp = reinterpret_cast<const SceGxmProgram*>(g_SceneVertexGxp);
    const SceGxmProgram* fragmentGxp = reinterpret_cast<const SceGxmProgram*>(g_SceneFragmentGxp);

    if (sceGxmProgramCheck(vertexGxp) < 0 || sceGxmProgramCheck(fragmentGxp) < 0)
    {
        Engine_LogError("GxmRenderer: a compiled shader failed validation");
        return false;
    }

    if (sceGxmShaderPatcherRegisterProgram(m_shaderPatcher, vertexGxp, &m_vertexProgramId) < 0 || sceGxmShaderPatcherRegisterProgram(m_shaderPatcher, fragmentGxp, &m_fragmentProgramId) < 0)
    {
        Engine_LogError("GxmRenderer: sceGxmShaderPatcherRegisterProgram failed");
        return false;
    }

    const SceGxmProgramParameter* pPosition = sceGxmProgramFindParameterByName(vertexGxp, "aPosition");
    const SceGxmProgramParameter* pTexcoord = sceGxmProgramFindParameterByName(vertexGxp, "aTexcoord");
    const SceGxmProgramParameter* pColor = sceGxmProgramFindParameterByName(vertexGxp, "aColor");
    m_viewProjParam = sceGxmProgramFindParameterByName(vertexGxp, "uViewProj");

    if (!pPosition || !pTexcoord || !pColor || !m_viewProjParam)
    {
        Engine_LogError("GxmRenderer: the vertex shader is missing an expected parameter");
        return false;
    }

    SceGxmVertexAttribute attributes[3];
    memset(attributes, 0, sizeof(attributes));

    attributes[0].streamIndex = 0;
    attributes[0].offset = offsetof(StagedGeometry::Vertex, x);
    attributes[0].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
    attributes[0].componentCount = 3;
    attributes[0].regIndex = sceGxmProgramParameterGetResourceIndex(pPosition);

    attributes[1].streamIndex = 0;
    attributes[1].offset = offsetof(StagedGeometry::Vertex, u);
    attributes[1].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
    attributes[1].componentCount = 2;
    attributes[1].regIndex = sceGxmProgramParameterGetResourceIndex(pTexcoord);

    attributes[2].streamIndex = 0;
    attributes[2].offset = offsetof(StagedGeometry::Vertex, r);
    attributes[2].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
    attributes[2].componentCount = 4;
    attributes[2].regIndex = sceGxmProgramParameterGetResourceIndex(pColor);

    SceGxmVertexStream stream;
    memset(&stream, 0, sizeof(stream));
    stream.stride = sizeof(StagedGeometry::Vertex);
    stream.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_32BIT;

    if (sceGxmShaderPatcherCreateVertexProgram(m_shaderPatcher, m_vertexProgramId, attributes, 3, &stream, 1, &m_vertexProgram) < 0)
    {
        Engine_LogError("GxmRenderer: sceGxmShaderPatcherCreateVertexProgram failed");
        return false;
    }

    SceGxmBlendInfo blendInfo;
    memset(&blendInfo, 0, sizeof(blendInfo));
    blendInfo.colorMask = SCE_GXM_COLOR_MASK_ALL;
    blendInfo.colorFunc = SCE_GXM_BLEND_FUNC_ADD;
    blendInfo.alphaFunc = SCE_GXM_BLEND_FUNC_ADD;
    blendInfo.colorSrc = SCE_GXM_BLEND_FACTOR_SRC_ALPHA;
    blendInfo.colorDst = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendInfo.alphaSrc = SCE_GXM_BLEND_FACTOR_SRC_ALPHA;
    blendInfo.alphaDst = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

    if (sceGxmShaderPatcherCreateFragmentProgram(m_shaderPatcher, m_fragmentProgramId, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE, &blendInfo, vertexGxp, &m_fragmentProgram) < 0)
    {
        Engine_LogError("GxmRenderer: sceGxmShaderPatcherCreateFragmentProgram failed");
        return false;
    }
    return true;
}

bool GxmRenderer::InitBuffers()
{
    const uint32_t totalVertices = GFX_GXM_MAX_FRAME_VERTICES + kClearQuadVertices;
    const uint32_t vertexBytes = totalVertices * sizeof(StagedGeometry::Vertex);
    const uint32_t indexBytes = totalVertices * sizeof(uint32_t);

    m_vertexBuffer = GpuAlloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, vertexBytes, 4u, SCE_GXM_MEMORY_ATTRIB_READ, &m_vertexBufferUid);
    m_indexBuffer = GpuAlloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, indexBytes, 4u, SCE_GXM_MEMORY_ATTRIB_READ, &m_indexBufferUid);
    if (!m_vertexBuffer || !m_indexBuffer)
    {
        Engine_LogError("GxmRenderer: could not reserve the geometry buffers (%u KB)", (vertexBytes + indexBytes) / 1024u);
        return false;
    }

    uint32_t* indices = static_cast<uint32_t*>(m_indexBuffer);
    for (uint32_t i = 0; i < totalVertices; ++i)
        indices[i] = i;

    static uint8_t whitePixels[kWhiteTextureSize * kWhiteTextureSize * 4];
    memset(whitePixels, 0xFF, sizeof(whitePixels));

    TextureUpload white;
    memset(&white, 0, sizeof(white));
    white.levelPtr[0] = whitePixels;
    white.mipCount = 1;
    white.width = kWhiteTextureSize;
    white.height = kWhiteTextureSize;
    white.format = PixelFormat::RGBA32;

    m_whiteTexture = UploadTexture(white);
    if (!m_whiteTexture)
    {
        Engine_LogError("GxmRenderer: could not create the untextured fallback");
        return false;
    }
    return true;
}

uint32_t GxmRenderer::UploadTexture(const TextureUpload& upload)
{
    const uint32_t width = static_cast<uint32_t>(upload.width);
    const uint32_t height = static_cast<uint32_t>(upload.height);
    if (width == 0 || height == 0)
        return 0;

    int slot = -1;
    for (int i = 0; i < GXM_MAX_RESIDENT_TEXTURES; ++i)
    {
        if (!m_textures[i].used)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
    {
        Engine_LogError("GxmRenderer: texture registry full (%d)", GXM_MAX_RESIDENT_TEXTURES);
        return 0;
    }

    const uint32_t bytes = width * height * 4u;

    SceUID uid = -1;
    void* data = GpuAlloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, bytes, SCE_GXM_TEXTURE_ALIGNMENT, SCE_GXM_MEMORY_ATTRIB_READ, &uid);
    if (!data)
    {
        Engine_LogError("GxmRenderer: could not reserve %u KB for a %ux%u texture", bytes / 1024u, width, height);
        return 0;
    }

    if (!Gfx_ExpandToRgba8(upload, static_cast<uint8_t*>(data), bytes))
    {
        GpuFree(uid);
        Engine_LogError("GxmRenderer: could not expand a %ux%u texture", width, height);
        return 0;
    }

    Texture& tex = m_textures[slot];
    if (sceGxmTextureInitLinear(&tex.texture, data, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8, width, height, 1) < 0)
    {
        GpuFree(uid);
        Engine_LogError("GxmRenderer: sceGxmTextureInitLinear failed for a %ux%u texture", width, height);
        return 0;
    }

    const SceGxmTextureFilter filter = (upload.filter == TextureFilter::Nearest) ? SCE_GXM_TEXTURE_FILTER_POINT : SCE_GXM_TEXTURE_FILTER_LINEAR;
    sceGxmTextureSetMinFilter(&tex.texture, filter);
    sceGxmTextureSetMagFilter(&tex.texture, filter);
    sceGxmTextureSetUAddrMode(&tex.texture, SCE_GXM_TEXTURE_ADDR_REPEAT);
    sceGxmTextureSetVAddrMode(&tex.texture, SCE_GXM_TEXTURE_ADDR_REPEAT);

    tex.data = data;
    tex.uid = uid;
    tex.used = true;

    return static_cast<uint32_t>(slot) + 1u;
}

void GxmRenderer::ReleaseTexture(uint32_t handle)
{
    if (handle == 0 || handle > GXM_MAX_RESIDENT_TEXTURES)
        return;

    Texture& tex = m_textures[handle - 1u];
    if (!tex.used)
        return;

    sceGxmFinish(m_context);

    GpuFree(tex.uid);
    memset(&tex, 0, sizeof(tex));
    tex.uid = -1;
}

// ---------------------------------------------------------------------------
// Render-to-image (Ui_Image3D)
// ---------------------------------------------------------------------------

void GxmRenderer::DestroyImageTarget()
{
    if (m_imageDepthData)
    {
        GpuFree(m_imageDepthUid);
        m_imageDepthData = nullptr;
        m_imageDepthUid = -1;
    }
    if (m_imageColorData)
    {
        GpuFree(m_imageColorUid);
        m_imageColorData = nullptr;
        m_imageColorUid = -1;
    }
    if (m_imageRenderTarget)
    {
        sceGxmDestroyRenderTarget(m_imageRenderTarget);
        m_imageRenderTarget = nullptr;
    }
    m_imageWidth = 0;
    m_imageHeight = 0;
}

bool GxmRenderer::EnsureImageTarget(int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;
    if (m_imageColorData && m_imageWidth == width && m_imageHeight == height)
        return true;

    DestroyImageTarget();

    SceGxmRenderTargetParams targetParams;
    memset(&targetParams, 0, sizeof(targetParams));
    targetParams.flags = 0;
    targetParams.width = static_cast<uint16_t>(width);
    targetParams.height = static_cast<uint16_t>(height);
    targetParams.scenesPerFrame = 1;
    targetParams.multisampleMode = SCE_GXM_MULTISAMPLE_NONE;
    targetParams.multisampleLocations = 0;
    targetParams.driverMemBlock = -1;
    if (sceGxmCreateRenderTarget(&targetParams, &m_imageRenderTarget) < 0)
    {
        Engine_LogError("GxmRenderer: sceGxmCreateRenderTarget failed for a %dx%d image target", width, height);
        return false;
    }

    const uint32_t colorBytes = static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 4u;
    m_imageColorData = GpuAlloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, colorBytes, SCE_GXM_COLOR_SURFACE_ALIGNMENT, SCE_GXM_MEMORY_ATTRIB_RW, &m_imageColorUid);
    if (!m_imageColorData)
    {
        Engine_LogError("GxmRenderer: no CDRAM for a %dx%d image target", width, height);
        DestroyImageTarget();
        return false;
    }
    memset(m_imageColorData, 0, colorBytes);

    if (sceGxmColorSurfaceInit(&m_imageColorSurface, SCE_GXM_COLOR_FORMAT_A8B8G8R8, SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE, SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,
                               static_cast<uint32_t>(width), static_cast<uint32_t>(height), static_cast<uint32_t>(width), m_imageColorData) < 0)
    {
        Engine_LogError("GxmRenderer: sceGxmColorSurfaceInit failed for the image target");
        DestroyImageTarget();
        return false;
    }

    const uint32_t alignedWidth = AlignUp(static_cast<uint32_t>(width), SCE_GXM_TILE_SIZEX);
    const uint32_t alignedHeight = AlignUp(static_cast<uint32_t>(height), SCE_GXM_TILE_SIZEY);
    const uint32_t depthBytes = alignedWidth * alignedHeight * 4u;
    m_imageDepthData = GpuAlloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, depthBytes, SCE_GXM_DEPTHSTENCIL_SURFACE_ALIGNMENT, SCE_GXM_MEMORY_ATTRIB_RW, &m_imageDepthUid);
    if (!m_imageDepthData)
    {
        Engine_LogError("GxmRenderer: no memory for the image target's depth buffer");
        DestroyImageTarget();
        return false;
    }
    if (sceGxmDepthStencilSurfaceInit(&m_imageDepthSurface, SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24, SCE_GXM_DEPTH_STENCIL_SURFACE_TILED, alignedWidth, m_imageDepthData, nullptr) < 0)
    {
        Engine_LogError("GxmRenderer: sceGxmDepthStencilSurfaceInit failed for the image target");
        DestroyImageTarget();
        return false;
    }

    if (m_imageTextureSlot < 0)
    {
        for (int i = 0; i < GXM_MAX_RESIDENT_TEXTURES; ++i)
        {
            if (!m_textures[i].used)
            {
                m_imageTextureSlot = i;
                break;
            }
        }
        if (m_imageTextureSlot < 0)
        {
            Engine_LogError("GxmRenderer: texture registry full (%d), no slot for the image target", GXM_MAX_RESIDENT_TEXTURES);
            DestroyImageTarget();
            return false;
        }
    }

    Texture& tex = m_textures[m_imageTextureSlot];
    if (sceGxmTextureInitLinear(&tex.texture, m_imageColorData, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8, static_cast<uint32_t>(width), static_cast<uint32_t>(height), 0) < 0)
    {
        Engine_LogError("GxmRenderer: sceGxmTextureInitLinear failed for the image target");
        DestroyImageTarget();
        return false;
    }
    sceGxmTextureSetMinFilter(&tex.texture, SCE_GXM_TEXTURE_FILTER_LINEAR);
    sceGxmTextureSetMagFilter(&tex.texture, SCE_GXM_TEXTURE_FILTER_LINEAR);
    sceGxmTextureSetUAddrMode(&tex.texture, SCE_GXM_TEXTURE_ADDR_CLAMP);
    sceGxmTextureSetVAddrMode(&tex.texture, SCE_GXM_TEXTURE_ADDR_CLAMP);
    // data/uid are NOT set to m_imageColorData/m_imageColorUid: that memory is
    // owned and released by DestroyImageTarget, not by ReleaseTexture. uid
    // stays -1, GpuFree's own no-op sentinel, so a defensive ReleaseTexture
    // call on this handle is safe (it still needs sceGxmFinish first, since
    // this texture is a colour surface the GPU may still be writing).
    tex.data = m_imageColorData;
    tex.uid = -1;
    tex.used = true;

    m_imageWidth = width;
    m_imageHeight = height;
    return true;
}

uint32_t GxmRenderer::RenderToImage3D(const Renderable3D& what, const Camera3D& camera, int width, int height, const Color3& clearColor)
{
    if (!m_initialized)
        return 0;
    if (!EnsureImageTarget(width, height))
        return 0;
    if (!m_imageGeometry.BuildOne(what, m_drawLists))
        return 0;

    const uint32_t count = m_imageGeometry.Count3D();
    if (count > 0)
        memcpy(m_vertexBuffer, m_imageGeometry.Vertices3D(), count * sizeof(StagedGeometry::Vertex));

    // sceGxm forbids a second open scene: this must run fully -- begin, draw,
    // end -- before the main frame's own sceGxmBeginScene, never nested
    // inside it. Callers already promise to call RenderToImage3D during
    // game/scene/Testbed update, strictly before this frame's Render()/
    // EndFrame(), which is what keeps this true.
    if (sceGxmBeginScene(m_context, 0, m_imageRenderTarget, nullptr, nullptr, nullptr, &m_imageColorSurface, &m_imageDepthSurface) < 0)
    {
        Engine_LogError("GxmRenderer: sceGxmBeginScene failed for the image target");
        return 0;
    }

    const uint32_t* indices = static_cast<const uint32_t*>(m_indexBuffer);

    sceGxmSetFrontDepthFunc(m_context, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(m_context, SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetVertexProgram(m_context, m_vertexProgram);
    sceGxmSetFragmentProgram(m_context, m_fragmentProgram);
    sceGxmSetVertexStream(m_context, 0, m_vertexBuffer);

    static const float kIdentity[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    void* clearUniforms = nullptr;
    sceGxmReserveVertexDefaultUniformBuffer(m_context, &clearUniforms);
    sceGxmSetUniformDataF(clearUniforms, m_viewProjParam, 0, 16, kIdentity);
    if (m_whiteTexture && m_textures[m_whiteTexture - 1u].used)
        sceGxmSetFragmentTexture(m_context, 0, &m_textures[m_whiteTexture - 1u].texture);

    // Reuses the same clear-quad vertex slot the main pass keeps just past
    // its own frame budget (see DrawClearQuad); nothing else is using it at
    // this point in the frame.
    StagedGeometry::Vertex* clearQuad = static_cast<StagedGeometry::Vertex*>(m_vertexBuffer) + GFX_GXM_MAX_FRAME_VERTICES;
    static const float kCorners[6][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
    for (uint32_t i = 0; i < 6; ++i)
    {
        memset(&clearQuad[i], 0, sizeof(StagedGeometry::Vertex));
        clearQuad[i].x = kCorners[i][0];
        clearQuad[i].y = kCorners[i][1];
        clearQuad[i].r = clearColor.r;
        clearQuad[i].g = clearColor.g;
        clearQuad[i].b = clearColor.b;
        clearQuad[i].a = 1.0f;
    }
    sceGxmDraw(m_context, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U32, indices + GFX_GXM_MAX_FRAME_VERTICES, 6);

    if (count > 0)
    {
        sceGxmSetFrontDepthFunc(m_context, SCE_GXM_DEPTH_FUNC_LESS_EQUAL);
        sceGxmSetFrontDepthWriteEnable(m_context, SCE_GXM_DEPTH_WRITE_ENABLED);

        float matrix[16];
        StagedGeometry::BuildViewProjection(camera, static_cast<uint32_t>(width), static_cast<uint32_t>(height), true, matrix);
        void* uniforms = nullptr;
        sceGxmReserveVertexDefaultUniformBuffer(m_context, &uniforms);
        sceGxmSetUniformDataF(uniforms, m_viewProjParam, 0, 16, matrix);

        const StagedGeometry::DrawRun* runs = m_imageGeometry.Runs();
        for (uint32_t i = 0; i < m_imageGeometry.RunCount(); ++i)
        {
            if (runs[i].first + runs[i].count > count)
                continue;
            const uint32_t handle = runs[i].texture ? runs[i].texture : m_whiteTexture;
            if (handle && handle <= GXM_MAX_RESIDENT_TEXTURES && m_textures[handle - 1u].used)
                sceGxmSetFragmentTexture(m_context, 0, &m_textures[handle - 1u].texture);
            sceGxmDraw(m_context, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U32, indices + runs[i].first, runs[i].count);
        }
    }

    sceGxmEndScene(m_context, nullptr, nullptr);
    // sceGxmEndScene only queues the work; the result must be sampleable
    // before this call returns, since the UI compositing it this same frame
    // has no later point to pick it up.
    sceGxmFinish(m_context);

    return static_cast<uint32_t>(m_imageTextureSlot) + 1u;
}

void GxmRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale)
{
    AddPrimitiveToDrawList(primitive, position, rotation, scale, Color3{1.0f, 1.0f, 1.0f}, -1);
}

void GxmRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, Color3 color)
{
    AddPrimitiveToDrawList(primitive, position, rotation, scale, color, -1);
}

void GxmRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, int32_t textureId)
{
    AddPrimitiveToDrawList(primitive, position, rotation, scale, Color3{1.0f, 1.0f, 1.0f}, textureId);
}

void GxmRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, Color3 color, int32_t textureId)
{
    PrimitiveDrawEntry entry;
    entry.transform = Transform3D(position, rotation, scale);
    entry.color = color;
    entry.type = primitive;
    entry.textureId = textureId;
    m_drawLists.AddPrimitive(entry);
}

void GxmRenderer::AddLevelToDrawList(const Level& level) { UNUSED_VAR(level); }

void GxmRenderer::AddModelToDrawList(int32_t modelId, const Vector3& position, const Vector3& rotation, const Vector3& scale)
{
    ModelDrawEntry entry;
    entry.resourceId = modelId;
    entry.transform = Transform3D(position, rotation, scale);
    m_drawLists.AddModel(entry);
}

void GxmRenderer::AddSkyToDrawList(int32_t resourceId) { m_drawLists.SetSkyboxTexture(resourceId); }

void GxmRenderer::ClearDrawLists() { m_drawLists.Reset(false); }

void GxmRenderer::ClearFrame(const Color3& color) { m_clearColor = color; }

void GxmRenderer::DrawQuad2D(const Quad2D& quad) { m_geometry.AddQuad2D(quad); }

void GxmRenderer::DrawGrid(int32_t slices, float spacing)
{
    UNUSED_VAR(slices);
    UNUSED_VAR(spacing);
}

void GxmRenderer::BeginFrame()
{
    Platform* platform = Engine_GetPlatform();
    platform->GetFramebufferSize(&m_width, &m_height);

    m_geometry.SetFrameBudget(GFX_GXM_MAX_FRAME_VERTICES, m_width, m_height);
    m_geometry.BeginFrame();
    m_frameStats = DrawStats{};
}

void GxmRenderer::Render()
{
    Platform* platform = Engine_GetPlatform();
    const double start = platform->GetTimeSeconds();
    m_geometry.BuildFrame(m_drawLists, &m_frameStats);
    m_frameStats.geometryBuildMs = static_cast<float>((platform->GetTimeSeconds() - start) * 1000.0);
}

void GxmRenderer::UploadVertices()
{
    const uint32_t count3D = m_geometry.Count3D();
    const uint32_t count2D = m_geometry.Count2D();
    uint32_t total = count3D + count2D;

    m_frameStats.submitBufferUsedBytes = total * sizeof(StagedGeometry::Vertex);
    m_frameStats.submitBufferCapacityBytes = GFX_GXM_MAX_FRAME_VERTICES * sizeof(StagedGeometry::Vertex);

    if (total > GFX_GXM_MAX_FRAME_VERTICES)
    {
        if (total != m_reportedOverflow)
        {
            m_reportedOverflow = total;
            Engine_LogError("GxmRenderer: frame needs %u vertices, ceiling is %u; dropping the excess", total, GFX_GXM_MAX_FRAME_VERTICES);
        }
        total = GFX_GXM_MAX_FRAME_VERTICES;
    }
    else
    {
        m_reportedOverflow = 0;
    }

    // The stager reserves the interface's share before it builds world geometry,
    // so this is a backstop. Should it ever bind, world geometry yields — the
    // interface is what a player needs in order to react to the problem.
    StagedGeometry::Vertex* dst = static_cast<StagedGeometry::Vertex*>(m_vertexBuffer);
    const uint32_t take2D = (count2D < total) ? count2D : total;
    const uint32_t take3D = (total - take2D < count3D) ? (total - take2D) : count3D;

    if (take3D)
        memcpy(dst, m_geometry.Vertices3D(), take3D * sizeof(StagedGeometry::Vertex));
    if (take2D)
        memcpy(dst + take3D, m_geometry.Vertices2D(), take2D * sizeof(StagedGeometry::Vertex));

    m_frame3DVertices = take3D;
    m_frame2DVertices = take2D;
    m_frameVertices = take3D + take2D;
}

void GxmRenderer::DrawClearQuad()
{
    StagedGeometry::Vertex* quad = static_cast<StagedGeometry::Vertex*>(m_vertexBuffer) + GFX_GXM_MAX_FRAME_VERTICES;

    static const float kCorners[kClearQuadVertices][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};

    for (uint32_t i = 0; i < kClearQuadVertices; ++i)
    {
        memset(&quad[i], 0, sizeof(StagedGeometry::Vertex));
        quad[i].x = kCorners[i][0];
        quad[i].y = kCorners[i][1];
        quad[i].z = 0.0f;
        quad[i].r = m_clearColor.r;
        quad[i].g = m_clearColor.g;
        quad[i].b = m_clearColor.b;
        quad[i].a = 1.0f;
    }

    static const float kIdentity[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};

    sceGxmSetFrontDepthFunc(m_context, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(m_context, SCE_GXM_DEPTH_WRITE_DISABLED);

    sceGxmSetVertexProgram(m_context, m_vertexProgram);
    sceGxmSetFragmentProgram(m_context, m_fragmentProgram);
    sceGxmSetVertexStream(m_context, 0, m_vertexBuffer);

    void* uniforms = nullptr;
    sceGxmReserveVertexDefaultUniformBuffer(m_context, &uniforms);
    sceGxmSetUniformDataF(uniforms, m_viewProjParam, 0, 16, kIdentity);

    if (m_whiteTexture && m_textures[m_whiteTexture - 1u].used)
        sceGxmSetFragmentTexture(m_context, 0, &m_textures[m_whiteTexture - 1u].texture);

    const uint32_t* indices = static_cast<const uint32_t*>(m_indexBuffer);
    sceGxmDraw(m_context, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U32, indices + GFX_GXM_MAX_FRAME_VERTICES, kClearQuadVertices);
}

void GxmRenderer::DrawStagedGeometry()
{
    if (m_frameVertices == 0)
        return;

    const uint32_t* indices = static_cast<const uint32_t*>(m_indexBuffer);

    sceGxmSetVertexProgram(m_context, m_vertexProgram);
    sceGxmSetFragmentProgram(m_context, m_fragmentProgram);
    sceGxmSetVertexStream(m_context, 0, m_vertexBuffer);

    float matrix[16];

    if (m_frame3DVertices > 0)
    {
        sceGxmSetFrontDepthFunc(m_context, SCE_GXM_DEPTH_FUNC_LESS_EQUAL);
        sceGxmSetFrontDepthWriteEnable(m_context, SCE_GXM_DEPTH_WRITE_ENABLED);

        StagedGeometry::BuildViewProjection(m_drawLists.GetCamera3D(), m_width, m_height, true, matrix);

        void* uniforms = nullptr;
        sceGxmReserveVertexDefaultUniformBuffer(m_context, &uniforms);
        sceGxmSetUniformDataF(uniforms, m_viewProjParam, 0, 16, matrix);

        const StagedGeometry::DrawRun* runs = m_geometry.Runs();
        for (uint32_t i = 0; i < m_geometry.RunCount(); ++i)
        {
            const uint32_t first = runs[i].first;
            if (first >= m_frame3DVertices)
                continue;
            uint32_t count = runs[i].count;
            if (first + count > m_frame3DVertices)
                count = m_frame3DVertices - first;
            count -= count % 3u;
            if (!count)
                continue;

            const uint32_t handle = runs[i].texture ? runs[i].texture : m_whiteTexture;
            if (handle && handle <= GXM_MAX_RESIDENT_TEXTURES && m_textures[handle - 1u].used)
                sceGxmSetFragmentTexture(m_context, 0, &m_textures[handle - 1u].texture);

            sceGxmDraw(m_context, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U32, indices + first, count);
        }
    }

    if (m_frame2DVertices > 0)
    {
        sceGxmSetFrontDepthFunc(m_context, SCE_GXM_DEPTH_FUNC_ALWAYS);
        sceGxmSetFrontDepthWriteEnable(m_context, SCE_GXM_DEPTH_WRITE_DISABLED);

        StagedGeometry::BuildOrtho2D(m_width, m_height, true, matrix);

        void* uniforms = nullptr;
        sceGxmReserveVertexDefaultUniformBuffer(m_context, &uniforms);
        sceGxmSetUniformDataF(uniforms, m_viewProjParam, 0, 16, matrix);

        const StagedGeometry::DrawRun* runs2D = m_geometry.Runs2D();
        for (uint32_t i = 0; i < m_geometry.RunCount2D(); ++i)
        {
            const uint32_t first = runs2D[i].first;
            if (first >= m_frame2DVertices)
                continue;
            uint32_t count = runs2D[i].count;
            if (first + count > m_frame2DVertices)
                count = m_frame2DVertices - first;
            count -= count % 3u;
            if (!count)
                continue;

            const uint32_t handle = runs2D[i].texture ? runs2D[i].texture : m_whiteTexture;
            if (handle && handle <= GXM_MAX_RESIDENT_TEXTURES && m_textures[handle - 1u].used)
                sceGxmSetFragmentTexture(m_context, 0, &m_textures[handle - 1u].texture);

            sceGxmDraw(m_context, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U32, indices + m_frame3DVertices + first, count);
        }
    }
}

void GxmRenderer::EndFrame()
{
    if (!m_initialized)
        return;

    Platform* platform = Engine_GetPlatform();
    const double uploadStart = platform->GetTimeSeconds();
    UploadVertices();
    m_frameStats.geometryUploadMs = static_cast<float>((platform->GetTimeSeconds() - uploadStart) * 1000.0);

    DisplayBuffer& back = m_displayBuffers[m_backBufferIndex];

    if (sceGxmBeginScene(m_context, 0, m_renderTarget, nullptr, nullptr, back.sync, &back.surface, &m_depthSurface) < 0)
    {
        Engine_LogError("GxmRenderer: sceGxmBeginScene failed");
        return;
    }
    m_sceneActive = true;

    DrawClearQuad();
    DrawStagedGeometry();

    sceGxmEndScene(m_context, nullptr, nullptr);
    m_sceneActive = false;

    if (VitaCommonDialog_IsActive())
    {
        SceCommonDialogUpdateParam dialogParam;
        memset(&dialogParam, 0, sizeof(dialogParam));
        dialogParam.renderTarget.colorSurfaceData = back.address;
        dialogParam.renderTarget.surfaceType = SCE_GXM_COLOR_SURFACE_LINEAR;
        dialogParam.renderTarget.colorFormat = SCE_GXM_COLOR_FORMAT_A8B8G8R8;
        dialogParam.renderTarget.width = GFX_SCREEN_WIDTH;
        dialogParam.renderTarget.height = GFX_SCREEN_HEIGHT;
        dialogParam.renderTarget.strideInPixels = kDisplayStride;
        dialogParam.displaySyncObject = back.sync;
        VitaCommonDialog_SetLastResult(sceCommonDialogUpdate(&dialogParam));
    }

    DisplayCallbackData callbackData;
    callbackData.address = back.address;

    const double waitStart = platform->GetTimeSeconds();
    sceGxmDisplayQueueAddEntry(m_displayBuffers[m_frontBufferIndex].sync, back.sync, &callbackData);
    m_frameStats.presentWaitMs = static_cast<float>((platform->GetTimeSeconds() - waitStart) * 1000.0);

    m_frontBufferIndex = m_backBufferIndex;
    m_backBufferIndex = (m_backBufferIndex + 1u) % GFX_GXM_DISPLAY_BUFFERS;

    m_geometry.EndFrame();
    m_drawLists.SetLastStats(m_frameStats);
    m_drawLists.Reset(false);
}


void GxmRenderer::SetCamera3D(CameraID id, const Camera3D& camera) { m_drawLists.SetCamera3D(id, camera); }
void GxmRenderer::SetActiveCamera3D(CameraID id) { m_drawLists.SetActiveCamera3D(id); }
void GxmRenderer::SetActiveCamera2D(const Camera2D& camera) { m_drawLists.SetActiveCamera2D(camera); }

bool GxmRenderer::IsInitialized() const { return m_initialized; }

void GxmRenderer::DestroyGraphics()
{
    if (m_context)
    {
        if (m_sceneActive)
        {
            sceGxmEndScene(m_context, nullptr, nullptr);
            m_sceneActive = false;
        }
        sceGxmFinish(m_context);
    }
    sceGxmDisplayQueueFinish();

    for (int i = 0; i < GXM_MAX_RESIDENT_TEXTURES; ++i)
    {
        if (!m_textures[i].used)
            continue;
        GpuFree(m_textures[i].uid);
        memset(&m_textures[i], 0, sizeof(m_textures[i]));
    }
    m_whiteTexture = 0;

    if (m_shaderPatcher)
    {
        if (m_fragmentProgram)
            sceGxmShaderPatcherReleaseFragmentProgram(m_shaderPatcher, m_fragmentProgram);
        if (m_vertexProgram)
            sceGxmShaderPatcherReleaseVertexProgram(m_shaderPatcher, m_vertexProgram);
        sceGxmShaderPatcherUnregisterProgram(m_shaderPatcher, m_fragmentProgramId);
        sceGxmShaderPatcherUnregisterProgram(m_shaderPatcher, m_vertexProgramId);
        sceGxmShaderPatcherDestroy(m_shaderPatcher);
        m_shaderPatcher = nullptr;
    }
    m_vertexProgram = nullptr;
    m_fragmentProgram = nullptr;

    GpuFree(m_vertexBufferUid);
    GpuFree(m_indexBufferUid);
    GpuFree(m_patcherBufferUid);
    GpuFree(m_patcherVertexUsseUid);
    GpuFree(m_patcherFragmentUsseUid);
    m_vertexBufferUid = m_indexBufferUid = -1;
    m_patcherBufferUid = m_patcherVertexUsseUid = m_patcherFragmentUsseUid = -1;

    for (uint32_t i = 0; i < GFX_GXM_DISPLAY_BUFFERS; ++i)
    {
        if (m_displayBuffers[i].sync)
            sceGxmSyncObjectDestroy(m_displayBuffers[i].sync);
        GpuFree(m_displayBuffers[i].uid);
        memset(&m_displayBuffers[i], 0, sizeof(m_displayBuffers[i]));
        m_displayBuffers[i].uid = -1;
    }

    GpuFree(m_depthUid);
    m_depthUid = -1;

    // The image-target slot's own texture entry (if any) was already zeroed
    // by the texture-cleanup loop above with its uid left at -1 (see
    // EnsureImageTarget), so GpuFree there was already a safe no-op; this
    // frees the surfaces and the render target that slot pointed at.
    DestroyImageTarget();

    if (m_renderTarget)
    {
        sceGxmDestroyRenderTarget(m_renderTarget);
        m_renderTarget = nullptr;
    }

    if (m_context)
    {
        sceGxmDestroyContext(m_context);
        m_context = nullptr;
    }

    GpuFree(m_vdmRingUid);
    GpuFree(m_vertexRingUid);
    GpuFree(m_fragmentRingUid);
    GpuFree(m_fragmentUsseRingUid);
    m_vdmRingUid = m_vertexRingUid = m_fragmentRingUid = m_fragmentUsseRingUid = -1;

    free(m_hostMem);
    m_hostMem = nullptr;

    sceGxmTerminate();
}

void GxmRenderer::Shutdown()
{
    if (!m_initialized)
        return;
    DestroyGraphics();
    m_initialized = false;
}

RendererType GxmRenderer::GetRendererType() const { return RendererType::Gxm; }
DrawStats GxmRenderer::GetLastStats() const { return m_drawLists.GetLastStats(); }
Camera3D GxmRenderer::GetActiveCamera3D() const { return m_drawLists.GetCamera3D(); }

void GxmRenderer::RenderSkybox(const DrawLists& lists) { UNUSED_VAR(lists); }
void GxmRenderer::RenderPrimitives(DrawLists& lists) { UNUSED_VAR(lists); }
void GxmRenderer::RenderModels(const DrawLists& lists) { UNUSED_VAR(lists); }
