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
#include "scene_pbr_f.h"
#include "scene_pbr_v.h"
#include "scene_shadow_f.h"
#include "scene_shadow_v.h"

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
    m_vertexProgram(nullptr), m_fragmentProgram(nullptr), m_viewProjParam(nullptr), m_pbrVertexProgramId(), m_pbrFragmentProgramId(), m_pbrVertexProgram(nullptr), m_pbrFragmentProgram(nullptr),
    m_pbrViewProjParam(nullptr), m_pbrCameraPosParam(nullptr), m_pbrAmbientParam(nullptr), m_pbrLightPosOrDirParam(nullptr), m_pbrLightColorIntensityParam(nullptr), m_pbrLightRangeParam(nullptr),
    m_pbrShadowCasterParam(nullptr), m_pbrLightViewProjParam(nullptr), m_pbrBaseColorParam(nullptr), m_pbrEmissiveParam(nullptr), m_pbrMrnaParam(nullptr), m_pbrAlphaMaskParam(nullptr),
    m_shadowVertexProgramId(), m_shadowFragmentProgramId(), m_shadowVertexProgram(nullptr), m_shadowFragmentProgram(nullptr), m_shadowLightViewProjParam(nullptr), m_shadowRenderTarget(nullptr),
    m_shadowColorData(nullptr), m_shadowColorUid(-1), m_shadowDepthData(nullptr), m_shadowDepthUid(-1), m_shadowTextureSlot(-1), m_defaultNormalTexture(0), m_defaultOrmTexture(0),
    m_shadowActive(false), m_shadowCasterIndex(-1), m_vertexBuffer(nullptr), m_indexBuffer(nullptr), m_vertexBufferUid(-1), m_indexBufferUid(-1), m_whiteTexture(0), m_geometry(),
    m_clearColor(Color3{0.0f, 0.0f, 0.0f}), m_width(GFX_SCREEN_WIDTH), m_height(GFX_SCREEN_HEIGHT), m_frameVertices(0), m_frame3DVertices(0), m_frame2DVertices(0), m_reportedOverflow(0),
    m_frameStats(), m_sceneActive(false), m_initialized(false), m_imageRenderTarget(nullptr), m_imageColorData(nullptr), m_imageColorUid(-1), m_imageDepthData(nullptr), m_imageDepthUid(-1),
    m_imageWidth(0), m_imageHeight(0), m_imageTextureSlot(-1)
{
    UNUSED_VAR(config);
    memset(m_displayBuffers, 0, sizeof(m_displayBuffers));
    memset(m_textures, 0, sizeof(m_textures));
    memset(m_lastLightViewProj, 0, sizeof(m_lastLightViewProj));
    for (uint32_t i = 0; i < GFX_GXM_DISPLAY_BUFFERS; ++i)
        m_displayBuffers[i].uid = -1;

    static_assert(GFX_MAX_LIGHTS == 4, "scene_pbr_f.cg hardcodes a 4-element light array (no preprocessor-define injection for this shader tool)");
    static_assert(GFX_SHADOW_MAP_SIZE == 512, "scene_pbr_f.cg hardcodes the shadow map's texel size as 1.0/512.0");

    if (!InitPrimitives() || !InitGraphics() || !InitRenderTarget() || !InitShaders() || !InitDefaultMaterialTextures() || !InitShadowTarget() || !InitPbrShaders() || !InitShadowShaders() ||
        !InitBuffers())
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

bool GxmRenderer::InitDefaultMaterialTextures()
{
    // Flat tangent-space normal (encoded 128,128,255) and a neutral ORM
    // (occlusion=1, roughness=1, metallic=0) -- what a material with no
    // normal/ORM map of its own samples. Created through the ordinary
    // UploadTexture path (same as m_whiteTexture, see InitBuffers), so their
    // GPU memory is released by the existing texture-registry cleanup loop
    // in DestroyGraphics with no special-casing needed.
    static uint8_t normalPixels[4] = {128, 128, 255, 255};
    TextureUpload normalUpload;
    memset(&normalUpload, 0, sizeof(normalUpload));
    normalUpload.levelPtr[0] = normalPixels;
    normalUpload.mipCount = 1;
    normalUpload.width = 1;
    normalUpload.height = 1;
    normalUpload.format = PixelFormat::RGBA32;
    m_defaultNormalTexture = UploadTexture(normalUpload);

    static uint8_t ormPixels[4] = {255, 255, 0, 255};
    TextureUpload ormUpload;
    memset(&ormUpload, 0, sizeof(ormUpload));
    ormUpload.levelPtr[0] = ormPixels;
    ormUpload.mipCount = 1;
    ormUpload.width = 1;
    ormUpload.height = 1;
    ormUpload.format = PixelFormat::RGBA32;
    m_defaultOrmTexture = UploadTexture(ormUpload);

    if (!m_defaultNormalTexture || !m_defaultOrmTexture)
    {
        Engine_LogError("GxmRenderer: could not create the default material textures");
        return false;
    }
    return true;
}

bool GxmRenderer::InitShadowTarget()
{
    const uint32_t size = GFX_SHADOW_MAP_SIZE;

    SceGxmRenderTargetParams targetParams;
    memset(&targetParams, 0, sizeof(targetParams));
    targetParams.flags = 0;
    targetParams.width = static_cast<uint16_t>(size);
    targetParams.height = static_cast<uint16_t>(size);
    targetParams.scenesPerFrame = 1;
    targetParams.multisampleMode = SCE_GXM_MULTISAMPLE_NONE;
    targetParams.multisampleLocations = 0;
    targetParams.driverMemBlock = -1;
    if (sceGxmCreateRenderTarget(&targetParams, &m_shadowRenderTarget) < 0)
    {
        Engine_LogError("GxmRenderer: sceGxmCreateRenderTarget failed for the shadow map");
        return false;
    }

    const uint32_t colorBytes = size * size * 4u;
    m_shadowColorData = GpuAlloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, colorBytes, SCE_GXM_COLOR_SURFACE_ALIGNMENT, SCE_GXM_MEMORY_ATTRIB_RW, &m_shadowColorUid);
    if (!m_shadowColorData)
    {
        Engine_LogError("GxmRenderer: no CDRAM for the shadow map");
        return false;
    }
    memset(m_shadowColorData, 0, colorBytes);

    // Depth written into an ordinary A8B8G8R8 colour surface's R channel by
    // scene_shadow_f.cg -- see the shader's own comment for why (no confirmed
    // readable-depth-texture path on this profile).
    if (sceGxmColorSurfaceInit(&m_shadowColorSurface, SCE_GXM_COLOR_FORMAT_A8B8G8R8, SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE, SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, size, size,
                               size, m_shadowColorData) < 0)
    {
        Engine_LogError("GxmRenderer: sceGxmColorSurfaceInit failed for the shadow map");
        return false;
    }

    const uint32_t alignedSize = AlignUp(size, SCE_GXM_TILE_SIZEX);
    const uint32_t depthBytes = alignedSize * alignedSize * 4u;
    m_shadowDepthData = GpuAlloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, depthBytes, SCE_GXM_DEPTHSTENCIL_SURFACE_ALIGNMENT, SCE_GXM_MEMORY_ATTRIB_RW, &m_shadowDepthUid);
    if (!m_shadowDepthData)
    {
        Engine_LogError("GxmRenderer: no memory for the shadow map's depth buffer");
        return false;
    }
    if (sceGxmDepthStencilSurfaceInit(&m_shadowDepthSurface, SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24, SCE_GXM_DEPTH_STENCIL_SURFACE_TILED, alignedSize, m_shadowDepthData, nullptr) < 0)
    {
        Engine_LogError("GxmRenderer: sceGxmDepthStencilSurfaceInit failed for the shadow map");
        return false;
    }

    for (int i = 0; i < GXM_MAX_RESIDENT_TEXTURES; ++i)
    {
        if (!m_textures[i].used)
        {
            m_shadowTextureSlot = i;
            break;
        }
    }
    if (m_shadowTextureSlot < 0)
    {
        Engine_LogError("GxmRenderer: texture registry full (%d), no slot for the shadow map", GXM_MAX_RESIDENT_TEXTURES);
        return false;
    }

    Texture& tex = m_textures[m_shadowTextureSlot];
    if (sceGxmTextureInitLinear(&tex.texture, m_shadowColorData, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8, size, size, 0) < 0)
    {
        Engine_LogError("GxmRenderer: sceGxmTextureInitLinear failed for the shadow map");
        return false;
    }
    // Point, not linear: the main pass does its own manual 3x3 PCF (see
    // scene_pbr_f.cg), and filtering here on top of that would double up.
    sceGxmTextureSetMinFilter(&tex.texture, SCE_GXM_TEXTURE_FILTER_POINT);
    sceGxmTextureSetMagFilter(&tex.texture, SCE_GXM_TEXTURE_FILTER_POINT);
    sceGxmTextureSetUAddrMode(&tex.texture, SCE_GXM_TEXTURE_ADDR_CLAMP);
    sceGxmTextureSetVAddrMode(&tex.texture, SCE_GXM_TEXTURE_ADDR_CLAMP);
    // data/uid are NOT set to m_shadowColorData/m_shadowColorUid: that memory
    // is owned and released by DestroyGraphics directly (mirroring
    // EnsureImageTarget's own reasoning), not by ReleaseTexture. uid stays -1,
    // GpuFree's own no-op sentinel.
    tex.data = m_shadowColorData;
    tex.uid = -1;
    tex.used = true;

    return true;
}

