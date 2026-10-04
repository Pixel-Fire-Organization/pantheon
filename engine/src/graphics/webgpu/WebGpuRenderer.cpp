#include "graphics/webgpu/WebGpuRenderer.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Macros.h"
#include "core/EngineDebug.h"
#include "core/EngineMemory.h"
#include "graphics/ShaderAssets.h"
#include "graphics/TextureExpand.h"
#include "platform/Platform.h"

namespace
{

    // wgpu takes explicit (pointer, length) strings rather than null-terminated
    // ones, so every literal has to be wrapped.
    WGPUStringView Str(const char* s) { return WGPUStringView{s, s ? strlen(s) : 0}; }

    // WGSL has no macro/include facility, so the two platform constants baked
    // into pbr.wgsl as text placeholders (the light array's fixed length, and
    // the shadow map's texel size) are spliced in here at pipeline-creation
    // time from the real PlatformConstants*.h values, rather than duplicated
    // as literals that could silently drift from them. Both replacements are
    // shorter than their tokens, so the result never outgrows the source; an
    // edited shader that somehow did is refused rather than truncated.
    // Returns a malloc'd, nul-terminated buffer; the caller frees it once the
    // shader module is created, since wgpu copies the source internally.
    char* BuildShaderSource3D(const char* source, size_t srcLen)
    {
        char lightsStr[16];
        snprintf(lightsStr, sizeof(lightsStr), "%d", GFX_MAX_LIGHTS);
        char shadowSizeStr[32];
        snprintf(shadowSizeStr, sizeof(shadowSizeStr), "%d.0", GFX_SHADOW_MAP_SIZE);

        const char* const kLightsToken = "GFX_MAX_LIGHTS_PLACEHOLDER";
        const char* const kShadowToken = "GFX_SHADOW_MAP_SIZE_PLACEHOLDER";
        const size_t lightsTokenLen = strlen(kLightsToken);
        const size_t shadowTokenLen = strlen(kShadowToken);

        const size_t cap = srcLen + 256;
        char* out = static_cast<char*>(malloc(cap));
        if (!out)
            return nullptr;

        size_t w = 0;
        for (size_t r = 0; r < srcLen;)
        {
            const char* replacement = nullptr;
            size_t tokenLen = 0;
            if (strncmp(source + r, kLightsToken, lightsTokenLen) == 0)
            {
                replacement = lightsStr;
                tokenLen = lightsTokenLen;
            }
            else if (strncmp(source + r, kShadowToken, shadowTokenLen) == 0)
            {
                replacement = shadowSizeStr;
                tokenLen = shadowTokenLen;
            }

            const size_t n = replacement ? strlen(replacement) : 1u;
            if (w + n + 1u > cap)
            {
                free(out);
                return nullptr;
            }
            if (replacement)
            {
                memcpy(out + w, replacement, n);
                r += tokenLen;
            }
            else
            {
                out[w] = source[r++];
            }
            w += n;
        }
        out[w] = '\0';
        return out;
    }

    struct AdapterRequest
    {
        WGPUAdapter adapter;
        bool done;
    };

    struct DeviceRequest
    {
        WGPUDevice device;
        bool done;
    };

    void OnAdapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* user1, void* user2)
    {
        UNUSED_VAR(user2);
        AdapterRequest* req = static_cast<AdapterRequest*>(user1);
        if (status == WGPURequestAdapterStatus_Success)
            req->adapter = adapter;
        else
            Engine_LogError("WebGpu: no adapter (%.*s)", static_cast<int>(message.length), message.data ? message.data : "");
        req->done = true;
    }

    void OnDevice(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* user1, void* user2)
    {
        UNUSED_VAR(user2);
        DeviceRequest* req = static_cast<DeviceRequest*>(user1);
        if (status == WGPURequestDeviceStatus_Success)
            req->device = device;
        else
            Engine_LogError("WebGpu: no device (%.*s)", static_cast<int>(message.length), message.data ? message.data : "");
        req->done = true;
    }

    // Validation errors are programmer errors and must be loud, per the engine's
    // crash-loudly-on-programming-error rule.
    void OnUncapturedError(WGPUDevice const* device, WGPUErrorType type, WGPUStringView message, void* user1, void* user2)
    {
        UNUSED_VAR(device);
        UNUSED_VAR(user1);
        UNUSED_VAR(user2);
        Engine_LogError("WebGpu: uncaptured error (type %d): %.*s", static_cast<int>(type), static_cast<int>(message.length), message.data ? message.data : "");
    }

} // namespace

WebGpuRenderer::WebGpuRenderer(const EngineConfig& config) :
    m_instance(nullptr), m_adapter(nullptr), m_device(nullptr), m_queue(nullptr), m_surface(nullptr), m_surfaceFormat(WGPUTextureFormat_BGRA8Unorm), m_pipeline2D(nullptr), m_uniformLayout(nullptr),
    m_textureLayout(nullptr), m_bindGroup2D(nullptr), m_uniformBuffer2D(nullptr), m_sampler(nullptr), m_samplerNearest(nullptr), m_pipeline3D(nullptr), m_frameLayout3D(nullptr),
    m_materialLayout3D(nullptr), m_materialTexLayout3D(nullptr), m_frameBindGroup3D(nullptr), m_materialBindGroup3D(nullptr), m_frameUniformBuffer3D(nullptr), m_materialUniformBuffer3D(nullptr),
    m_materialGroupCount(0), m_shadowPipeline(nullptr), m_shadowPassLayout(nullptr), m_shadowPassBindGroup(nullptr), m_shadowPassUniformBuffer(nullptr), m_shadowMapTexture(nullptr),
    m_shadowMapView(nullptr), m_shadowSamplerCompare(nullptr), m_shadowActive(false), m_shadowCasterIndex(-1), m_vertexBuffer(nullptr), m_vertexBufferCapacity(0), m_depthTexture(nullptr),
    m_depthView(nullptr), m_whiteTexture{}, m_defaultNormalTexture(nullptr), m_defaultNormalView(nullptr), m_defaultOrmTexture(nullptr), m_defaultOrmView(nullptr), m_clearColor{0.0f, 0.0f, 0.0f},
    m_width(0), m_height(0), m_frameStats{}, m_initialized(false), m_imagePipeline3D(nullptr), m_imageUniformBuffer(nullptr), m_imageUniformBindGroup(nullptr), m_imageVertexBuffer(nullptr),
    m_imageVertexBufferCapacity(0), m_imageColorTexture(nullptr), m_imageColorView(nullptr), m_imageDepthTexture(nullptr), m_imageDepthView(nullptr), m_imageWidth(0), m_imageHeight(0),
    m_imageTextureSlot(-1)
{
    UNUSED_VAR(config);
    memset(m_textures, 0, sizeof(m_textures));
    memset(m_materialGroups, 0, sizeof(m_materialGroups));
    memset(m_lastLightViewProj, 0, sizeof(m_lastLightViewProj));
}

void WebGpuRenderer::Initialize()
{
    Platform* platform = Engine_GetPlatform();
    platform->GetFramebufferSize(&m_width, &m_height);
    Engine_LogInfo("WebGpuRenderer: initializing (%ux%u)", m_width, m_height);

    // The primitive geometry tables live in the renderer arena, same as the PS2
    // backends: the draw lists own the de-interleaved arrays and every path reads
    // them, so nothing about that is platform-specific.
    float* arena = static_cast<float*>(Engine_GetSlot(ARENA_RENDERER, 0));
    if (!arena)
    {
        Engine_LogError("WebGpuRenderer: failed to retrieve ARENA_RENDERER slot 0");
        return;
    }
    m_drawLists.Init(arena);

    if (!InitDevice())
        return;
    if (!CreatePipelines())
        return;
    if (!CreateWhiteTexture())
        return;
    if (!CreateDefaultMaterialTextures())
        return;
    // The shadow map (and its comparison sampler) must exist before
    // CreatePbrPipeline, which binds them into the main pass's group 0.
    if (!EnsureShadowMap())
        return;
    if (!CreatePbrPipeline())
        return;
    if (!CreateShadowPipeline())
        return;
    if (!ConfigureSurface(m_width, m_height))
        return;

    m_initialized = true;
    Engine_LogInfo("WebGpuRenderer: ready");
}

bool WebGpuRenderer::InitDevice()
{
    m_instance = wgpuCreateInstance(nullptr);
    if (!m_instance)
    {
        Engine_LogError("WebGpu: wgpuCreateInstance failed");
        return false;
    }

    // Surface first: the adapter is chosen for compatibility with it, so a
    // machine with several GPUs picks the one that can actually present here.
    m_surface = CreateSurface(m_instance);
    if (!m_surface)
    {
        Engine_LogError("WebGpu: could not create a surface for the window");
        return false;
    }

    AdapterRequest adapterReq;
    adapterReq.adapter = nullptr;
    adapterReq.done = false;

    WGPURequestAdapterOptions options;
    memset(&options, 0, sizeof(options));
    options.compatibleSurface = m_surface;
    options.powerPreference = WGPUPowerPreference_HighPerformance;

    WGPURequestAdapterCallbackInfo adapterCb;
    memset(&adapterCb, 0, sizeof(adapterCb));
    adapterCb.mode = WGPUCallbackMode_AllowProcessEvents;
    adapterCb.callback = &OnAdapter;
    adapterCb.userdata1 = &adapterReq;

    wgpuInstanceRequestAdapter(m_instance, &options, adapterCb);
    while (!adapterReq.done)
        wgpuInstanceProcessEvents(m_instance);

    if (!adapterReq.adapter)
        return false; // OnAdapter already said why
    m_adapter = adapterReq.adapter;

    DeviceRequest deviceReq;
    deviceReq.device = nullptr;
    deviceReq.done = false;

    WGPUDeviceDescriptor deviceDesc;
    memset(&deviceDesc, 0, sizeof(deviceDesc));
    deviceDesc.label = Str("engine-device");
    deviceDesc.uncapturedErrorCallbackInfo.callback = &OnUncapturedError;

    WGPURequestDeviceCallbackInfo deviceCb;
    memset(&deviceCb, 0, sizeof(deviceCb));
    deviceCb.mode = WGPUCallbackMode_AllowProcessEvents;
    deviceCb.callback = &OnDevice;
    deviceCb.userdata1 = &deviceReq;

    wgpuAdapterRequestDevice(m_adapter, &deviceDesc, deviceCb);
    while (!deviceReq.done)
        wgpuInstanceProcessEvents(m_instance);

    if (!deviceReq.device)
        return false;
    m_device = deviceReq.device;
    m_queue = wgpuDeviceGetQueue(m_device);

    // Prefer a NON-sRGB surface format. The engine hands renderers raw 0-1
    // colour components and the PS2 writes them to the GS untouched; an sRGB
    // surface would apply an encode on top and make every desktop frame visibly
    // lighter than the console, which is the opposite of what a preview target
    // is for. Fall back to whatever the surface prefers if none is offered.
    WGPUSurfaceCapabilities caps;
    memset(&caps, 0, sizeof(caps));
    if (wgpuSurfaceGetCapabilities(m_surface, m_adapter, &caps) == WGPUStatus_Success)
    {
        if (caps.formatCount > 0)
        {
            m_surfaceFormat = caps.formats[0];
            for (size_t i = 0; i < caps.formatCount; ++i)
            {
                const WGPUTextureFormat f = caps.formats[i];
                if (f == WGPUTextureFormat_BGRA8Unorm || f == WGPUTextureFormat_RGBA8Unorm)
                {
                    m_surfaceFormat = f;
                    break;
                }
            }
        }
        wgpuSurfaceCapabilitiesFreeMembers(caps);
    }

    return true;
}

