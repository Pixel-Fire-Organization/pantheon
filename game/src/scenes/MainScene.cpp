#include <cmath>
#include <cstdio>

#include "../../include/scenes/MainScene.h"
#include "EcsComponents.h"
#include "GameAPI.h"

MainScene* MainScene::m_instance = nullptr;

namespace
{
    struct OverlayColor
    {
        int r;
        int g;
        int b;
    };

    const OverlayColor COLOR_LOD0_READY = {70, 255, 70};
    const OverlayColor COLOR_LOD0_LOADING = {255, 220, 40};
    const OverlayColor COLOR_LOD1_READY = {60, 130, 255};
    const OverlayColor COLOR_LOD1_LOADING = {60, 230, 230};
    const OverlayColor COLOR_MISSING = {255, 50, 50};
    const OverlayColor COLOR_CENTRE = {255, 70, 255};
    const OverlayColor COLOR_PLAYER = {255, 255, 255};

    const int CELL_TIERS = game::CELL_LOD0_LOADING | game::CELL_LOD0_READY | game::CELL_LOD1_LOADING | game::CELL_LOD1_READY;

    void DrawRectOutline(float minX, float minZ, float maxX, float maxZ, float width, float y, float height, const OverlayColor& color)
    {
        const float sizeX = maxX - minX;
        const float sizeZ = maxZ - minZ;
        const float midX = (minX + maxX) * 0.5f;
        const float midZ = (minZ + maxZ) * 0.5f;
        game::DrawBox(midX, y, minZ, sizeX, height, width, color.r, color.g, color.b);
        game::DrawBox(midX, y, maxZ, sizeX, height, width, color.r, color.g, color.b);
        game::DrawBox(minX, y, midZ, width, height, sizeZ, color.r, color.g, color.b);
        game::DrawBox(maxX, y, midZ, width, height, sizeZ, color.r, color.g, color.b);
    }
} // namespace

const MainScene* MainScene::GetInstance()
{
    if (!m_instance)
    {
        m_instance = new MainScene();
    }

    return m_instance;
}

const char* MainScene::GetLevelName() const { return LEVEL_NAME; }

game::SpawnHandler MainScene::GetSpawnHandler() const { return &Ecs_SpawnDispatch; }

const game::SceneResource* MainScene::GetResources(int* outCount) const
{
    static const game::SceneResource kResources[] = {
        {"TEXTURE", BOX_TEXTURE_PATH},
    };
    *outCount = 1;
    return kResources;
}

void MainScene::OnLevelStart()
{
    m_playerX = 0.0f;
    m_playerY = 0.0f;
    m_playerZ = 0.0f;
    m_yaw = 0.0f;
    m_pitch = 0.4f;
    m_texture = game::LoadResource("TEXTURE", game::MakePath(BOX_TEXTURE_PATH));
}

void MainScene::GetStreamingCenter(float* outX, float* outZ) const
{
    *outX = m_playerX;
    *outZ = m_playerZ;
}