bool GxmRenderer::InitPbrShaders()
{
    const SceGxmProgram* vertexGxp = reinterpret_cast<const SceGxmProgram*>(g_ScenePbrVertexGxp);
    const SceGxmProgram* fragmentGxp = reinterpret_cast<const SceGxmProgram*>(g_ScenePbrFragmentGxp);

    if (sceGxmProgramCheck(vertexGxp) < 0 || sceGxmProgramCheck(fragmentGxp) < 0)
    {
        Engine_LogError("GxmRenderer: a compiled PBR shader failed validation");
        return false;
    }

    if (sceGxmShaderPatcherRegisterProgram(m_shaderPatcher, vertexGxp, &m_pbrVertexProgramId) < 0 || sceGxmShaderPatcherRegisterProgram(m_shaderPatcher, fragmentGxp, &m_pbrFragmentProgramId) < 0)
    {
        Engine_LogError("GxmRenderer: sceGxmShaderPatcherRegisterProgram failed for the PBR shaders");
        return false;
    }

    const SceGxmProgramParameter* pPosition = sceGxmProgramFindParameterByName(vertexGxp, "aPosition");
    const SceGxmProgramParameter* pNormal = sceGxmProgramFindParameterByName(vertexGxp, "aNormal");
    const SceGxmProgramParameter* pTexcoord = sceGxmProgramFindParameterByName(vertexGxp, "aTexcoord");
    const SceGxmProgramParameter* pColor = sceGxmProgramFindParameterByName(vertexGxp, "aColor");
    m_pbrViewProjParam = sceGxmProgramFindParameterByName(vertexGxp, "uViewProj");

    if (!pPosition || !pNormal || !pTexcoord || !pColor || !m_pbrViewProjParam)
    {
        Engine_LogError("GxmRenderer: the PBR vertex shader is missing an expected parameter");
        return false;
    }

    m_pbrCameraPosParam = sceGxmProgramFindParameterByName(fragmentGxp, "uCameraPos");
    m_pbrAmbientParam = sceGxmProgramFindParameterByName(fragmentGxp, "uAmbient");
    m_pbrLightPosOrDirParam = sceGxmProgramFindParameterByName(fragmentGxp, "uLightPosOrDir");
    m_pbrLightColorIntensityParam = sceGxmProgramFindParameterByName(fragmentGxp, "uLightColorIntensity");
    m_pbrLightRangeParam = sceGxmProgramFindParameterByName(fragmentGxp, "uLightRange");
    m_pbrShadowCasterParam = sceGxmProgramFindParameterByName(fragmentGxp, "uShadowCaster");
    m_pbrLightViewProjParam = sceGxmProgramFindParameterByName(fragmentGxp, "uLightViewProj");
    m_pbrBaseColorParam = sceGxmProgramFindParameterByName(fragmentGxp, "uBaseColor");
    m_pbrEmissiveParam = sceGxmProgramFindParameterByName(fragmentGxp, "uEmissive");
    m_pbrMrnaParam = sceGxmProgramFindParameterByName(fragmentGxp, "uMrna");
    m_pbrAlphaMaskParam = sceGxmProgramFindParameterByName(fragmentGxp, "uAlphaMask");

    if (!m_pbrCameraPosParam || !m_pbrAmbientParam || !m_pbrLightPosOrDirParam || !m_pbrLightColorIntensityParam || !m_pbrLightRangeParam || !m_pbrShadowCasterParam || !m_pbrLightViewProjParam ||
        !m_pbrBaseColorParam || !m_pbrEmissiveParam || !m_pbrMrnaParam || !m_pbrAlphaMaskParam)
    {
        Engine_LogError("GxmRenderer: the PBR fragment shader is missing an expected parameter");
        return false;
    }

    SceGxmVertexAttribute attributes[4];
    memset(attributes, 0, sizeof(attributes));

    attributes[0].streamIndex = 0;
    attributes[0].offset = offsetof(StagedGeometry::Vertex, x);
    attributes[0].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
    attributes[0].componentCount = 3;
    attributes[0].regIndex = sceGxmProgramParameterGetResourceIndex(pPosition);

    attributes[1].streamIndex = 0;
    attributes[1].offset = offsetof(StagedGeometry::Vertex, nx);
    attributes[1].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
    attributes[1].componentCount = 3;
    attributes[1].regIndex = sceGxmProgramParameterGetResourceIndex(pNormal);

    attributes[2].streamIndex = 0;
    attributes[2].offset = offsetof(StagedGeometry::Vertex, u);
    attributes[2].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
    attributes[2].componentCount = 2;
    attributes[2].regIndex = sceGxmProgramParameterGetResourceIndex(pTexcoord);

    attributes[3].streamIndex = 0;
    attributes[3].offset = offsetof(StagedGeometry::Vertex, r);
    attributes[3].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
    attributes[3].componentCount = 4;
    attributes[3].regIndex = sceGxmProgramParameterGetResourceIndex(pColor);

    SceGxmVertexStream stream;
    memset(&stream, 0, sizeof(stream));
    stream.stride = sizeof(StagedGeometry::Vertex);
    stream.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_32BIT;

    if (sceGxmShaderPatcherCreateVertexProgram(m_shaderPatcher, m_pbrVertexProgramId, attributes, 4, &stream, 1, &m_pbrVertexProgram) < 0)
    {
        Engine_LogError("GxmRenderer: sceGxmShaderPatcherCreateVertexProgram failed for the PBR pipeline");
        return false;
    }

    // Opaque only (no AlphaBlend material support -- matches every other
    // backend's own scope cut): straight overwrite, no blend function needed.
    SceGxmBlendInfo blendInfo;
    memset(&blendInfo, 0, sizeof(blendInfo));
    blendInfo.colorMask = SCE_GXM_COLOR_MASK_ALL;
    blendInfo.colorFunc = SCE_GXM_BLEND_FUNC_ADD;
    blendInfo.alphaFunc = SCE_GXM_BLEND_FUNC_ADD;
    blendInfo.colorSrc = SCE_GXM_BLEND_FACTOR_ONE;
    blendInfo.colorDst = SCE_GXM_BLEND_FACTOR_ZERO;
    blendInfo.alphaSrc = SCE_GXM_BLEND_FACTOR_ONE;
    blendInfo.alphaDst = SCE_GXM_BLEND_FACTOR_ZERO;

    if (sceGxmShaderPatcherCreateFragmentProgram(m_shaderPatcher, m_pbrFragmentProgramId, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE, &blendInfo, vertexGxp,
                                                  &m_pbrFragmentProgram) < 0)
    {
        Engine_LogError("GxmRenderer: sceGxmShaderPatcherCreateFragmentProgram failed for the PBR pipeline");
        return false;
    }
    return true;
}