bool WebGpuRenderer::CreatePipelines()
{
    ShaderSource flatSource;
    if (!ShaderAssets_Load("flat.wgsl", &flatSource))
        return false;

    WGPUShaderSourceWGSL wgsl;
    memset(&wgsl, 0, sizeof(wgsl));
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = WGPUStringView{flatSource.text.get(), flatSource.size};

    WGPUShaderModuleDescriptor shaderDesc;
    memset(&shaderDesc, 0, sizeof(shaderDesc));
    shaderDesc.nextInChain = &wgsl.chain;
    shaderDesc.label = Str("engine-shader-flat");

    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(m_device, &shaderDesc);
    if (!shader)
    {
        Engine_LogError("WebGpu: flat shader module creation failed");
        return false;
    }

    // --- group 0: the view-projection uniform -------------------------------
    WGPUBindGroupLayoutEntry uniformEntry;
    memset(&uniformEntry, 0, sizeof(uniformEntry));
    uniformEntry.binding = 0;
    uniformEntry.visibility = WGPUShaderStage_Vertex;
    uniformEntry.buffer.type = WGPUBufferBindingType_Uniform;
    uniformEntry.buffer.minBindingSize = sizeof(Uniforms);

    WGPUBindGroupLayoutDescriptor uniformLayoutDesc;
    memset(&uniformLayoutDesc, 0, sizeof(uniformLayoutDesc));
    uniformLayoutDesc.entryCount = 1;
    uniformLayoutDesc.entries = &uniformEntry;
    m_uniformLayout = wgpuDeviceCreateBindGroupLayout(m_device, &uniformLayoutDesc);

    // --- group 1: texture + sampler -----------------------------------------
    WGPUBindGroupLayoutEntry texEntries[2];
    memset(texEntries, 0, sizeof(texEntries));
    texEntries[0].binding = 0;
    texEntries[0].visibility = WGPUShaderStage_Fragment;
    texEntries[0].texture.sampleType = WGPUTextureSampleType_Float;
    texEntries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
    texEntries[1].binding = 1;
    texEntries[1].visibility = WGPUShaderStage_Fragment;
    texEntries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor texLayoutDesc;
    memset(&texLayoutDesc, 0, sizeof(texLayoutDesc));
    texLayoutDesc.entryCount = 2;
    texLayoutDesc.entries = texEntries;
    m_textureLayout = wgpuDeviceCreateBindGroupLayout(m_device, &texLayoutDesc);

    WGPUSamplerDescriptor samplerDesc;
    memset(&samplerDesc, 0, sizeof(samplerDesc));
    samplerDesc.addressModeU = WGPUAddressMode_Repeat;
    samplerDesc.addressModeV = WGPUAddressMode_Repeat;
    samplerDesc.addressModeW = WGPUAddressMode_Repeat;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    samplerDesc.lodMaxClamp = 32.0f;
    samplerDesc.maxAnisotropy = 1;
    m_sampler = wgpuDeviceCreateSampler(m_device, &samplerDesc);

    samplerDesc.magFilter = WGPUFilterMode_Nearest;
    samplerDesc.minFilter = WGPUFilterMode_Nearest;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    m_samplerNearest = wgpuDeviceCreateSampler(m_device, &samplerDesc);

    WGPUBufferDescriptor uniformDesc;
    memset(&uniformDesc, 0, sizeof(uniformDesc));
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformDesc.size = sizeof(Uniforms);
    m_uniformBuffer2D = wgpuDeviceCreateBuffer(m_device, &uniformDesc);

    WGPUBindGroupEntry bindEntry;
    memset(&bindEntry, 0, sizeof(bindEntry));
    bindEntry.binding = 0;
    bindEntry.size = sizeof(Uniforms);

    WGPUBindGroupDescriptor bindDesc;
    memset(&bindDesc, 0, sizeof(bindDesc));
    bindDesc.layout = m_uniformLayout;
    bindDesc.entryCount = 1;
    bindDesc.entries = &bindEntry;

    bindEntry.buffer = m_uniformBuffer2D;
    m_bindGroup2D = wgpuDeviceCreateBindGroup(m_device, &bindDesc);

    WGPUBindGroupLayout layouts[2] = {m_uniformLayout, m_textureLayout};
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc;
    memset(&pipelineLayoutDesc, 0, sizeof(pipelineLayoutDesc));
    pipelineLayoutDesc.bindGroupLayoutCount = 2;
    pipelineLayoutDesc.bindGroupLayouts = layouts;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(m_device, &pipelineLayoutDesc);

    // --- vertex layout ------------------------------------------------------
    WGPUVertexAttribute attributes[4];
    memset(attributes, 0, sizeof(attributes));
    attributes[0].format = WGPUVertexFormat_Float32x3; // position
    attributes[0].offset = 0;
    attributes[0].shaderLocation = 0;
    attributes[1].format = WGPUVertexFormat_Float32x3; // normal
    attributes[1].offset = sizeof(float) * 3;
    attributes[1].shaderLocation = 1;
    attributes[2].format = WGPUVertexFormat_Float32x2; // uv
    attributes[2].offset = sizeof(float) * 6;
    attributes[2].shaderLocation = 2;
    attributes[3].format = WGPUVertexFormat_Float32x4; // colour
    attributes[3].offset = sizeof(float) * 8;
    attributes[3].shaderLocation = 3;

    WGPUVertexBufferLayout vertexLayout;
    memset(&vertexLayout, 0, sizeof(vertexLayout));
    vertexLayout.arrayStride = sizeof(StagedGeometry::Vertex);
    vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexLayout.attributeCount = 4;
    vertexLayout.attributes = attributes;

    WGPUColorTargetState colorTarget;
    memset(&colorTarget, 0, sizeof(colorTarget));
    colorTarget.format = m_surfaceFormat;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment;
    memset(&fragment, 0, sizeof(fragment));
    fragment.module = shader;
    fragment.entryPoint = Str("fs_main");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPUDepthStencilState depthState;
    memset(&depthState, 0, sizeof(depthState));
    depthState.format = WGPUTextureFormat_Depth24Plus;
    depthState.depthWriteEnabled = WGPUOptionalBool_True;
    depthState.depthCompare = WGPUCompareFunction_Less;
    depthState.stencilFront.compare = WGPUCompareFunction_Always;
    depthState.stencilBack.compare = WGPUCompareFunction_Always;

    WGPURenderPipelineDescriptor pipelineDesc;
    memset(&pipelineDesc, 0, sizeof(pipelineDesc));
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shader;
    pipelineDesc.vertex.entryPoint = Str("vs_main");
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexLayout;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    // No back-face culling: the baked level and model geometry does not carry a
    // guaranteed winding, and dropping triangles is worse than drawing extra.
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = 0xFFFFFFFFu;

    // 2D differs from the image-preview pipeline below in depth and in
    // blending: HUD quads are submitted in draw order and must not be
    // discarded by Z, and the interface carries per-quad alpha.
    WGPUDepthStencilState depth2D = depthState;
    depth2D.depthWriteEnabled = WGPUOptionalBool_False;
    depth2D.depthCompare = WGPUCompareFunction_Always;

    WGPUBlendState blend2D;
    memset(&blend2D, 0, sizeof(blend2D));
    blend2D.color.operation = WGPUBlendOperation_Add;
    blend2D.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend2D.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend2D.alpha.operation = WGPUBlendOperation_Add;
    blend2D.alpha.srcFactor = WGPUBlendFactor_One;
    blend2D.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;

    WGPUColorTargetState colorTarget2D = colorTarget;
    colorTarget2D.blend = &blend2D;

    WGPUFragmentState fragment2D = fragment;
    fragment2D.targets = &colorTarget2D;

    pipelineDesc.label = Str("engine-2d");
    pipelineDesc.depthStencil = &depth2D;
    pipelineDesc.fragment = &fragment2D;
    m_pipeline2D = wgpuDeviceCreateRenderPipeline(m_device, &pipelineDesc);

    if (!m_pipeline2D)
    {
        Engine_LogError("WebGpu: 2D pipeline creation failed");
        return false;
    }

    // RenderToImage3D's scratch target is always RGBA8Unorm regardless of the
    // swapchain's own surface format, so its pipeline is its own dedicated
    // object rather than a conditional alias of another one.
    WGPUColorTargetState imageColorTarget;
    memset(&imageColorTarget, 0, sizeof(imageColorTarget));
    imageColorTarget.format = WGPUTextureFormat_RGBA8Unorm;
    imageColorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState imageFragment = fragment;
    imageFragment.targets = &imageColorTarget;

    pipelineDesc.label = Str("engine-3d-image");
    pipelineDesc.depthStencil = &depthState;
    pipelineDesc.fragment = &imageFragment;
    m_imagePipeline3D = wgpuDeviceCreateRenderPipeline(m_device, &pipelineDesc);
    if (!m_imagePipeline3D)
    {
        Engine_LogError("WebGpu: image-target render pipeline creation failed");
        return false;
    }

    // Dedicated uniform buffer + bind group for RenderToImage3D -- see the
    // member comment in WebGpuRenderer.h for why this does not reuse m_uniformBuffer2D.
    m_imageUniformBuffer = wgpuDeviceCreateBuffer(m_device, &uniformDesc);
    bindEntry.buffer = m_imageUniformBuffer;
    m_imageUniformBindGroup = wgpuDeviceCreateBindGroup(m_device, &bindDesc);
    if (!m_imageUniformBuffer || !m_imageUniformBindGroup)
    {
        Engine_LogError("WebGpu: image-target uniform resources creation failed");
        return false;
    }

    return true;
}