void MainScene::DrawStreamingOverlay() const
{
    game::LevelGrid grid;
    if (!game::GetLevelGrid(&grid))
        return;

    int lod0Ready = 0;
    int lod0Loading = 0;
    int lod1Ready = 0;
    int lod1Loading = 0;
    int missing = 0;

    for (int dz = -OVERLAY_RADIUS_CELLS; dz <= OVERLAY_RADIUS_CELLS; ++dz)
    {
        for (int dx = -OVERLAY_RADIUS_CELLS; dx <= OVERLAY_RADIUS_CELLS; ++dx)
        {
            const int cx = grid.centreCellX + dx;
            const int cz = grid.centreCellZ + dz;
            if (cx < 0 || cz < 0 || cx >= grid.cellsX || cz >= grid.cellsZ)
                continue;

            const int mask = game::GetCellResidency(cx, cz);
            if (!(mask & game::CELL_GEOMETRY))
                continue;

            const float minX = grid.originX + static_cast<float>(cx) * grid.cellSize;
            const float minZ = grid.originZ + static_cast<float>(cz) * grid.cellSize;
            const float maxX = minX + grid.cellSize;
            const float maxZ = minZ + grid.cellSize;

            if (!(mask & CELL_TIERS))
            {
                ++missing;
                DrawRectOutline(minX + OVERLAY_INSET_MISSING, minZ + OVERLAY_INSET_MISSING, maxX - OVERLAY_INSET_MISSING, maxZ - OVERLAY_INSET_MISSING, OVERLAY_LINE_WIDTH, OVERLAY_LINE_Y,
                                OVERLAY_LINE_HEIGHT, COLOR_MISSING);
                continue;
            }

            if (mask & (game::CELL_LOD0_READY | game::CELL_LOD0_LOADING))
            {
                const bool ready = (mask & game::CELL_LOD0_READY) != 0;
                if (ready)
                    ++lod0Ready;
                else
                    ++lod0Loading;
                DrawRectOutline(minX + OVERLAY_INSET_LOD0, minZ + OVERLAY_INSET_LOD0, maxX - OVERLAY_INSET_LOD0, maxZ - OVERLAY_INSET_LOD0, OVERLAY_LINE_WIDTH, OVERLAY_LINE_Y, OVERLAY_LINE_HEIGHT,
                                ready ? COLOR_LOD0_READY : COLOR_LOD0_LOADING);
            }
            if (mask & (game::CELL_LOD1_READY | game::CELL_LOD1_LOADING))
            {
                const bool ready = (mask & game::CELL_LOD1_READY) != 0;
                if (ready)
                    ++lod1Ready;
                else
                    ++lod1Loading;
                DrawRectOutline(minX + OVERLAY_INSET_LOD1, minZ + OVERLAY_INSET_LOD1, maxX - OVERLAY_INSET_LOD1, maxZ - OVERLAY_INSET_LOD1, OVERLAY_LINE_WIDTH, OVERLAY_LINE_Y, OVERLAY_LINE_HEIGHT,
                                ready ? COLOR_LOD1_READY : COLOR_LOD1_LOADING);
            }
        }
    }

    const float centreMinX = grid.originX + static_cast<float>(grid.centreCellX) * grid.cellSize;
    const float centreMinZ = grid.originZ + static_cast<float>(grid.centreCellZ) * grid.cellSize;
    DrawRectOutline(centreMinX + OVERLAY_INSET_CENTRE, centreMinZ + OVERLAY_INSET_CENTRE, centreMinX + grid.cellSize - OVERLAY_INSET_CENTRE, centreMinZ + grid.cellSize - OVERLAY_INSET_CENTRE,
                    OVERLAY_LINE_WIDTH, OVERLAY_LINE_Y, OVERLAY_LINE_HEIGHT, COLOR_CENTRE);

    const int playerCellX = static_cast<int>(std::floor((m_playerX - grid.originX) / grid.cellSize));
    const int playerCellZ = static_cast<int>(std::floor((m_playerZ - grid.originZ) / grid.cellSize));
    if (playerCellX >= 0 && playerCellZ >= 0 && playerCellX < grid.cellsX && playerCellZ < grid.cellsZ)
    {
        const float playerMinX = grid.originX + static_cast<float>(playerCellX) * grid.cellSize;
        const float playerMinZ = grid.originZ + static_cast<float>(playerCellZ) * grid.cellSize;
        DrawRectOutline(playerMinX + OVERLAY_INSET_PLAYER, playerMinZ + OVERLAY_INSET_PLAYER, playerMinX + grid.cellSize - OVERLAY_INSET_PLAYER, playerMinZ + grid.cellSize - OVERLAY_INSET_PLAYER,
                        OVERLAY_PLAYER_LINE_WIDTH, OVERLAY_LINE_Y, OVERLAY_LINE_HEIGHT, COLOR_PLAYER);
    }

    const int rowHeight = game::TextHeight() + HUD_ROW_GAP;
    const int swatchOffset = (rowHeight - HUD_ROW_GAP - HUD_SWATCH) / 2;
    const int textX = HUD_MARGIN + HUD_SWATCH + HUD_ROW_GAP;
    char line[64];
    int y = HUD_MARGIN;

    std::snprintf(line, sizeof(line), "LOD0  %d ready  %d loading", lod0Ready, lod0Loading);
    game::DrawRect(HUD_MARGIN, y + swatchOffset, HUD_SWATCH, HUD_SWATCH, COLOR_LOD0_READY.r, COLOR_LOD0_READY.g, COLOR_LOD0_READY.b);
    game::Text(textX, y, line);
    y += rowHeight;

    std::snprintf(line, sizeof(line), "LOD1  %d ready  %d loading", lod1Ready, lod1Loading);
    game::DrawRect(HUD_MARGIN, y + swatchOffset, HUD_SWATCH, HUD_SWATCH, COLOR_LOD1_READY.r, COLOR_LOD1_READY.g, COLOR_LOD1_READY.b);
    game::Text(textX, y, line);
    y += rowHeight;

    std::snprintf(line, sizeof(line), "NO GEOMETRY  %d cells", missing);
    game::DrawRect(HUD_MARGIN, y + swatchOffset, HUD_SWATCH, HUD_SWATCH, COLOR_MISSING.r, COLOR_MISSING.g, COLOR_MISSING.b);
    game::Text(textX, y, line);
    y += rowHeight;

    std::snprintf(line, sizeof(line), "ring (%d, %d)  player (%d, %d)", grid.centreCellX, grid.centreCellZ, playerCellX, playerCellZ);
    game::DrawRect(HUD_MARGIN, y + swatchOffset, HUD_SWATCH, HUD_SWATCH, COLOR_CENTRE.r, COLOR_CENTRE.g, COLOR_CENTRE.b);
    game::Text(textX, y, line);
}

