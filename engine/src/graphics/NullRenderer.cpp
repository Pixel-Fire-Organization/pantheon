#include "graphics/NullRenderer.h"

#include "../../include/core/EngineDebug.h"
#include "Macros.h"

NullRenderer::NullRenderer() : m_nextTextureHandle(1), m_initialized(true) { Engine_LogInfo("NullRenderer: headless renderer active - nothing will be drawn."); }

RendererType NullRenderer::GetRendererType() const { return RendererType::Null; }

// --- Draw-list submission ---------------------------------------------------
// Submissions are still recorded, so the perf snapshot shows real counts and a
// platform can be validated end to end without a GPU.

void NullRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale)
{
    AddPrimitiveToDrawList(primitive, position, rotation, scale, Color3{1.0f, 1.0f, 1.0f}, -1);
}

void NullRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, Color3 color)
{
    AddPrimitiveToDrawList(primitive, position, rotation, scale, color, -1);
}

void NullRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, int32_t textureId)
{
    AddPrimitiveToDrawList(primitive, position, rotation, scale, Color3{1.0f, 1.0f, 1.0f}, textureId);
}

void NullRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, Color3 color, int32_t textureId)
{
    PrimitiveDrawEntry entry;
    entry.transform = Transform3D(position, rotation, scale);
    entry.color = color;
    entry.type = primitive;
    entry.textureId = textureId;
    m_drawLists.AddPrimitive(entry);
}

void NullRenderer::AddLevelToDrawList(const Level& level) { UNUSED_VAR(level); }

void NullRenderer::AddModelToDrawList(int32_t modelId, const Vector3& position, const Vector3& rotation, const Vector3& scale)
{
    ModelDrawEntry entry;
    entry.resourceId = modelId;
    entry.transform = Transform3D(position, rotation, scale);
    m_drawLists.AddModel(entry);
}

void NullRenderer::AddSkyToDrawList(int32_t resourceId) { m_drawLists.SetSkyboxTexture(resourceId); }

void NullRenderer::ClearDrawLists() { m_drawLists.Reset(false); }

// --- Frame ------------------------------------------------------------------

void NullRenderer::Render()
{
    DrawStats stats{};
    stats.primitiveCount = static_cast<uint16_t>(m_drawLists.GetUntexturedCount() + m_drawLists.GetTexturedCount());
    stats.modelCount = m_drawLists.GetModelCount();
    m_drawLists.SetLastStats(stats);
}

void NullRenderer::BeginFrame() {}

void NullRenderer::EndFrame() { m_drawLists.Reset(false); }


void NullRenderer::ClearFrame(const Color3& color) { UNUSED_VAR(color); }

void NullRenderer::DrawQuad2D(const Quad2D& quad) { UNUSED_VAR(quad); }

void NullRenderer::DrawGrid(int32_t slices, float spacing)
{
    UNUSED_VAR(slices);
    UNUSED_VAR(spacing);
}

// --- Cameras ----------------------------------------------------------------

void NullRenderer::SetCamera3D(CameraID id, const Camera3D& camera) { m_drawLists.SetCamera3D(id, camera); }

void NullRenderer::SetActiveCamera3D(CameraID id) { m_drawLists.SetActiveCamera3D(id); }

void NullRenderer::SetActiveCamera2D(const Camera2D& camera) { m_drawLists.SetActiveCamera2D(camera); }

// --- Textures ---------------------------------------------------------------

uint32_t NullRenderer::UploadTexture(const TextureUpload& upload)
{
    UNUSED_VAR(upload);
    return m_nextTextureHandle++;
}

void NullRenderer::ReleaseTexture(uint32_t handle) { UNUSED_VAR(handle); }

// --- Lifecycle --------------------------------------------------------------

bool NullRenderer::IsInitialized() const { return m_initialized; }

void NullRenderer::Shutdown() { m_initialized = false; }

DrawStats NullRenderer::GetLastStats() const { return m_drawLists.GetLastStats(); }

Camera3D NullRenderer::GetActiveCamera3D() const { return m_drawLists.GetCamera3D(); }

void NullRenderer::RenderSkybox(const DrawLists& lists) { UNUSED_VAR(lists); }

void NullRenderer::RenderPrimitives(DrawLists& lists) { UNUSED_VAR(lists); }

void NullRenderer::RenderModels(const DrawLists& lists) { UNUSED_VAR(lists); }

