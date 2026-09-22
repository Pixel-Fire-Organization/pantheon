#pragma once

#include "core/EngineCore.h"
#include "graphics/Renderer.h"
#include "graphics/StagedGeometry.h"

#include <webgpu/webgpu.h>

// Resident GPU textures. Handles are index+1 so 0 stays "invalid" to every
// caller, matching what the PS2 backends promise.
#define WGPU_MAX_TEXTURES 256

// Cached per-material (albedo+normal+ORM) bind groups for the PBR pass -- see
// MaterialGroupFor. Bounded the same way the texture registry is.
#define WGPU_MAX_MATERIAL_GROUPS 256

// wgpu's guaranteed minimum for a dynamic uniform offset (WebGPU spec default
// limits.minUniformBufferOffsetAlignment); the per-run material buffer is
// written once per frame at this stride and indexed with a dynamic offset per
// draw run, since wgpuQueueWriteBuffer cannot be interleaved with draw calls
// inside an already-encoded render pass.
#define WGPU_MATERIAL_UNIFORM_STRIDE 256

class WebGpuRenderer final : public Renderer
{
public:
    WebGpuRenderer() = delete;
    explicit WebGpuRenderer(const EngineConfig& config);
    ~WebGpuRenderer() override = default;

    WebGpuRenderer(const WebGpuRenderer&) = delete;
    WebGpuRenderer(WebGpuRenderer&&) = delete;
    WebGpuRenderer& operator=(const WebGpuRenderer&) = delete;
    WebGpuRenderer& operator=(WebGpuRenderer&&) = delete;

    RendererType GetRendererType() const override;

    void AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale) override;
    void AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, Color3 color) override;
    void AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, int32_t textureId) override;
    void AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, Color3 color, int32_t textureId) override;
    void AddLevelToDrawList(const Level& level) override;
    void AddModelToDrawList(int32_t modelId, const Vector3& position, const Vector3& rotation, const Vector3& scale) override;
    void AddSkyToDrawList(int32_t resourceId) override;
    void ClearDrawLists() override;

    void Render() override;
    void BeginFrame() override;
    void EndFrame() override;
    void ClearFrame(const Color3& color) override;
    void DrawQuad2D(const Quad2D& quad) override;
    void DrawGrid(int32_t slices, float spacing) override;

    void SetCamera3D(CameraID id, const Camera3D& camera) override;
    void SetActiveCamera3D(CameraID id) override;
    void SetActiveCamera2D(const Camera2D& camera) override;

    void SetLight3D(LightID id, const Light3D& light) override;
    void SetAmbientLight(const Color3& color) override;
    void SetShadowCasterLight(LightID id) override;
    bool SupportsPbrShading() const override { return true; }

    uint32_t UploadTexture(const TextureUpload& upload) override;
    void ReleaseTexture(uint32_t handle) override;

    uint32_t RenderToImage3D(const Renderable3D& what, const Camera3D& camera, int width, int height, const Color3& clearColor) override;

    bool IsInitialized() const override;
    void Shutdown() override;

    DrawStats GetLastStats() const override;
    Camera3D GetActiveCamera3D() const override;

protected:
    void RenderSkybox(const DrawLists& lists) override;
    void RenderPrimitives(DrawLists& lists) override;
    void RenderModels(const DrawLists& lists) override;
    void RenderShadowMap(const DrawLists& lists) override;