bool WebGpuRenderer::CreateWhiteTexture()
{
    WGPUTextureDescriptor desc;
    memset(&desc, 0, sizeof(desc));
    desc.label = Str("engine-white");
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    desc.dimension = WGPUTextureDimension_2D;
    desc.size.width = 1;
    desc.size.height = 1;
    desc.size.depthOrArrayLayers = 1;
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;

    m_whiteTexture.texture = wgpuDeviceCreateTexture(m_device, &desc);
    if (!m_whiteTexture.texture)
        return false;

    const uint32_t white = 0xFFFFFFFFu;
    WGPUTexelCopyTextureInfo dst;
    memset(&dst, 0, sizeof(dst));
    dst.texture = m_whiteTexture.texture;
    dst.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout layout;
    memset(&layout, 0, sizeof(layout));
    layout.bytesPerRow = 4;
    layout.rowsPerImage = 1;

    WGPUExtent3D extent = {1, 1, 1};
    wgpuQueueWriteTexture(m_queue, &dst, &white, sizeof(white), &layout, &extent);

    m_whiteTexture.view = wgpuTextureCreateView(m_whiteTexture.texture, nullptr);

    WGPUBindGroupEntry entries[2];
    memset(entries, 0, sizeof(entries));
    entries[0].binding = 0;
    entries[0].textureView = m_whiteTexture.view;
    entries[1].binding = 1;
    entries[1].sampler = m_sampler;

    WGPUBindGroupDescriptor bindDesc;
    memset(&bindDesc, 0, sizeof(bindDesc));
    bindDesc.layout = m_textureLayout;
    bindDesc.entryCount = 2;
    bindDesc.entries = entries;
    m_whiteTexture.bindGroup = wgpuDeviceCreateBindGroup(m_device, &bindDesc);

    return m_whiteTexture.bindGroup != nullptr;
}

bool WebGpuRenderer::CreateDefaultMaterialTextures()
{
    // Flat tangent-space normal (encoded 128,128,255) and a neutral ORM
    // (occlusion=1, roughness=1, metallic=0) -- what a material with no
    // normal/ORM map of its own samples, via MaterialGroupFor's fallback.
    WGPUTextureDescriptor desc;
    memset(&desc, 0, sizeof(desc));
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    desc.dimension = WGPUTextureDimension_2D;
    desc.size.width = 1;
    desc.size.height = 1;
    desc.size.depthOrArrayLayers = 1;
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;

    WGPUTexelCopyBufferLayout layout;
    memset(&layout, 0, sizeof(layout));
    layout.bytesPerRow = 4;
    layout.rowsPerImage = 1;
    WGPUExtent3D extent = {1, 1, 1};

    desc.label = Str("engine-default-normal");
    m_defaultNormalTexture = wgpuDeviceCreateTexture(m_device, &desc);
    if (!m_defaultNormalTexture)
        return false;
    const uint8_t flatNormal[4] = {128, 128, 255, 255};
    WGPUTexelCopyTextureInfo normalDst;
    memset(&normalDst, 0, sizeof(normalDst));
    normalDst.texture = m_defaultNormalTexture;
    normalDst.aspect = WGPUTextureAspect_All;
    wgpuQueueWriteTexture(m_queue, &normalDst, flatNormal, sizeof(flatNormal), &layout, &extent);
    m_defaultNormalView = wgpuTextureCreateView(m_defaultNormalTexture, nullptr);

    desc.label = Str("engine-default-orm");
    m_defaultOrmTexture = wgpuDeviceCreateTexture(m_device, &desc);
    if (!m_defaultOrmTexture)
        return false;
    const uint8_t neutralOrm[4] = {255, 255, 0, 255};
    WGPUTexelCopyTextureInfo ormDst;
    memset(&ormDst, 0, sizeof(ormDst));
    ormDst.texture = m_defaultOrmTexture;
    ormDst.aspect = WGPUTextureAspect_All;
    wgpuQueueWriteTexture(m_queue, &ormDst, neutralOrm, sizeof(neutralOrm), &layout, &extent);
    m_defaultOrmView = wgpuTextureCreateView(m_defaultOrmTexture, nullptr);

    return m_defaultNormalView != nullptr && m_defaultOrmView != nullptr;
}

bool WebGpuRenderer::EnsureShadowMap()
{
    if (m_shadowMapTexture)
        return true;

    WGPUSamplerDescriptor compareDesc;
    memset(&compareDesc, 0, sizeof(compareDesc));
    compareDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    compareDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    compareDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    compareDesc.magFilter = WGPUFilterMode_Linear;
    compareDesc.minFilter = WGPUFilterMode_Linear;
    compareDesc.maxAnisotropy = 1;
    // <=: a fragment at exactly the occluder's own depth is lit, not shadowed.
    compareDesc.compare = WGPUCompareFunction_LessEqual;
    m_shadowSamplerCompare = wgpuDeviceCreateSampler(m_device, &compareDesc);
    if (!m_shadowSamplerCompare)
    {
        Engine_LogError("WebGpu: shadow comparison sampler creation failed");
        return false;
    }

    WGPUTextureDescriptor desc;
    memset(&desc, 0, sizeof(desc));
    desc.label = Str("engine-shadow-map");
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    desc.dimension = WGPUTextureDimension_2D;
    desc.size.width = GFX_SHADOW_MAP_SIZE;
    desc.size.height = GFX_SHADOW_MAP_SIZE;
    desc.size.depthOrArrayLayers = 1;
    desc.format = WGPUTextureFormat_Depth32Float;
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    m_shadowMapTexture = wgpuDeviceCreateTexture(m_device, &desc);
    if (!m_shadowMapTexture)
    {
        Engine_LogError("WebGpu: shadow map texture creation failed");
        return false;
    }
    m_shadowMapView = wgpuTextureCreateView(m_shadowMapTexture, nullptr);
    return m_shadowMapView != nullptr;
}