bool GxmRenderer::InitShadowShaders()
{
    const SceGxmProgram* vertexGxp = reinterpret_cast<const SceGxmProgram*>(g_SceneShadowVertexGxp);
    const SceGxmProgram* fragmentGxp = reinterpret_cast<const SceGxmProgram*>(g_SceneShadowFragmentGxp);

    if (sceGxmProgramCheck(vertexGxp) < 0 || sceGxmProgramCheck(fragmentGxp) < 0)
    {
        Engine_LogError("GxmRenderer: a compiled shadow shader failed validation");
        return false;
    }

    if (sceGxmShaderPatcherRegisterProgram(m_shaderPatcher, vertexGxp, &m_shadowVertexProgramId) < 0 ||
        sceGxmShaderPatcherRegisterProgram(m_shaderPatcher, fragmentGxp, &m_shadowFragmentProgramId) < 0)
    {
        Engine_LogError("GxmRenderer: sceGxmShaderPatcherRegisterProgram failed for the shadow shaders");
        return false;
    }

    const SceGxmProgramParameter* pPosition = sceGxmProgramFindParameterByName(vertexGxp, "aPosition");
    m_shadowLightViewProjParam = sceGxmProgramFindParameterByName(vertexGxp, "uLightViewProj");
    if (!pPosition || !m_shadowLightViewProjParam)
    {
        Engine_LogError("GxmRenderer: the shadow vertex shader is missing an expected parameter");
        return false;
    }

    SceGxmVertexAttribute attribute;
    memset(&attribute, 0, sizeof(attribute));
    attribute.streamIndex = 0;
    attribute.offset = offsetof(StagedGeometry::Vertex, x);
    attribute.format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
    attribute.componentCount = 3;
    attribute.regIndex = sceGxmProgramParameterGetResourceIndex(pPosition);

    SceGxmVertexStream stream;
    memset(&stream, 0, sizeof(stream));
    stream.stride = sizeof(StagedGeometry::Vertex);
    stream.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_32BIT;

    if (sceGxmShaderPatcherCreateVertexProgram(m_shaderPatcher, m_shadowVertexProgramId, &attribute, 1, &stream, 1, &m_shadowVertexProgram) < 0)
    {
        Engine_LogError("GxmRenderer: sceGxmShaderPatcherCreateVertexProgram failed for the shadow pipeline");
        return false;
    }

    if (sceGxmShaderPatcherCreateFragmentProgram(m_shaderPatcher, m_shadowFragmentProgramId, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE, nullptr, vertexGxp,
                                                  &m_shadowFragmentProgram) < 0)
    {
        Engine_LogError("GxmRenderer: sceGxmShaderPatcherCreateFragmentProgram failed for the shadow pipeline");
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

void GxmRenderer::RenderShadowMap(const DrawLists& lists)
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

    // m_frame3DVertices is this frame's already-uploaded (and possibly
    // overflow-clamped) 3D vertex count -- DynamicVertexStart indexes the
    // same m_vertexBuffer this frame's UploadVertices just filled.
    const uint32_t dynStart = m_geometry.DynamicVertexStart();
    if (dynStart >= m_frame3DVertices)
        return; // nothing dynamic uploaded this frame; leave the map unsampled (see scene_pbr_f.cg)
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

    if (sceGxmBeginScene(m_context, 0, m_shadowRenderTarget, nullptr, nullptr, nullptr, &m_shadowColorSurface, &m_shadowDepthSurface) < 0)
    {
        Engine_LogError("GxmRenderer: sceGxmBeginScene failed for the shadow map");
        return;
    }

    // sceGxm has no direct "clear colour" call -- a beginning scene's colour
    // surface must be flood-filled by an actual draw, exactly like
    // DrawClearQuad does for the main scene (see its own comment) and
    // RenderToImage3D does for the offscreen preview target. Unlike those two,
    // this flood-fills white (1.0, encoded into every channel -- see
    // scene_shadow_f.cg) rather than a caller-chosen background colour: an
    // uncovered shadow-map texel must read back as "nothing occludes here",
    // not black (which SampleShadow would read as the nearest possible depth,
    // falsely shadowing anything that samples it).
    {
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

        // Reuses the same clear-quad vertex slot DrawClearQuad/RenderToImage3D
        // keep just past the frame budget: this shadow scene begins, draws and
        // ends entirely before the main scene's own BeginScene (see below),
        // so nothing else is using it yet.
        static const float kCorners[kClearQuadVertices][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
        StagedGeometry::Vertex* clearQuad = static_cast<StagedGeometry::Vertex*>(m_vertexBuffer) + GFX_GXM_MAX_FRAME_VERTICES;
        for (uint32_t i = 0; i < kClearQuadVertices; ++i)
        {
            memset(&clearQuad[i], 0, sizeof(StagedGeometry::Vertex));
            clearQuad[i].x = kCorners[i][0];
            clearQuad[i].y = kCorners[i][1];
            clearQuad[i].r = 1.0f;
            clearQuad[i].g = 1.0f;
            clearQuad[i].b = 1.0f;
            clearQuad[i].a = 1.0f;
        }
        const uint32_t* clearIndices = static_cast<const uint32_t*>(m_indexBuffer);
        sceGxmDraw(m_context, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U32, clearIndices + GFX_GXM_MAX_FRAME_VERTICES, kClearQuadVertices);
    }

    sceGxmSetFrontDepthFunc(m_context, SCE_GXM_DEPTH_FUNC_LESS_EQUAL);
    sceGxmSetFrontDepthWriteEnable(m_context, SCE_GXM_DEPTH_WRITE_ENABLED);
    sceGxmSetVertexProgram(m_context, m_shadowVertexProgram);
    sceGxmSetFragmentProgram(m_context, m_shadowFragmentProgram);
    sceGxmSetVertexStream(m_context, 0, m_vertexBuffer);

    void* uniforms = nullptr;
    sceGxmReserveVertexDefaultUniformBuffer(m_context, &uniforms);
    sceGxmSetUniformDataF(uniforms, m_shadowLightViewProjParam, 0, 16, m_lastLightViewProj);

    // One draw over every dynamic vertex: no per-material texture binding to
    // change between runs (no alpha-mask cutout support yet -- see
    // scene_shadow_f.cg), so there is nothing run boundaries buy it.
    const uint32_t* indices = static_cast<const uint32_t*>(m_indexBuffer);
    sceGxmDraw(m_context, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U32, indices + dynStart, dynCount);

    sceGxmEndScene(m_context, nullptr, nullptr);
    // sceGxmEndScene only queues the work; the main pass samples this target
    // as a texture this same frame, so it must actually be finished, not
    // merely submitted, before that happens -- same reasoning as
    // RenderToImage3D's own sceGxmFinish call.
    sceGxmFinish(m_context);

    m_shadowActive = true;
    m_shadowCasterIndex = casterId;
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

    sceGxmSetVertexStream(m_context, 0, m_vertexBuffer);

    float matrix[16];

    if (m_frame3DVertices > 0)
    {
        sceGxmSetFrontDepthFunc(m_context, SCE_GXM_DEPTH_FUNC_LESS_EQUAL);
        sceGxmSetFrontDepthWriteEnable(m_context, SCE_GXM_DEPTH_WRITE_ENABLED);
        sceGxmSetVertexProgram(m_context, m_pbrVertexProgram);
        sceGxmSetFragmentProgram(m_context, m_pbrFragmentProgram);

        StagedGeometry::BuildViewProjection(m_drawLists.GetCamera3D(), m_width, m_height, true, matrix);

        void* vertUniforms = nullptr;
        sceGxmReserveVertexDefaultUniformBuffer(m_context, &vertUniforms);
        sceGxmSetUniformDataF(vertUniforms, m_pbrViewProjParam, 0, 16, matrix);

        // Frame-constant fragment uniforms (camera, ambient, lights, shadow
        // caster), set once here rather than per run.
        void* fragUniforms = nullptr;
        sceGxmReserveFragmentDefaultUniformBuffer(m_context, &fragUniforms);

        const Camera3D& camera = m_drawLists.GetCamera3D();
        const float cameraPos[3] = {camera.position.x, camera.position.y, camera.position.z};
        sceGxmSetUniformDataF(fragUniforms, m_pbrCameraPosParam, 0, 3, cameraPos);

        const Color3& ambient = m_drawLists.GetAmbientLight();
        const float ambientArr[3] = {ambient.r, ambient.g, ambient.b};
        sceGxmSetUniformDataF(fragUniforms, m_pbrAmbientParam, 0, 3, ambientArr);

        float lightPosOrDir[GFX_MAX_LIGHTS * 4];
        float lightColorIntensity[GFX_MAX_LIGHTS * 4];
        float lightRange[GFX_MAX_LIGHTS * 4];
        const Light3D* lights = m_drawLists.GetLights();
        for (uint32_t i = 0; i < GFX_MAX_LIGHTS; ++i)
        {
            const Light3D& l = lights[i];
            const bool directional = (l.type == LightType::Directional);
            lightPosOrDir[i * 4 + 0] = directional ? l.direction.x : l.position.x;
            lightPosOrDir[i * 4 + 1] = directional ? l.direction.y : l.position.y;
            lightPosOrDir[i * 4 + 2] = directional ? l.direction.z : l.position.z;
            lightPosOrDir[i * 4 + 3] = directional ? 0.0f : 1.0f;
            lightColorIntensity[i * 4 + 0] = l.color.r;
            lightColorIntensity[i * 4 + 1] = l.color.g;
            lightColorIntensity[i * 4 + 2] = l.color.b;
            lightColorIntensity[i * 4 + 3] = l.intensity; // <= 0 means "off"; the shader skips it
            lightRange[i * 4 + 0] = l.range;
            lightRange[i * 4 + 1] = 0.0f;
            lightRange[i * 4 + 2] = 0.0f;
            lightRange[i * 4 + 3] = 0.0f;
        }
        sceGxmSetUniformDataF(fragUniforms, m_pbrLightPosOrDirParam, 0, GFX_MAX_LIGHTS * 4, lightPosOrDir);
        sceGxmSetUniformDataF(fragUniforms, m_pbrLightColorIntensityParam, 0, GFX_MAX_LIGHTS * 4, lightColorIntensity);
        sceGxmSetUniformDataF(fragUniforms, m_pbrLightRangeParam, 0, GFX_MAX_LIGHTS * 4, lightRange);

        const float shadowCaster = m_shadowActive ? static_cast<float>(m_shadowCasterIndex) : -1.0f;
        sceGxmSetUniformDataF(fragUniforms, m_pbrShadowCasterParam, 0, 1, &shadowCaster);
        sceGxmSetUniformDataF(fragUniforms, m_pbrLightViewProjParam, 0, 16, m_lastLightViewProj);

        if (m_shadowTextureSlot >= 0)
            sceGxmSetFragmentTexture(m_context, 3, &m_textures[m_shadowTextureSlot].texture);

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

            const StagedGeometry::RunMaterial& mat = runs[i].material;

            void* runFragUniforms = nullptr;
            sceGxmReserveFragmentDefaultUniformBuffer(m_context, &runFragUniforms);
            sceGxmSetUniformDataF(runFragUniforms, m_pbrBaseColorParam, 0, 4, mat.baseColor);
            sceGxmSetUniformDataF(runFragUniforms, m_pbrEmissiveParam, 0, 3, mat.emissive);
            const float mrna[4] = {mat.metallic, mat.roughness, mat.normalScale, mat.alphaCutoff};
            sceGxmSetUniformDataF(runFragUniforms, m_pbrMrnaParam, 0, 4, mrna);
            const float alphaMask = (mat.flags & MATERIAL_FLAG_ALPHA_MASK) ? 1.0f : 0.0f;
            sceGxmSetUniformDataF(runFragUniforms, m_pbrAlphaMaskParam, 0, 1, &alphaMask);

            const uint32_t albedoHandle = runs[i].texture ? runs[i].texture : m_whiteTexture;
            if (albedoHandle && albedoHandle <= GXM_MAX_RESIDENT_TEXTURES && m_textures[albedoHandle - 1u].used)
                sceGxmSetFragmentTexture(m_context, 0, &m_textures[albedoHandle - 1u].texture);

            const uint32_t normalHandle = mat.normalTexture ? mat.normalTexture : m_defaultNormalTexture;
            if (normalHandle && normalHandle <= GXM_MAX_RESIDENT_TEXTURES && m_textures[normalHandle - 1u].used)
                sceGxmSetFragmentTexture(m_context, 1, &m_textures[normalHandle - 1u].texture);

            const uint32_t ormHandle = mat.ormTexture ? mat.ormTexture : m_defaultOrmTexture;
            if (ormHandle && ormHandle <= GXM_MAX_RESIDENT_TEXTURES && m_textures[ormHandle - 1u].used)
                sceGxmSetFragmentTexture(m_context, 2, &m_textures[ormHandle - 1u].texture);

            sceGxmDraw(m_context, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U32, indices + first, count);
        }
    }

    if (m_frame2DVertices > 0)
    {
        sceGxmSetFrontDepthFunc(m_context, SCE_GXM_DEPTH_FUNC_ALWAYS);
        sceGxmSetFrontDepthWriteEnable(m_context, SCE_GXM_DEPTH_WRITE_DISABLED);
        sceGxmSetVertexProgram(m_context, m_vertexProgram);
        sceGxmSetFragmentProgram(m_context, m_fragmentProgram);

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

    // A complete scene of its own (sceGxm forbids a second open scene), so
    // this must run and finish strictly before the main frame's
    // sceGxmBeginScene below -- never nested inside it.
    RenderShadowMap(m_drawLists);

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
void GxmRenderer::SetLight3D(LightID id, const Light3D& light) { m_drawLists.SetLight3D(id, light); }
void GxmRenderer::SetAmbientLight(const Color3& color) { m_drawLists.SetAmbientLight(color); }
void GxmRenderer::SetShadowCasterLight(LightID id) { m_drawLists.SetShadowCasterLight(id); }

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

        if (m_pbrFragmentProgram)
            sceGxmShaderPatcherReleaseFragmentProgram(m_shaderPatcher, m_pbrFragmentProgram);
        if (m_pbrVertexProgram)
            sceGxmShaderPatcherReleaseVertexProgram(m_shaderPatcher, m_pbrVertexProgram);
        sceGxmShaderPatcherUnregisterProgram(m_shaderPatcher, m_pbrFragmentProgramId);
        sceGxmShaderPatcherUnregisterProgram(m_shaderPatcher, m_pbrVertexProgramId);

        if (m_shadowFragmentProgram)
            sceGxmShaderPatcherReleaseFragmentProgram(m_shaderPatcher, m_shadowFragmentProgram);
        if (m_shadowVertexProgram)
            sceGxmShaderPatcherReleaseVertexProgram(m_shaderPatcher, m_shadowVertexProgram);
        sceGxmShaderPatcherUnregisterProgram(m_shaderPatcher, m_shadowFragmentProgramId);
        sceGxmShaderPatcherUnregisterProgram(m_shaderPatcher, m_shadowVertexProgramId);

        sceGxmShaderPatcherDestroy(m_shaderPatcher);
        m_shaderPatcher = nullptr;
    }
    m_vertexProgram = nullptr;
    m_fragmentProgram = nullptr;
    m_pbrVertexProgram = nullptr;
    m_pbrFragmentProgram = nullptr;
    m_shadowVertexProgram = nullptr;
    m_shadowFragmentProgram = nullptr;

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

    // Same reasoning as the image target above: the shadow map's texture
    // registry entry is already handled by the cleanup loop (uid -1, see
    // InitShadowTarget); this frees the backing memory and render target
    // that slot pointed at.
    GpuFree(m_shadowDepthUid);
    m_shadowDepthUid = -1;
    m_shadowDepthData = nullptr;
    GpuFree(m_shadowColorUid);
    m_shadowColorUid = -1;
    m_shadowColorData = nullptr;
    if (m_shadowRenderTarget)
    {
        sceGxmDestroyRenderTarget(m_shadowRenderTarget);
        m_shadowRenderTarget = nullptr;
    }

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