void MainScene::OnLevelUpdate(float dt)
{
    MovePlayer(dt);
    MoveCamera(dt);

    game::Clear(20, 20, 26);
    game::DrawGrid(GRID_SLICES, GRID_SPACING);

    DrawStreamingOverlay();

    if (m_texture >= 0 && game::IsResourceReady(m_texture))
        game::DrawCubeTextured(m_playerX, m_playerY, m_playerZ, PLAYER_SIZE, m_texture);
    else
        game::DrawCube(m_playerX, m_playerY, m_playerZ, PLAYER_SIZE, 220, 60, 60);
}

void MainScene::MovePlayer(float dt)
{
    float moveX = 0.0f;
    float moveZ = 0.0f;
    game::GetJoyAxis(0, "left", &moveX, &moveZ);
    m_playerX += moveX * MOVE_SPEED * dt;
    m_playerZ += moveZ * MOVE_SPEED * dt;

    if (game::IsPadPressed(0, "l1"))
        m_playerY += LIFT_SPEED * dt;
    if (game::IsPadPressed(0, "l2"))
        m_playerY -= LIFT_SPEED * dt;
    if (game::IsPadPressed(0, "tri"))
        game::ReloadScene();
}

void MainScene::MoveCamera(float dt)
{
    float turnX = 0.0f;
    float turnY = 0.0f;
    game::GetJoyAxis(0, "right", &turnX, &turnY);
    m_yaw += turnX * CAMERA_SPEED * dt;
    m_pitch += turnY * CAMERA_SPEED * dt;
    if (m_pitch > PITCH_MAX)
        m_pitch = PITCH_MAX;
    if (m_pitch < PITCH_MIN)
        m_pitch = PITCH_MIN;

    const float flat = cosf(m_pitch);
    game::SetCamera3D(m_playerX + CAMERA_DISTANCE * flat * sinf(m_yaw), m_playerY + CAMERA_DISTANCE * sinf(m_pitch), m_playerZ + CAMERA_DISTANCE * flat * cosf(m_yaw), m_playerX, m_playerY, m_playerZ,
                      45.0f);
}