bool WebGpuRenderer::CreatePbrPipeline()
{
    ShaderSource pbrSource;
    if (!ShaderAssets_Load("pbr.wgsl", &pbrSource))
        return false;

    char* source3D = BuildShaderSource3D(pbrSource.text.get(), pbrSource.size);
    if (!source3D)
    {
        Engine_LogError("WebGpu: could not build the PBR shader source (out of memory, or pbr.wgsl grew past its placeholders)");
        return false;
    }

    WGPUShaderSourceWGSL wgsl;
    memset(&wgsl, 0, sizeof(wgsl));
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = Str(source3D);

    WGPUShaderModuleDescriptor shaderDesc;
    memset(&shaderDesc, 0, sizeof(shaderDesc));
    shaderDesc.nextInChain = &wgsl.chain;
    shaderDesc.label = Str("engine-shader-pbr");

    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(m_device, &shaderDesc);
    free(source3D);
    if (!shader)
    {
        Engine_LogError("WebGpu: PBR shader module creation failed");
        return false;
    }

    // --- group 0: frame uniforms (camera, lights, ambient) + shadow map -----
    WGPUBindGroupLayoutEntry frameEntries[3];
    memset(frameEntries, 0, sizeof(frameEntries));
    frameEntries[0].binding = 0;
    frameEntries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    frameEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
    frameEntries[0].buffer.minBindingSize = sizeof(FrameUniforms3D);
    frameEntries[1].binding = 1;
    frameEntries[1].visibility = WGPUShaderStage_Fragment;
    frameEntries[1].texture.sampleType = WGPUTextureSampleType_Depth;
    frameEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    frameEntries[2].binding = 2;
    frameEntries[2].visibility = WGPUShaderStage_Fragment;
    frameEntries[2].sampler.type = WGPUSamplerBindingType_Comparison;

    WGPUBindGroupLayoutDescriptor frameLayoutDesc;
    memset(&frameLayoutDesc, 0, sizeof(frameLayoutDesc));
    frameLayoutDesc.entryCount = 3;
    frameLayoutDesc.entries = frameEntries;
    m_frameLayout3D = wgpuDeviceCreateBindGroupLayout(m_device, &frameLayoutDesc);

    // --- group 1: one per-run material, selected by a dynamic offset --------
    WGPUBindGroupLayoutEntry materialEntry;
    memset(&materialEntry, 0, sizeof(materialEntry));
    materialEntry.binding = 0;
    materialEntry.visibility = WGPUShaderStage_Fragment;
    materialEntry.buffer.type = WGPUBufferBindingType_Uniform;
    materialEntry.buffer.hasDynamicOffset = true;
    materialEntry.buffer.minBindingSize = sizeof(MaterialUniformGpu);

    WGPUBindGroupLayoutDescriptor materialLayoutDesc;
    memset(&materialLayoutDesc, 0, sizeof(materialLayoutDesc));
    materialLayoutDesc.entryCount = 1;
    materialLayoutDesc.entries = &materialEntry;
    m_materialLayout3D = wgpuDeviceCreateBindGroupLayout(m_device, &materialLayoutDesc);

    // --- group 2: albedo/normal/orm + one shared filtering sampler ----------
    WGPUBindGroupLayoutEntry texEntries[4];
    memset(texEntries, 0, sizeof(texEntries));
    for (uint32_t i = 0; i < 3; ++i)
    {
        texEntries[i].binding = i;
        texEntries[i].visibility = WGPUShaderStage_Fragment;
        texEntries[i].texture.sampleType = WGPUTextureSampleType_Float;
        texEntries[i].texture.viewDimension = WGPUTextureViewDimension_2D;
    }
    texEntries[3].binding = 3;
    texEntries[3].visibility = WGPUShaderStage_Fragment;
    texEntries[3].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor materialTexLayoutDesc;
    memset(&materialTexLayoutDesc, 0, sizeof(materialTexLayoutDesc));
    materialTexLayoutDesc.entryCount = 4;
    materialTexLayoutDesc.entries = texEntries;
    m_materialTexLayout3D = wgpuDeviceCreateBindGroupLayout(m_device, &materialTexLayoutDesc);

    if (!m_frameLayout3D || !m_materialLayout3D || !m_materialTexLayout3D)
    {
        Engine_LogError("WebGpu: PBR bind group layout creation failed");
        return false;
    }

    WGPUBufferDescriptor frameBufDesc;
    memset(&frameBufDesc, 0, sizeof(frameBufDesc));
    frameBufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    frameBufDesc.size = sizeof(FrameUniforms3D);
    m_frameUniformBuffer3D = wgpuDeviceCreateBuffer(m_device, &frameBufDesc);

    WGPUBufferDescriptor materialBufDesc;
    memset(&materialBufDesc, 0, sizeof(materialBufDesc));
    materialBufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    materialBufDesc.size = static_cast<uint64_t>(GFX_MAX_DRAW_RUNS) * WGPU_MATERIAL_UNIFORM_STRIDE;
    m_materialUniformBuffer3D = wgpuDeviceCreateBuffer(m_device, &materialBufDesc);

    if (!m_frameUniformBuffer3D || !m_materialUniformBuffer3D)
    {
        Engine_LogError("WebGpu: PBR uniform buffer creation failed");
        return false;
    }

    WGPUBindGroupEntry frameBindEntries[3];
    memset(frameBindEntries, 0, sizeof(frameBindEntries));
    frameBindEntries[0].binding = 0;
    frameBindEntries[0].buffer = m_frameUniformBuffer3D;
    frameBindEntries[0].size = sizeof(FrameUniforms3D);
    frameBindEntries[1].binding = 1;
    frameBindEntries[1].textureView = m_shadowMapView;
    frameBindEntries[2].binding = 2;
    frameBindEntries[2].sampler = m_shadowSamplerCompare;

    WGPUBindGroupDescriptor frameBindDesc;
    memset(&frameBindDesc, 0, sizeof(frameBindDesc));
    frameBindDesc.layout = m_frameLayout3D;
    frameBindDesc.entryCount = 3;
    frameBindDesc.entries = frameBindEntries;
    m_frameBindGroup3D = wgpuDeviceCreateBindGroup(m_device, &frameBindDesc);

    WGPUBindGroupEntry materialBindEntry;
    memset(&materialBindEntry, 0, sizeof(materialBindEntry));
    materialBindEntry.binding = 0;
    materialBindEntry.buffer = m_materialUniformBuffer3D;
    materialBindEntry.size = sizeof(MaterialUniformGpu);

    WGPUBindGroupDescriptor materialBindDesc;
    memset(&materialBindDesc, 0, sizeof(materialBindDesc));
    materialBindDesc.layout = m_materialLayout3D;
    materialBindDesc.entryCount = 1;
    materialBindDesc.entries = &materialBindEntry;
    m_materialBindGroup3D = wgpuDeviceCreateBindGroup(m_device, &materialBindDesc);

    if (!m_frameBindGroup3D || !m_materialBindGroup3D)
    {
        Engine_LogError("WebGpu: PBR bind group creation failed");
        return false;
    }

    WGPUBindGroupLayout layouts[3] = {m_frameLayout3D, m_materialLayout3D, m_materialTexLayout3D};
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc;
    memset(&pipelineLayoutDesc, 0, sizeof(pipelineLayoutDesc));
    pipelineLayoutDesc.bindGroupLayoutCount = 3;
    pipelineLayoutDesc.bindGroupLayouts = layouts;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(m_device, &pipelineLayoutDesc);

    WGPUVertexAttribute attributes[4];
    memset(attributes, 0, sizeof(attributes));
    attributes[0].format = WGPUVertexFormat_Float32x3; // position
    attributes[0].offset = 0;
    attributes[0].shaderLocation = 0;
    attributes[1].format = WGPUVertexFormat_Float32x3; // normal
    attributes[1].offset = sizeof(float) * 3;
    attributes[1].shaderLocation = 1;
    attributes[2].format = WGPUVertexFormat_Float32x2; // uv
    attributes[2].offset = sizeof(float) * 6;
    attributes[2].shaderLocation = 2;
    attributes[3].format = WGPUVertexFormat_Float32x4; // colour
    attributes[3].offset = sizeof(float) * 8;
    attributes[3].shaderLocation = 3;

    WGPUVertexBufferLayout vertexLayout;
    memset(&vertexLayout, 0, sizeof(vertexLayout));
    vertexLayout.arrayStride = sizeof(StagedGeometry::Vertex);
    vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexLayout.attributeCount = 4;
    vertexLayout.attributes = attributes;

    WGPUColorTargetState colorTarget;
    memset(&colorTarget, 0, sizeof(colorTarget));
    colorTarget.format = m_surfaceFormat;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment;
    memset(&fragment, 0, sizeof(fragment));
    fragment.module = shader;
    fragment.entryPoint = Str("fs_main_3d");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPUDepthStencilState depthState;
    memset(&depthState, 0, sizeof(depthState));
    depthState.format = WGPUTextureFormat_Depth24Plus;
    depthState.depthWriteEnabled = WGPUOptionalBool_True;
    depthState.depthCompare = WGPUCompareFunction_Less;
    depthState.stencilFront.compare = WGPUCompareFunction_Always;
    depthState.stencilBack.compare = WGPUCompareFunction_Always;

    WGPURenderPipelineDescriptor pipelineDesc;
    memset(&pipelineDesc, 0, sizeof(pipelineDesc));
    pipelineDesc.label = Str("engine-3d-pbr");
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shader;
    pipelineDesc.vertex.entryPoint = Str("vs_main_3d");
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexLayout;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    // No back-face culling: the baked level and model geometry does not carry a
    // guaranteed winding, and dropping triangles is worse than drawing extra.
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = 0xFFFFFFFFu;
    pipelineDesc.depthStencil = &depthState;
    pipelineDesc.fragment = &fragment;

    m_pipeline3D = wgpuDeviceCreateRenderPipeline(m_device, &pipelineDesc);
    if (!m_pipeline3D)
    {
        Engine_LogError("WebGpu: PBR pipeline creation failed");
        return false;
    }

    return true;
}