private:
    // Matches the WGSL uniform block: mat4x4 is 64 bytes and the struct must
    // be a multiple of 16. Used by the flat/unlit shader shared between the 2D
    // pass and RenderToImage3D's preview pipeline -- see kShaderSourceFlat.
    struct Uniforms
    {
        float viewProj[16];
    };

    // Group 0 of the PBR 3D pass: everything constant for the whole frame
    // (camera, lights, ambient, the shadow-caster's light-space matrix).
    // Field order/padding matches FrameUniforms in kShaderSource3D exactly.
    struct GpuLight
    {
        float positionOrDir[4]; // xyz, w: 0 = directional, 1 = point
        float colorIntensity[4]; // rgb, intensity (<=0 means the slot is off)
        float rangeParams[4]; // x = range (point lights), yzw unused/padding
    };

    struct FrameUniforms3D
    {
        float viewProj[16];
        float lightViewProj[16];
        float cameraPos[4];
        float ambient[4];
        GpuLight lights[GFX_MAX_LIGHTS];
        float shadowCaster[4]; // x = active light index as a float, -1 = none
    };

    // Group 1 of the PBR 3D pass: one per-run material, read through a
    // dynamic uniform offset (see WGPU_MATERIAL_UNIFORM_STRIDE).
    struct MaterialUniformGpu
    {
        float baseColor[4];
        float emissive[4]; // rgb, unused w
        float metallicRoughnessNormalAlpha[4]; // metallic, roughness, normalScale, alphaCutoff
        float matFlags[4]; // x = alpha-mask enabled (0/1), yzw unused/padding
    };

    // Group 0 of the depth-only shadow pass: just the caster's light-space matrix.
    struct ShadowUniforms
    {
        float lightViewProj[16];
    };

    struct TextureEntry
    {
        WGPUTexture texture;
        WGPUTextureView view;
        WGPUBindGroup bindGroup;
    };

    // Cached bind group for one resolved (albedo, normal, orm) texture-handle
    // triple -- see MaterialGroupFor. Built lazily the first time a combination
    // is seen and reused after, so the PBR pass never creates a WebGPU object
    // on the per-draw-call path.
    struct MaterialGroupEntry
    {
        uint32_t albedo;
        uint32_t normal;
        uint32_t orm;
        WGPUBindGroup group;
    };

    bool InitDevice();
    bool CreatePipelines();
    bool CreatePbrPipeline();
    bool CreateShadowPipeline();
    bool CreateWhiteTexture();
    bool CreateDefaultMaterialTextures();
    bool ConfigureSurface(uint32_t width, uint32_t height);
    bool EnsureDepthTexture(uint32_t width, uint32_t height);
    void ReleaseDepthTexture();
    bool EnsureShadowMap();

    WGPUBindGroup BindGroupFor(uint32_t handle) const;

    /// Resolve (or lazily build and cache) the group-2 bind group for one
    /// material's three texture slots. A slot of 0 samples the matching
    /// default (white albedo, flat normal, or occlusion=1/roughness=1/
    /// metallic=0 ORM) rather than being left unbound.
    WGPUBindGroup MaterialGroupFor(uint32_t albedo, uint32_t normal, uint32_t orm);

    /// Drop any cached MaterialGroupEntry that references `handle` in any of
    /// its three slots, releasing its bind group first. Called from
    /// ReleaseTexture: a cached group holding a destroyed WGPUTextureView
    /// would sample an invalid resource, so eviction must be eager, not
    /// merely stale-tolerant.
    void PurgeMaterialGroupsReferencing(uint32_t handle);

    /// Write the whole per-run material array for this frame's PBR pass in
    /// one call, before any render pass begins -- wgpuQueueWriteBuffer writes
    /// cannot be interleaved with draw calls inside an open pass, so every
    /// run's material data is uploaded up front and selected per-draw with a
    /// dynamic offset instead.
    void UploadMaterialUniforms(const StagedGeometry::DrawRun* runs, uint32_t count);

    // --- wgpu objects -------------------------------------------------------
    WGPUInstance m_instance;
    WGPUAdapter m_adapter;
    WGPUDevice m_device;
    WGPUQueue m_queue;
    WGPUSurface m_surface;
    WGPUTextureFormat m_surfaceFormat;

    // --- Flat/unlit pipeline: shared by the 2D pass and RenderToImage3D's
    // preview target. Thumbnails intentionally stay on the cheap flat-headlight
    // shader rather than the full PBR pass below, so a preview's lighting never
    // depends on -- and never goes dark relative to -- the scene's own dynamic
    // lights/shadow caster. See docs/subsystems/RENDERER.md.
    WGPURenderPipeline m_pipeline2D;
    WGPUBindGroupLayout m_uniformLayout;
    WGPUBindGroupLayout m_textureLayout;
    WGPUBindGroup m_bindGroup2D;
    WGPUBuffer m_uniformBuffer2D;
    WGPUSampler m_sampler;
    WGPUSampler m_samplerNearest;

    // --- PBR 3D pipeline (main scene) ---
    WGPURenderPipeline m_pipeline3D;
    WGPUBindGroupLayout m_frameLayout3D; // group 0: frame uniforms + shadow map
    WGPUBindGroupLayout m_materialLayout3D; // group 1: dynamic-offset per-run material
    WGPUBindGroupLayout m_materialTexLayout3D; // group 2: albedo/normal/orm + sampler
    WGPUBindGroup m_frameBindGroup3D;
    WGPUBindGroup m_materialBindGroup3D; // bound with a dynamic offset per draw run
    WGPUBuffer m_frameUniformBuffer3D;
    WGPUBuffer m_materialUniformBuffer3D; // GFX_MAX_DRAW_RUNS * WGPU_MATERIAL_UNIFORM_STRIDE bytes

    MaterialGroupEntry m_materialGroups[WGPU_MAX_MATERIAL_GROUPS];
    uint32_t m_materialGroupCount;

    // --- Shadow pass: dynamic geometry only, one caster, depth-only ---
    WGPURenderPipeline m_shadowPipeline;
    WGPUBindGroupLayout m_shadowPassLayout;
    WGPUBindGroup m_shadowPassBindGroup;
    WGPUBuffer m_shadowPassUniformBuffer;
    WGPUTexture m_shadowMapTexture;
    WGPUTextureView m_shadowMapView;
    WGPUSampler m_shadowSamplerCompare;

    // Result of this frame's RenderShadowMap, consumed when EndFrame builds
    // FrameUniforms3D for the main pass right afterwards.
    float m_lastLightViewProj[16];
    bool m_shadowActive;
    LightID m_shadowCasterIndex;

    WGPUBuffer m_vertexBuffer;
    uint64_t m_vertexBufferCapacity;

    WGPUTexture m_depthTexture;
    WGPUTextureView m_depthView;

    // Sampled by untextured geometry, so one pipeline serves both cases rather
    // than two that differ only in whether a texture is bound. Also the PBR
    // pass's default albedo.
    TextureEntry m_whiteTexture;
    TextureEntry m_textures[WGPU_MAX_TEXTURES];

    // PBR pass defaults for a material with no normal/ORM map: flat tangent-
    // space normal (128,128,255), and occlusion=1/roughness=1/metallic=0.
    WGPUTexture m_defaultNormalTexture;
    WGPUTextureView m_defaultNormalView;
    WGPUTexture m_defaultOrmTexture;
    WGPUTextureView m_defaultOrmView;

    // Geometry staging is shared with the OpenGL backend: the transform and
    // texture batching are identical, only the upload differs.
    StagedGeometry m_geometry;

    Color3 m_clearColor;
    uint32_t m_width;
    uint32_t m_height;

    DrawStats m_frameStats;
    bool m_initialized;

    // --- RenderToImage3D (Ui_Image3D) scratch state ---------------------
    // A separate StagedGeometry so staging one preview object never discards
    // whatever the ordinary per-frame path (m_geometry) has already built,
    // and dedicated uniform/vertex buffers and bind group rather than the
    // main pass's m_uniformBuffer3D/m_bindGroup3D/m_vertexBuffer: those are
    // rewritten later this same frame by EndFrame(), and while WebGPU orders
    // a queue's writes and submissions against each other correctly, a
    // dedicated set removes any need to reason about that ordering at all.
    // Registered into the ordinary m_textures[] table (see UploadTexture) so
    // the interface's own 2D draw path resolves it exactly like any uploaded
    // texture, through BindGroupFor -- no special-casing there.
    StagedGeometry m_imageGeometry;
    WGPURenderPipeline m_imagePipeline3D; // RGBA8Unorm variant; only built if m_surfaceFormat differs
    WGPUBuffer m_imageUniformBuffer;
    WGPUBindGroup m_imageUniformBindGroup;
    WGPUBuffer m_imageVertexBuffer;
    uint64_t m_imageVertexBufferCapacity;
    WGPUTexture m_imageColorTexture;
    WGPUTextureView m_imageColorView;
    WGPUTexture m_imageDepthTexture;
    WGPUTextureView m_imageDepthView;
    int m_imageWidth;
    int m_imageHeight;
    int m_imageTextureSlot; // index into m_textures[], or -1 before first use

    /// (Re)allocate the scratch colour+depth target and its m_textures[] slot
    /// at this size if it does not already match.
    /// @return False when allocation failed; RenderToImage3D answers 0.
    bool EnsureImageTarget(int width, int height);
};