bool WebGpuRenderer::CreateShadowPipeline()
{
    ShaderSource shadowSource;
    if (!ShaderAssets_Load("shadow.wgsl", &shadowSource))
        return false;

    WGPUShaderSourceWGSL wgsl;
    memset(&wgsl, 0, sizeof(wgsl));
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = WGPUStringView{shadowSource.text.get(), shadowSource.size};

    WGPUShaderModuleDescriptor shaderDesc;
    memset(&shaderDesc, 0, sizeof(shaderDesc));
    shaderDesc.nextInChain = &wgsl.chain;
    shaderDesc.label = Str("engine-shader-shadow");

    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(m_device, &shaderDesc);
    if (!shader)
    {
        Engine_LogError("WebGpu: shadow shader module creation failed");
        return false;
    }

    WGPUBindGroupLayoutEntry uniformEntry;
    memset(&uniformEntry, 0, sizeof(uniformEntry));
    uniformEntry.binding = 0;
    uniformEntry.visibility = WGPUShaderStage_Vertex;
    uniformEntry.buffer.type = WGPUBufferBindingType_Uniform;
    uniformEntry.buffer.minBindingSize = sizeof(ShadowUniforms);

    WGPUBindGroupLayoutDescriptor layoutDesc;
    memset(&layoutDesc, 0, sizeof(layoutDesc));
    layoutDesc.entryCount = 1;
    layoutDesc.entries = &uniformEntry;
    m_shadowPassLayout = wgpuDeviceCreateBindGroupLayout(m_device, &layoutDesc);
    if (!m_shadowPassLayout)
    {
        Engine_LogError("WebGpu: shadow bind group layout creation failed");
        return false;
    }

    WGPUBufferDescriptor bufDesc;
    memset(&bufDesc, 0, sizeof(bufDesc));
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    bufDesc.size = sizeof(ShadowUniforms);
    m_shadowPassUniformBuffer = wgpuDeviceCreateBuffer(m_device, &bufDesc);
    if (!m_shadowPassUniformBuffer)
    {
        Engine_LogError("WebGpu: shadow uniform buffer creation failed");
        return false;
    }

    WGPUBindGroupEntry bindEntry;
    memset(&bindEntry, 0, sizeof(bindEntry));
    bindEntry.binding = 0;
    bindEntry.buffer = m_shadowPassUniformBuffer;
    bindEntry.size = sizeof(ShadowUniforms);

    WGPUBindGroupDescriptor bindDesc;
    memset(&bindDesc, 0, sizeof(bindDesc));
    bindDesc.layout = m_shadowPassLayout;
    bindDesc.entryCount = 1;
    bindDesc.entries = &bindEntry;
    m_shadowPassBindGroup = wgpuDeviceCreateBindGroup(m_device, &bindDesc);
    if (!m_shadowPassBindGroup)
    {
        Engine_LogError("WebGpu: shadow bind group creation failed");
        return false;
    }

    WGPUPipelineLayoutDescriptor pipelineLayoutDesc;
    memset(&pipelineLayoutDesc, 0, sizeof(pipelineLayoutDesc));
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_shadowPassLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(m_device, &pipelineLayoutDesc);

    // Position only: the shadow pass ignores normal/uv/colour, but the vertex
    // buffer is shared with the main pass, so the layout still declares the
    // full stride and simply does not bind the unused attributes.
    WGPUVertexAttribute attribute;
    memset(&attribute, 0, sizeof(attribute));
    attribute.format = WGPUVertexFormat_Float32x3;
    attribute.offset = 0;
    attribute.shaderLocation = 0;

    WGPUVertexBufferLayout vertexLayout;
    memset(&vertexLayout, 0, sizeof(vertexLayout));
    vertexLayout.arrayStride = sizeof(StagedGeometry::Vertex);
    vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexLayout.attributeCount = 1;
    vertexLayout.attributes = &attribute;

    WGPUDepthStencilState depthState;
    memset(&depthState, 0, sizeof(depthState));
    depthState.format = WGPUTextureFormat_Depth32Float;
    depthState.depthWriteEnabled = WGPUOptionalBool_True;
    depthState.depthCompare = WGPUCompareFunction_Less;
    depthState.stencilFront.compare = WGPUCompareFunction_Always;
    depthState.stencilBack.compare = WGPUCompareFunction_Always;

    WGPURenderPipelineDescriptor pipelineDesc;
    memset(&pipelineDesc, 0, sizeof(pipelineDesc));
    pipelineDesc.label = Str("engine-shadow");
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shader;
    pipelineDesc.vertex.entryPoint = Str("vs_main_shadow");
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexLayout;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = 0xFFFFFFFFu;
    pipelineDesc.depthStencil = &depthState;
    pipelineDesc.fragment = nullptr; // depth-only: no colour target

    m_shadowPipeline = wgpuDeviceCreateRenderPipeline(m_device, &pipelineDesc);
    if (!m_shadowPipeline)
    {
        Engine_LogError("WebGpu: shadow pipeline creation failed");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

uint32_t WebGpuRenderer::UploadTexture(const TextureUpload& upload)
{
    if (!m_initialized && !m_device)
        return 0;

    int slot = -1;
    for (int i = 0; i < WGPU_MAX_TEXTURES; ++i)
    {
        if (!m_textures[i].texture)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
    {
        Engine_LogError("WebGpuRenderer: texture registry full (%d)", WGPU_MAX_TEXTURES);
        return 0;
    }

    const uint32_t width = static_cast<uint32_t>(upload.width);
    const uint32_t height = static_cast<uint32_t>(upload.height);
    const size_t texels = static_cast<size_t>(width) * height;
    if (width == 0 || height == 0)
        return 0;

    // Every source format is expanded to RGBA8 here. A desktop GPU has no reason
    // to carry the PS2 storage modes, and the resource manager's byte budget is
    // computed against the same assumption (see Platform::GetTextureFootprintBytes).
    uint32_t* rgba = static_cast<uint32_t*>(malloc(texels * 4));
    if (!rgba)
    {
        Engine_LogError("WebGpuRenderer: out of memory expanding a %ux%u texture", width, height);
        return 0;
    }

    if (!Gfx_ExpandToRgba8(upload, reinterpret_cast<uint8_t*>(rgba), texels * 4u))
    {
        free(rgba);
        Engine_LogError("WebGpuRenderer: could not expand a %ux%u texture", width, height);
        return 0;
    }

    WGPUTextureDescriptor desc;
    memset(&desc, 0, sizeof(desc));
    desc.label = Str("engine-texture");
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    desc.dimension = WGPUTextureDimension_2D;
    desc.size.width = width;
    desc.size.height = height;
    desc.size.depthOrArrayLayers = 1;
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    // Only level 0 is uploaded: the baked mip chain is in the PS2 storage layout,
    // and expanding every level would cost more than it buys before there is a
    // measured need for it.
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;

    WGPUTexture texture = wgpuDeviceCreateTexture(m_device, &desc);
    if (!texture)
    {
        free(rgba);
        Engine_LogError("WebGpuRenderer: wgpuDeviceCreateTexture failed for %ux%u", width, height);
        return 0;
    }

    WGPUTexelCopyTextureInfo dst;
    memset(&dst, 0, sizeof(dst));
    dst.texture = texture;
    dst.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout layout;
    memset(&layout, 0, sizeof(layout));
    layout.bytesPerRow = width * 4u;
    layout.rowsPerImage = height;

    WGPUExtent3D extent = {width, height, 1};
    wgpuQueueWriteTexture(m_queue, &dst, rgba, texels * 4, &layout, &extent);
    free(rgba);

    WGPUTextureView view = wgpuTextureCreateView(texture, nullptr);

    WGPUBindGroupEntry entries[2];
    memset(entries, 0, sizeof(entries));
    entries[0].binding = 0;
    entries[0].textureView = view;
    entries[1].binding = 1;
    entries[1].sampler = (upload.filter == TextureFilter::Nearest) ? m_samplerNearest : m_sampler;

    WGPUBindGroupDescriptor bindDesc;
    memset(&bindDesc, 0, sizeof(bindDesc));
    bindDesc.layout = m_textureLayout;
    bindDesc.entryCount = 2;
    bindDesc.entries = entries;

    m_textures[slot].texture = texture;
    m_textures[slot].view = view;
    m_textures[slot].bindGroup = wgpuDeviceCreateBindGroup(m_device, &bindDesc);

    // index+1, so 0 stays the universal "invalid handle".
    return static_cast<uint32_t>(slot) + 1u;
}

void WebGpuRenderer::ReleaseTexture(uint32_t handle)
{
    if (handle == 0 || handle > WGPU_MAX_TEXTURES)
        return;

    TextureEntry& e = m_textures[handle - 1u];
    if (e.bindGroup)
        wgpuBindGroupRelease(e.bindGroup);
    if (e.view)
        wgpuTextureViewRelease(e.view);
    if (e.texture)
    {
        wgpuTextureDestroy(e.texture);
        wgpuTextureRelease(e.texture);
    }
    memset(&e, 0, sizeof(e));

    // Any cached PBR material group naming this handle now holds a view onto
    // a destroyed texture -- WebGPU invalidates it regardless of the bind
    // group's own reference, so the entry must be dropped eagerly, not left
    // to go stale.
    PurgeMaterialGroupsReferencing(handle);
}

WGPUBindGroup WebGpuRenderer::BindGroupFor(uint32_t handle) const
{
    if (handle == 0 || handle > WGPU_MAX_TEXTURES)
        return m_whiteTexture.bindGroup;
    return m_textures[handle - 1u].bindGroup ? m_textures[handle - 1u].bindGroup : m_whiteTexture.bindGroup;
}

WGPUBindGroup WebGpuRenderer::MaterialGroupFor(uint32_t albedo, uint32_t normal, uint32_t orm)
{
    for (uint32_t i = 0; i < m_materialGroupCount; ++i)
    {
        const MaterialGroupEntry& e = m_materialGroups[i];
        if (e.albedo == albedo && e.normal == normal && e.orm == orm)
            return e.group;
    }

    if (m_materialGroupCount >= WGPU_MAX_MATERIAL_GROUPS)
    {
        // Refuse whole rather than exceed the fixed cache: reuse whatever
        // group is already cached at slot 0 instead of creating a 257th
        // object. Visually wrong for this one run, but the material-tex
        // layout still matches (unlike m_whiteTexture.bindGroup, which is
        // a different, smaller layout), so the draw itself stays valid.
        Engine_LogError("WebGpuRenderer: material bind group cache full (%d)", WGPU_MAX_MATERIAL_GROUPS);
        return m_materialGroups[0].group;
    }

    WGPUTextureView albedoView = (albedo == 0 || albedo > WGPU_MAX_TEXTURES) ? m_whiteTexture.view : (m_textures[albedo - 1u].view ? m_textures[albedo - 1u].view : m_whiteTexture.view);
    WGPUTextureView normalView = (normal == 0 || normal > WGPU_MAX_TEXTURES) ? m_defaultNormalView : (m_textures[normal - 1u].view ? m_textures[normal - 1u].view : m_defaultNormalView);
    WGPUTextureView ormView = (orm == 0 || orm > WGPU_MAX_TEXTURES) ? m_defaultOrmView : (m_textures[orm - 1u].view ? m_textures[orm - 1u].view : m_defaultOrmView);

    WGPUBindGroupEntry entries[4];
    memset(entries, 0, sizeof(entries));
    entries[0].binding = 0;
    entries[0].textureView = albedoView;
    entries[1].binding = 1;
    entries[1].textureView = normalView;
    entries[2].binding = 2;
    entries[2].textureView = ormView;
    entries[3].binding = 3;
    entries[3].sampler = m_sampler;

    WGPUBindGroupDescriptor bindDesc;
    memset(&bindDesc, 0, sizeof(bindDesc));
    bindDesc.layout = m_materialTexLayout3D;
    bindDesc.entryCount = 4;
    bindDesc.entries = entries;

    WGPUBindGroup group = wgpuDeviceCreateBindGroup(m_device, &bindDesc);
    if (!group)
    {
        Engine_LogError("WebGpuRenderer: material bind group creation failed");
        return m_whiteTexture.bindGroup;
    }

    MaterialGroupEntry& slot = m_materialGroups[m_materialGroupCount++];
    slot.albedo = albedo;
    slot.normal = normal;
    slot.orm = orm;
    slot.group = group;
    return group;
}

void WebGpuRenderer::PurgeMaterialGroupsReferencing(uint32_t handle)
{
    for (uint32_t i = 0; i < m_materialGroupCount;)
    {
        MaterialGroupEntry& e = m_materialGroups[i];
        if (e.albedo != handle && e.normal != handle && e.orm != handle)
        {
            ++i;
            continue;
        }
        if (e.group)
            wgpuBindGroupRelease(e.group);
        // Swap-erase: draw-run order never depends on this table's order.
        m_materialGroups[i] = m_materialGroups[m_materialGroupCount - 1u];
        --m_materialGroupCount;
    }
}

void WebGpuRenderer::UploadMaterialUniforms(const StagedGeometry::DrawRun* runs, uint32_t count)
{
    if (count == 0)
        return;
    if (count > GFX_MAX_DRAW_RUNS)
        count = GFX_MAX_DRAW_RUNS; // BuildFrame already enforces this cap; defensive only

    // One CPU-side scratch buffer, written in one call: wgpuQueueWriteBuffer
    // cannot be interleaved with draw calls inside an already-open render
    // pass, so every run's material data for this frame is uploaded here,
    // before the pass begins, and selected per-draw by a dynamic offset.
    static_assert(sizeof(MaterialUniformGpu) <= WGPU_MATERIAL_UNIFORM_STRIDE, "material uniform stride too small");
    uint8_t* scratch = static_cast<uint8_t*>(malloc(static_cast<size_t>(count) * WGPU_MATERIAL_UNIFORM_STRIDE));
    if (!scratch)
        return;

    for (uint32_t i = 0; i < count; ++i)
    {
        MaterialUniformGpu gpu;
        memset(&gpu, 0, sizeof(gpu));
        const StagedGeometry::RunMaterial& m = runs[i].material;
        gpu.baseColor[0] = m.baseColor[0];
        gpu.baseColor[1] = m.baseColor[1];
        gpu.baseColor[2] = m.baseColor[2];
        gpu.baseColor[3] = m.baseColor[3];
        gpu.emissive[0] = m.emissive[0];
        gpu.emissive[1] = m.emissive[1];
        gpu.emissive[2] = m.emissive[2];
        gpu.metallicRoughnessNormalAlpha[0] = m.metallic;
        gpu.metallicRoughnessNormalAlpha[1] = m.roughness;
        gpu.metallicRoughnessNormalAlpha[2] = m.normalScale;
        gpu.metallicRoughnessNormalAlpha[3] = m.alphaCutoff;
        gpu.matFlags[0] = (m.flags & MATERIAL_FLAG_ALPHA_MASK) ? 1.0f : 0.0f;
        memcpy(scratch + static_cast<size_t>(i) * WGPU_MATERIAL_UNIFORM_STRIDE, &gpu, sizeof(gpu));
    }

    wgpuQueueWriteBuffer(m_queue, m_materialUniformBuffer3D, 0, scratch, static_cast<size_t>(count) * WGPU_MATERIAL_UNIFORM_STRIDE);
    free(scratch);
}


// ---------------------------------------------------------------------------
// Surface and depth
// ---------------------------------------------------------------------------

bool WebGpuRenderer::ConfigureSurface(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return true; // minimised; keep the old configuration

    WGPUSurfaceConfiguration config;
    memset(&config, 0, sizeof(config));
    config.device = m_device;
    config.format = m_surfaceFormat;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.width = width;
    config.height = height;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;
    config.presentMode = WGPUPresentMode_Fifo; // vsync

    wgpuSurfaceConfigure(m_surface, &config);

    m_width = width;
    m_height = height;
    return EnsureDepthTexture(width, height);
}

bool WebGpuRenderer::EnsureDepthTexture(uint32_t width, uint32_t height)
{
    ReleaseDepthTexture();

    WGPUTextureDescriptor desc;
    memset(&desc, 0, sizeof(desc));
    desc.label = Str("engine-depth");
    desc.usage = WGPUTextureUsage_RenderAttachment;
    desc.dimension = WGPUTextureDimension_2D;
    desc.size.width = width;
    desc.size.height = height;
    desc.size.depthOrArrayLayers = 1;
    desc.format = WGPUTextureFormat_Depth24Plus;
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;

    m_depthTexture = wgpuDeviceCreateTexture(m_device, &desc);
    if (!m_depthTexture)
    {
        Engine_LogError("WebGpu: depth texture creation failed");
        return false;
    }
    m_depthView = wgpuTextureCreateView(m_depthTexture, nullptr);
    return m_depthView != nullptr;
}

void WebGpuRenderer::ReleaseDepthTexture()
{
    if (m_depthView)
    {
        wgpuTextureViewRelease(m_depthView);
        m_depthView = nullptr;
    }
    if (m_depthTexture)
    {
        wgpuTextureDestroy(m_depthTexture);
        wgpuTextureRelease(m_depthTexture);
        m_depthTexture = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Draw-list submission
// ---------------------------------------------------------------------------

void WebGpuRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale)
{
    AddPrimitiveToDrawList(primitive, position, rotation, scale, Color3{1.0f, 1.0f, 1.0f}, -1);
}

void WebGpuRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, Color3 color)
{
    AddPrimitiveToDrawList(primitive, position, rotation, scale, color, -1);
}

void WebGpuRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, int32_t textureId)
{
    AddPrimitiveToDrawList(primitive, position, rotation, scale, Color3{1.0f, 1.0f, 1.0f}, textureId);
}

void WebGpuRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, Color3 color, int32_t textureId)
{
    PrimitiveDrawEntry entry;
    entry.transform = Transform3D(position, rotation, scale);
    entry.color = color;
    entry.type = primitive;
    entry.textureId = textureId;
    m_drawLists.AddPrimitive(entry);
}

// Level geometry is pulled from the sector manager at render time rather than
// queued here, matching the PS2 backends: the resident ring is the source of
// truth and re-queueing it every frame would duplicate that state.
void WebGpuRenderer::AddLevelToDrawList(const Level& level) { UNUSED_VAR(level); }

void WebGpuRenderer::AddModelToDrawList(int32_t modelId, const Vector3& position, const Vector3& rotation, const Vector3& scale)
{
    ModelDrawEntry entry;
    entry.resourceId = modelId;
    entry.transform = Transform3D(position, rotation, scale);
    m_drawLists.AddModel(entry);
}

void WebGpuRenderer::AddSkyToDrawList(int32_t resourceId) { m_drawLists.SetSkyboxTexture(resourceId); }

void WebGpuRenderer::ClearDrawLists() { m_drawLists.Reset(false); }

void WebGpuRenderer::ClearFrame(const Color3& color) { m_clearColor = color; }

void WebGpuRenderer::DrawQuad2D(const Quad2D& quad) { m_geometry.AddQuad2D(quad); }

void WebGpuRenderer::DrawGrid(int32_t slices, float spacing)
{
    UNUSED_VAR(slices);
    UNUSED_VAR(spacing);
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void WebGpuRenderer::BeginFrame()
{
    // A resize invalidates the swapchain and the depth buffer together.
    Platform* platform = Engine_GetPlatform();
    uint32_t w = 0, h = 0;
    platform->GetFramebufferSize(&w, &h);
    if ((w != m_width || h != m_height) && w > 0 && h > 0)
        ConfigureSurface(w, h);

    m_geometry.SetFrameBudget(0, m_width, m_height);
    m_geometry.BeginFrame();
    m_frameStats = DrawStats{};
}

void WebGpuRenderer::Render() { m_geometry.BuildFrame(m_drawLists, &m_frameStats); }

void WebGpuRenderer::RenderShadowMap(const DrawLists& lists)
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
    const uint32_t dynCount = m_geometry.Count3D() - dynStart;
    if (dynCount == 0)
        return; // nothing dynamic to cast this frame; leave the map unsampled (see fs_main_3d)

    // A frustum centred on the camera, not the whole level: this pass only
    // ever covers dynamic (model/primitive) geometry, which clusters near
    // wherever the camera is looking, not the static world. See the member
    // comment on BuildLightViewProjection.
    const float kShadowHalfExtent = 24.0f;
    const float kShadowDepthExtent = 120.0f;
    StagedGeometry::BuildLightViewProjection(caster.direction, lists.GetCamera3D().position, kShadowHalfExtent, kShadowDepthExtent, true, m_lastLightViewProj);

    ShadowUniforms su;
    memcpy(su.lightViewProj, m_lastLightViewProj, sizeof(su.lightViewProj));
    wgpuQueueWriteBuffer(m_queue, m_shadowPassUniformBuffer, 0, &su, sizeof(su));

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_device, nullptr);

    WGPURenderPassDepthStencilAttachment depthAttachment;
    memset(&depthAttachment, 0, sizeof(depthAttachment));
    depthAttachment.view = m_shadowMapView;
    depthAttachment.depthLoadOp = WGPULoadOp_Clear;
    depthAttachment.depthStoreOp = WGPUStoreOp_Store;
    depthAttachment.depthClearValue = 1.0f;

    WGPURenderPassDescriptor passDesc;
    memset(&passDesc, 0, sizeof(passDesc));
    passDesc.colorAttachmentCount = 0;
    passDesc.depthStencilAttachment = &depthAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, m_shadowPipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_shadowPassBindGroup, 0, nullptr);
    // One draw over every dynamic vertex: the shadow pass has no per-material
    // texture binding to change between runs (no alpha-mask cutout support
    // yet -- see shadow.wgsl), so there is nothing run boundaries buy it.
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_vertexBuffer, 0, static_cast<uint64_t>(m_geometry.Count3D()) * sizeof(StagedGeometry::Vertex));
    wgpuRenderPassEncoderDraw(pass, dynCount, 1, dynStart, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(m_queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);

    m_shadowActive = true;
    m_shadowCasterIndex = casterId;
}

void WebGpuRenderer::EndFrame()
{
    if (!m_initialized)
        return;

    WGPUSurfaceTexture surfaceTexture;
    memset(&surfaceTexture, 0, sizeof(surfaceTexture));
    wgpuSurfaceGetCurrentTexture(m_surface, &surfaceTexture);

    // Lost/outdated is normal after a resize or a mode switch: reconfigure and
    // skip this frame rather than treating it as an error.
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal && surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
    {
        if (surfaceTexture.texture)
            wgpuTextureRelease(surfaceTexture.texture);
        ConfigureSurface(m_width, m_height);
        m_geometry.EndFrame();
        m_drawLists.Reset(false);
        return;
    }

    WGPUTextureView backbuffer = wgpuTextureCreateView(surfaceTexture.texture, nullptr);

    // Upload both ranges into one buffer, 3D first, so each pass draws a
    // contiguous slice and only the pipeline and bind group change between them.
    const uint32_t count3D = m_geometry.Count3D();
    const uint32_t count2D = m_geometry.Count2D();
    const uint32_t totalVerts = count3D + count2D;
    if (totalVerts > 0)
    {
        const uint64_t needed = static_cast<uint64_t>(totalVerts) * sizeof(StagedGeometry::Vertex);
        if (needed > m_vertexBufferCapacity)
        {
            if (m_vertexBuffer)
            {
                wgpuBufferDestroy(m_vertexBuffer);
                wgpuBufferRelease(m_vertexBuffer);
            }
            WGPUBufferDescriptor desc;
            memset(&desc, 0, sizeof(desc));
            desc.label = Str("engine-vertices");
            desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            desc.size = needed * 2u; // headroom, so a growing scene does not realloc every frame
            m_vertexBuffer = wgpuDeviceCreateBuffer(m_device, &desc);
            m_vertexBufferCapacity = desc.size;
        }

        if (count3D > 0)
            wgpuQueueWriteBuffer(m_queue, m_vertexBuffer, 0, m_geometry.Vertices3D(), static_cast<size_t>(count3D) * sizeof(StagedGeometry::Vertex));
        if (count2D > 0)
            wgpuQueueWriteBuffer(m_queue, m_vertexBuffer, static_cast<uint64_t>(count3D) * sizeof(StagedGeometry::Vertex), m_geometry.Vertices2D(),
                                 static_cast<size_t>(count2D) * sizeof(StagedGeometry::Vertex));
    }

    // The shadow map is rendered (its own encoder, submitted separately) and
    // the per-run material array is uploaded before the main pass's encoder
    // is even created: both write buffers, and a queue write cannot be
    // interleaved with draw calls inside an already-open render pass.
    RenderShadowMap(m_drawLists);
    UploadMaterialUniforms(m_geometry.Runs(), m_geometry.RunCount());

    FrameUniforms3D frameUniforms;
    memset(&frameUniforms, 0, sizeof(frameUniforms));
    // WebGPU clips Z to [0,1]; the shared builder takes that as a flag.
    StagedGeometry::BuildViewProjection(m_drawLists.GetCamera3D(), m_width, m_height, true, frameUniforms.viewProj);
    memcpy(frameUniforms.lightViewProj, m_lastLightViewProj, sizeof(frameUniforms.lightViewProj));
    const Vector3& camPos = m_drawLists.GetCamera3D().position;
    frameUniforms.cameraPos[0] = camPos.x;
    frameUniforms.cameraPos[1] = camPos.y;
    frameUniforms.cameraPos[2] = camPos.z;
    const Color3& ambient = m_drawLists.GetAmbientLight();
    frameUniforms.ambient[0] = ambient.r;
    frameUniforms.ambient[1] = ambient.g;
    frameUniforms.ambient[2] = ambient.b;
    const Light3D* lights = m_drawLists.GetLights();
    for (uint32_t i = 0; i < GFX_MAX_LIGHTS; ++i)
    {
        GpuLight& gl = frameUniforms.lights[i];
        const Light3D& l = lights[i];
        gl.positionOrDir[0] = (l.type == LightType::Directional) ? l.direction.x : l.position.x;
        gl.positionOrDir[1] = (l.type == LightType::Directional) ? l.direction.y : l.position.y;
        gl.positionOrDir[2] = (l.type == LightType::Directional) ? l.direction.z : l.position.z;
        gl.positionOrDir[3] = (l.type == LightType::Directional) ? 0.0f : 1.0f;
        gl.colorIntensity[0] = l.color.r;
        gl.colorIntensity[1] = l.color.g;
        gl.colorIntensity[2] = l.color.b;
        gl.colorIntensity[3] = l.intensity; // <= 0 means "off"; the shader skips it
        gl.rangeParams[0] = l.range;
    }
    frameUniforms.shadowCaster[0] = m_shadowActive ? static_cast<float>(m_shadowCasterIndex) : -1.0f;
    wgpuQueueWriteBuffer(m_queue, m_frameUniformBuffer3D, 0, &frameUniforms, sizeof(frameUniforms));

    Uniforms uniforms;
    StagedGeometry::BuildOrtho2D(m_width, m_height, true, uniforms.viewProj);
    wgpuQueueWriteBuffer(m_queue, m_uniformBuffer2D, 0, &uniforms, sizeof(uniforms));

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_device, nullptr);

    WGPURenderPassColorAttachment colorAttachment;
    memset(&colorAttachment, 0, sizeof(colorAttachment));
    colorAttachment.view = backbuffer;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = WGPUColor{m_clearColor.r, m_clearColor.g, m_clearColor.b, 1.0};
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDepthStencilAttachment depthAttachment;
    memset(&depthAttachment, 0, sizeof(depthAttachment));
    depthAttachment.view = m_depthView;
    depthAttachment.depthLoadOp = WGPULoadOp_Clear;
    depthAttachment.depthStoreOp = WGPUStoreOp_Store;
    depthAttachment.depthClearValue = 1.0f;

    WGPURenderPassDescriptor passDesc;
    memset(&passDesc, 0, sizeof(passDesc));
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    passDesc.depthStencilAttachment = m_depthView ? &depthAttachment : nullptr;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

    if (totalVerts > 0)
    {
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_vertexBuffer, 0, static_cast<uint64_t>(totalVerts) * sizeof(StagedGeometry::Vertex));

        // 3D: one draw per material run, PBR-shaded.
        if (count3D > 0)
        {
            wgpuRenderPassEncoderSetPipeline(pass, m_pipeline3D);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, m_frameBindGroup3D, 0, nullptr);
            const StagedGeometry::DrawRun* runs = m_geometry.Runs();
            for (uint32_t i = 0; i < m_geometry.RunCount(); ++i)
            {
                const uint32_t materialOffset = i * WGPU_MATERIAL_UNIFORM_STRIDE;
                wgpuRenderPassEncoderSetBindGroup(pass, 1, m_materialBindGroup3D, 1, &materialOffset);
                wgpuRenderPassEncoderSetBindGroup(pass, 2, MaterialGroupFor(runs[i].texture, runs[i].material.normalTexture, runs[i].material.ormTexture), 0, nullptr);
                wgpuRenderPassEncoderDraw(pass, runs[i].count, 1, runs[i].first, 0);
            }
        }

        // 2D: one draw per texture run, over the top of the world.
        if (count2D > 0)
        {
            wgpuRenderPassEncoderSetPipeline(pass, m_pipeline2D);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, m_bindGroup2D, 0, nullptr);
            const StagedGeometry::DrawRun* runs2D = m_geometry.Runs2D();
            for (uint32_t i = 0; i < m_geometry.RunCount2D(); ++i)
            {
                wgpuRenderPassEncoderSetBindGroup(pass, 1, BindGroupFor(runs2D[i].texture), 0, nullptr);
                wgpuRenderPassEncoderDraw(pass, runs2D[i].count, 1, count3D + runs2D[i].first, 0);
            }
        }
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(m_queue, 1, &commands);

    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(backbuffer);

    wgpuSurfacePresent(m_surface);
    wgpuTextureRelease(surfaceTexture.texture);

    // 2D is consumed here, not at BeginFrame: this is when the game has finished
    // submitting it.
    m_geometry.EndFrame();

    m_drawLists.SetLastStats(m_frameStats);
    m_drawLists.Reset(false);
}


// ---------------------------------------------------------------------------
// Render-to-image (Ui_Image3D)
// ---------------------------------------------------------------------------

bool WebGpuRenderer::EnsureImageTarget(int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;

    if (m_imageTextureSlot < 0)
    {
        for (int i = 0; i < WGPU_MAX_TEXTURES; ++i)
        {
            if (!m_textures[i].texture)
            {
                m_imageTextureSlot = i;
                break;
            }
        }
        if (m_imageTextureSlot < 0)
        {
            Engine_LogError("WebGpuRenderer: texture registry full (%d), no slot for the image target", WGPU_MAX_TEXTURES);
            return false;
        }
    }

    if (m_imageColorTexture && m_imageWidth == width && m_imageHeight == height)
        return true;

    // Release the previous size's objects. m_textures[slot] is reassigned
    // below rather than freed through ReleaseTexture, since that would also
    // give the slot back for UploadTexture to reclaim.
    if (m_textures[m_imageTextureSlot].bindGroup)
        wgpuBindGroupRelease(m_textures[m_imageTextureSlot].bindGroup);
    if (m_imageColorView)
        wgpuTextureViewRelease(m_imageColorView);
    if (m_imageColorTexture)
    {
        wgpuTextureDestroy(m_imageColorTexture);
        wgpuTextureRelease(m_imageColorTexture);
    }
    if (m_imageDepthView)
        wgpuTextureViewRelease(m_imageDepthView);
    if (m_imageDepthTexture)
    {
        wgpuTextureDestroy(m_imageDepthTexture);
        wgpuTextureRelease(m_imageDepthTexture);
    }
    memset(&m_textures[m_imageTextureSlot], 0, sizeof(m_textures[m_imageTextureSlot]));

    WGPUTextureDescriptor colorDesc;
    memset(&colorDesc, 0, sizeof(colorDesc));
    colorDesc.label = Str("engine-image-target");
    colorDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    colorDesc.dimension = WGPUTextureDimension_2D;
    colorDesc.size.width = static_cast<uint32_t>(width);
    colorDesc.size.height = static_cast<uint32_t>(height);
    colorDesc.size.depthOrArrayLayers = 1;
    colorDesc.format = WGPUTextureFormat_RGBA8Unorm;
    colorDesc.mipLevelCount = 1;
    colorDesc.sampleCount = 1;
    m_imageColorTexture = wgpuDeviceCreateTexture(m_device, &colorDesc);

    WGPUTextureDescriptor depthDesc;
    memset(&depthDesc, 0, sizeof(depthDesc));
    depthDesc.label = Str("engine-image-target-depth");
    depthDesc.usage = WGPUTextureUsage_RenderAttachment;
    depthDesc.dimension = WGPUTextureDimension_2D;
    depthDesc.size.width = static_cast<uint32_t>(width);
    depthDesc.size.height = static_cast<uint32_t>(height);
    depthDesc.size.depthOrArrayLayers = 1;
    depthDesc.format = WGPUTextureFormat_Depth24Plus;
    depthDesc.mipLevelCount = 1;
    depthDesc.sampleCount = 1;
    m_imageDepthTexture = wgpuDeviceCreateTexture(m_device, &depthDesc);

    if (!m_imageColorTexture || !m_imageDepthTexture)
    {
        Engine_LogError("WebGpuRenderer: offscreen image target %dx%d creation failed", width, height);
        return false;
    }

    m_imageColorView = wgpuTextureCreateView(m_imageColorTexture, nullptr);
    m_imageDepthView = wgpuTextureCreateView(m_imageDepthTexture, nullptr);

    WGPUBindGroupEntry entries[2];
    memset(entries, 0, sizeof(entries));
    entries[0].binding = 0;
    entries[0].textureView = m_imageColorView;
    entries[1].binding = 1;
    entries[1].sampler = m_sampler;

    WGPUBindGroupDescriptor bindDesc;
    memset(&bindDesc, 0, sizeof(bindDesc));
    bindDesc.layout = m_textureLayout;
    bindDesc.entryCount = 2;
    bindDesc.entries = entries;

    m_textures[m_imageTextureSlot].texture = m_imageColorTexture;
    m_textures[m_imageTextureSlot].view = m_imageColorView;
    m_textures[m_imageTextureSlot].bindGroup = wgpuDeviceCreateBindGroup(m_device, &bindDesc);
    if (!m_textures[m_imageTextureSlot].bindGroup)
        return false;

    m_imageWidth = width;
    m_imageHeight = height;
    return true;
}

uint32_t WebGpuRenderer::RenderToImage3D(const Renderable3D& what, const Camera3D& camera, int width, int height, const Color3& clearColor)
{
    if (!m_initialized)
        return 0;
    if (!EnsureImageTarget(width, height))
        return 0;
    if (!m_imageGeometry.BuildOne(what, m_drawLists))
        return 0;

    const uint32_t count = m_imageGeometry.Count3D();
    if (count > 0)
    {
        const uint64_t needed = static_cast<uint64_t>(count) * sizeof(StagedGeometry::Vertex);
        if (needed > m_imageVertexBufferCapacity)
        {
            if (m_imageVertexBuffer)
            {
                wgpuBufferDestroy(m_imageVertexBuffer);
                wgpuBufferRelease(m_imageVertexBuffer);
            }
            WGPUBufferDescriptor desc;
            memset(&desc, 0, sizeof(desc));
            desc.label = Str("engine-image-vertices");
            desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            desc.size = needed * 2u;
            m_imageVertexBuffer = wgpuDeviceCreateBuffer(m_device, &desc);
            m_imageVertexBufferCapacity = desc.size;
        }
        wgpuQueueWriteBuffer(m_queue, m_imageVertexBuffer, 0, m_imageGeometry.Vertices3D(), static_cast<size_t>(count) * sizeof(StagedGeometry::Vertex));
    }

    Uniforms uniforms;
    StagedGeometry::BuildViewProjection(camera, static_cast<uint32_t>(width), static_cast<uint32_t>(height), true, uniforms.viewProj);
    wgpuQueueWriteBuffer(m_queue, m_imageUniformBuffer, 0, &uniforms, sizeof(uniforms));

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_device, nullptr);

    WGPURenderPassColorAttachment colorAttachment;
    memset(&colorAttachment, 0, sizeof(colorAttachment));
    colorAttachment.view = m_imageColorView;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = WGPUColor{clearColor.r, clearColor.g, clearColor.b, 1.0};
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDepthStencilAttachment depthAttachment;
    memset(&depthAttachment, 0, sizeof(depthAttachment));
    depthAttachment.view = m_imageDepthView;
    depthAttachment.depthLoadOp = WGPULoadOp_Clear;
    depthAttachment.depthStoreOp = WGPUStoreOp_Store;
    depthAttachment.depthClearValue = 1.0f;

    WGPURenderPassDescriptor passDesc;
    memset(&passDesc, 0, sizeof(passDesc));
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    passDesc.depthStencilAttachment = &depthAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

    if (count > 0)
    {
        wgpuRenderPassEncoderSetPipeline(pass, m_imagePipeline3D);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, m_imageUniformBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_imageVertexBuffer, 0, static_cast<uint64_t>(count) * sizeof(StagedGeometry::Vertex));

        const StagedGeometry::DrawRun* runs = m_imageGeometry.Runs();
        for (uint32_t i = 0; i < m_imageGeometry.RunCount(); ++i)
        {
            wgpuRenderPassEncoderSetBindGroup(pass, 1, BindGroupFor(runs[i].texture), 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, runs[i].count, 1, runs[i].first, 0);
        }
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
    // Submitting is sufficient: WebGPU orders every later submission on this
    // queue (the main pass, the UI draw that reads this texture) after this
    // one, so no CPU-side wait is needed for the result to be sampleable this
    // same frame.
    wgpuQueueSubmit(m_queue, 1, &commands);

    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);

    return static_cast<uint32_t>(m_imageTextureSlot) + 1u;
}

// ---------------------------------------------------------------------------
// Cameras and lifecycle
// ---------------------------------------------------------------------------

void WebGpuRenderer::SetCamera3D(CameraID id, const Camera3D& camera) { m_drawLists.SetCamera3D(id, camera); }
void WebGpuRenderer::SetActiveCamera3D(CameraID id) { m_drawLists.SetActiveCamera3D(id); }
void WebGpuRenderer::SetActiveCamera2D(const Camera2D& camera) { m_drawLists.SetActiveCamera2D(camera); }
void WebGpuRenderer::SetLight3D(LightID id, const Light3D& light) { m_drawLists.SetLight3D(id, light); }
void WebGpuRenderer::SetAmbientLight(const Color3& color) { m_drawLists.SetAmbientLight(color); }
void WebGpuRenderer::SetShadowCasterLight(LightID id) { m_drawLists.SetShadowCasterLight(id); }

bool WebGpuRenderer::IsInitialized() const { return m_initialized; }

void WebGpuRenderer::Shutdown()
{
    if (!m_initialized)
        return;

    for (uint32_t i = 0; i < WGPU_MAX_TEXTURES; ++i)
        ReleaseTexture(i + 1u);

    // Any remaining material group is necessarily the all-defaults (0,0,0)
    // combination -- every entry naming a real texture was already dropped
    // by the ReleaseTexture loop above, via PurgeMaterialGroupsReferencing.
    for (uint32_t i = 0; i < m_materialGroupCount; ++i)
    {
        if (m_materialGroups[i].group)
            wgpuBindGroupRelease(m_materialGroups[i].group);
    }
    m_materialGroupCount = 0;

    if (m_whiteTexture.bindGroup)
        wgpuBindGroupRelease(m_whiteTexture.bindGroup);
    if (m_whiteTexture.view)
        wgpuTextureViewRelease(m_whiteTexture.view);
    if (m_whiteTexture.texture)
        wgpuTextureRelease(m_whiteTexture.texture);

    if (m_defaultNormalView)
        wgpuTextureViewRelease(m_defaultNormalView);
    if (m_defaultNormalTexture)
    {
        wgpuTextureDestroy(m_defaultNormalTexture);
        wgpuTextureRelease(m_defaultNormalTexture);
    }
    if (m_defaultOrmView)
        wgpuTextureViewRelease(m_defaultOrmView);
    if (m_defaultOrmTexture)
    {
        wgpuTextureDestroy(m_defaultOrmTexture);
        wgpuTextureRelease(m_defaultOrmTexture);
    }

    ReleaseDepthTexture();
    if (m_sampler)
        wgpuSamplerRelease(m_sampler);
    if (m_samplerNearest)
        wgpuSamplerRelease(m_samplerNearest);
    if (m_shadowSamplerCompare)
        wgpuSamplerRelease(m_shadowSamplerCompare);
    if (m_vertexBuffer)
        wgpuBufferRelease(m_vertexBuffer);
    if (m_uniformBuffer2D)
        wgpuBufferRelease(m_uniformBuffer2D);
    if (m_bindGroup2D)
        wgpuBindGroupRelease(m_bindGroup2D);
    if (m_uniformLayout)
        wgpuBindGroupLayoutRelease(m_uniformLayout);
    if (m_textureLayout)
        wgpuBindGroupLayoutRelease(m_textureLayout);
    if (m_pipeline2D)
        wgpuRenderPipelineRelease(m_pipeline2D);

    // --- PBR 3D pipeline -----------------------------------------------------
    if (m_frameUniformBuffer3D)
        wgpuBufferRelease(m_frameUniformBuffer3D);
    if (m_materialUniformBuffer3D)
        wgpuBufferRelease(m_materialUniformBuffer3D);
    if (m_frameBindGroup3D)
        wgpuBindGroupRelease(m_frameBindGroup3D);
    if (m_materialBindGroup3D)
        wgpuBindGroupRelease(m_materialBindGroup3D);
    if (m_frameLayout3D)
        wgpuBindGroupLayoutRelease(m_frameLayout3D);
    if (m_materialLayout3D)
        wgpuBindGroupLayoutRelease(m_materialLayout3D);
    if (m_materialTexLayout3D)
        wgpuBindGroupLayoutRelease(m_materialTexLayout3D);
    if (m_pipeline3D)
        wgpuRenderPipelineRelease(m_pipeline3D);

    // --- shadow pass -----------------------------------------------------------
    if (m_shadowPassUniformBuffer)
        wgpuBufferRelease(m_shadowPassUniformBuffer);
    if (m_shadowPassBindGroup)
        wgpuBindGroupRelease(m_shadowPassBindGroup);
    if (m_shadowPassLayout)
        wgpuBindGroupLayoutRelease(m_shadowPassLayout);
    if (m_shadowPipeline)
        wgpuRenderPipelineRelease(m_shadowPipeline);
    if (m_shadowMapView)
        wgpuTextureViewRelease(m_shadowMapView);
    if (m_shadowMapTexture)
    {
        wgpuTextureDestroy(m_shadowMapTexture);
        wgpuTextureRelease(m_shadowMapTexture);
    }

    // m_imageColorTexture/m_imageColorView were already released above by the
    // ReleaseTexture(slot) loop, since RenderToImage3D registers them into the
    // ordinary m_textures[] table. m_imagePipeline3D is always its own
    // dedicated object (see CreatePipelines), unlike the old m_pipeline3D alias.
    if (m_imagePipeline3D)
        wgpuRenderPipelineRelease(m_imagePipeline3D);
    if (m_imageUniformBindGroup)
        wgpuBindGroupRelease(m_imageUniformBindGroup);
    if (m_imageUniformBuffer)
        wgpuBufferRelease(m_imageUniformBuffer);
    if (m_imageVertexBuffer)
        wgpuBufferRelease(m_imageVertexBuffer);
    if (m_imageDepthView)
        wgpuTextureViewRelease(m_imageDepthView);
    if (m_imageDepthTexture)
    {
        wgpuTextureDestroy(m_imageDepthTexture);
        wgpuTextureRelease(m_imageDepthTexture);
    }

    if (m_surface)
        ReleaseSurface(m_surface);
    if (m_device)
        wgpuDeviceRelease(m_device);
    if (m_adapter)
        wgpuAdapterRelease(m_adapter);
    if (m_instance)
        wgpuInstanceRelease(m_instance);

    m_initialized = false;
}

RendererType WebGpuRenderer::GetRendererType() const { return RendererType::WebGpu; }
DrawStats WebGpuRenderer::GetLastStats() const { return m_drawLists.GetLastStats(); }
Camera3D WebGpuRenderer::GetActiveCamera3D() const { return m_drawLists.GetCamera3D(); }

// The Renderer interface exposes these as separate phases; this backend builds
// everything in Render() and submits once in EndFrame(), so they stay empty.
void WebGpuRenderer::RenderSkybox(const DrawLists& lists) { UNUSED_VAR(lists); }
void WebGpuRenderer::RenderPrimitives(DrawLists& lists) { UNUSED_VAR(lists); }
void WebGpuRenderer::RenderModels(const DrawLists& lists) { UNUSED_VAR(lists); }
