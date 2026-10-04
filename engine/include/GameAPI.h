#pragma once

#include "ecs/Entity.h"
#include "scenes/Scene.h"

// ---------------------------------------------------------------------------
// GameAPI — the friendly C++ surface for authoring gameplay.
//
// This replaces the old Lua gameplay bindings. Game code lives in C++ and runs
// natively (no interpreter in the per-frame hot loop), but stays approachable:
// every function takes plain scalars (floats / ints / const char*), so a game
// module needs no engine internals and no engine types.
//
//   * The GAME implements  GameConfigure() / GameInit() / GameUpdate(dt).
//   * The ENGINE implements everything in namespace game (game calls them).
//
// Mirrors the semantics of the retired graphics.*/input.*/resources.* bindings
// one-for-one, so porting a Lua script to C++ is mechanical.
// ---------------------------------------------------------------------------

// --- Entry points the game module must define -------------------------------
// GameConfigure() runs FIRST, before the engine or its memory exist. Choose the
//                 subsystems here; touching anything else is too early.
// GameInit()      is called once, after the engine is initialised.
// GameUpdate(dt)  is called every frame (dt = seconds since last frame). Do all
//                 per-frame gameplay + draw submission here.
struct EngineConfig;
void GameConfigure(EngineConfig* config);
void GameInit();
void GameUpdate(float dt);

namespace game
{
    // --- Time -------------------------------------------------------------------
    float GetTime(); // seconds since engine start
    float GetDeltaTime(); // seconds elapsed last frame

    // --- Lifecycle / debug ------------------------------------------------------
    void Exit(); // request engine shutdown (loop ends next check)
    void Log(const char* msg); // info log line

    // Resolve a relative asset path against the active device token
    // (e.g. "RASSETS\\BOX.PS2A" -> "cdrom0:\\RASSETS\\BOX.PS2A;1").
    // Returns a pointer to an internal static buffer — copy it if you need to keep
    // it; the next call overwrites it. Not reentrant (single-threaded game code).
    const char* MakePath(const char* relativePath);

    // --- Camera -----------------------------------------------------------------
    // Set the 3D camera pose and make it the active (rendered) camera this frame.
    // Up vector is (0,1,0) and projection is perspective. Call each frame when the
    // camera moves. Collapses the old make/update/begin_mode_3d trio into one call.
    void SetCamera3D(float posX, float posY, float posZ, float targetX, float targetY, float targetZ, float fovy);

    // --- Lights -------------------------------------------------------------------
    // Fixed dynamic-light slots, mirroring the camera-slot model. A slot with
    // intensity <= 0 is off. `x,y,z` is a direction (normalized) for a
    // directional light, or a world position for a point light; `range` is a
    // point light's attenuation distance and is ignored for directional.
    // Colour components are 0..255.
    void SetLight(int slot, bool directional, float x, float y, float z, int r, int g, int b, float intensity, float range);

    // The flat ambient term added on top of every active light's contribution.
    void SetAmbientLight(int r, int g, int b);

    // Which light slot (if any) casts the single real-time shadow map this
    // frame; a negative slot means no shadows. Only honoured on backends
    // whose shading is per-pixel PBR -- see docs/subsystems/RENDERER.md.
    void SetShadowCaster(int slot);

    // --- Frame / background -----------------------------------------------------
    void Clear(int r, int g, int b); // clear colour, components 0..255

    // --- 2D primitives (immediate mode — submit every frame) --------------------
    // Screen-space rectangle in pixels. Colour components 0..255. The building
    // block for UI/HUD authored in C++ (there is no separate UI scripting layer).
    void DrawRect(int x, int y, int width, int height, int r, int g, int b);
    // The same, with an alpha component. Goes straight to the renderer, so it is
    // neither clipped nor layered; the interface calls below are.
    void DrawRect(int x, int y, int width, int height, int r, int g, int b, int a);

    // --- Interface --------------------------------------------------------------
    // Available when the Ui subsystem is running. Absent, every call here is a
    // no-op and a query reports "not interacted with", so game code compiles and
    // runs either way.
    int ScreenWidth();
    int ScreenHeight();

    // Text drawn with whichever font the interface has: a cooked one where the
    // content pipeline supplied it, the built-in one otherwise.
    void Text(int x, int y, const char* text);
    int TextWidth(const char* text);
    int TextHeight();

    // A panel, and the rows that stack inside it. Every Panel must be closed.
    void Panel(const char* title, int x, int y, int width, int height);
    void EndPanel();
    void Label(const char* text);
    bool Button(const char* label);

    // A notification, shown in the corner for a few seconds.
    void Toast(const char* text, float seconds);

    // --- 3D primitives (immediate mode — submit every frame) --------------------
    // Colour components are 0..255.
    void DrawGrid(int slices, float spacing);
    void DrawCube(float x, float y, float z, float size, int r, int g, int b);
    /// Draw an axis-aligned box, centred on a point, with a separate extent on each axis.
    /// @param x Centre X.
    /// @param y Centre Y.
    /// @param z Centre Z.
    /// @param sizeX Extent along X.
    /// @param sizeY Extent along Y.
    /// @param sizeZ Extent along Z.
    /// @param r Red, 0 to 255.
    /// @param g Green, 0 to 255.
    /// @param b Blue, 0 to 255.
    void DrawBox(float x, float y, float z, float sizeX, float sizeY, float sizeZ, int r, int g, int b);
    void DrawSphere(float x, float y, float z, float size, int r, int g, int b);
    void DrawCylinder(float x, float y, float z, float size, int r, int g, int b);
    void DrawCubeTextured(float x, float y, float z, float size, int textureId);

    // --- Input ------------------------------------------------------------------
    // button: "x","cir","squ","tri","dpad_up/down/left/right","l1","l2","r1","r2",
    //         "l3","r3","start","select".
    bool IsPadPressed(int pad, const char* button);
    // side: "left" or "right". Writes analog stick axes in [-1,+1] (deadzone
    // applied C-side). Writes 0,0 on any error.
    void GetJoyAxis(int pad, const char* side, float* outX, float* outY);

    // Rising edge: true only on the frame the button went down. Saves every
    // caller keeping its own "was held" flag.
    bool WasPadPressed(int pad, const char* button);

    // --- Keyboard ---------------------------------------------------------------
    // key: "a".."z", "0".."9", "f1".."f12", "up","down","left","right", "space",
    //      "enter", "escape", "tab", "backspace", "shift", "ctrl", "alt", and the
    //      punctuation names in PlatformKeys.h.
    //
    // Always false on a platform with no keyboard (the PS2), so code using these
    // still compiles and runs everywhere. Use HasInputDevice("keyboard") to branch
    // on presence rather than testing the platform name.
    bool IsKeyDown(const char* key);
    bool WasKeyPressed(const char* key);

    // --- Mouse ------------------------------------------------------------------
    // button: 0 = left, 1 = right, 2 = middle, 3/4 = extra.
    // Position is in client pixels, origin top-left. Zero on a platform with no
    // mouse.
    bool IsMouseButtonDown(int button);
    bool WasMouseButtonPressed(int button);
    void GetMousePosition(float* outX, float* outY);
    void GetMouseDelta(float* outX, float* outY);
    float GetMouseWheel();

    /// @param surface "front" or "rear".
    /// @return Live contacts; zero on a platform without touch.
    int GetTouchCount(const char* surface);

    /// @param surface "front" or "rear".
    /// @param index Contact index below GetTouchCount.
    /// @param outX Receives the x position, normalised to [0,1].
    /// @param outY Receives the y position, normalised to [0,1].
    /// @return False when index is past the count.
    bool GetTouch(const char* surface, int index, float* outX, float* outY);

    /// @param device "gamepad", "keyboard", "mouse" or "touch".
    /// @return Whether the running platform provides it.
    bool HasInputDevice(const char* device);

    /// Record an achievement as earned. Idempotent, and safe on every platform.
    /// @param id Identifier from the generated achievement header.
    /// @return Whether it was recorded.
    bool UnlockAchievement(int id);

    /// @param id Identifier from the generated achievement header.
    /// @return False when not unlocked, or unavailable.
    bool IsAchievementUnlocked(int id);

    /// @return Whether achievements can actually be recorded here.
    bool HasAchievements();

    /// Show the achievements screen. The game decides when and from where; the
    /// engine binds no button to it, so none is taken away from the game.
    void StartAchievementsUI();

    void StopAchievementsUI();

    /// @return Whether the screen is currently being drawn.
    bool IsAchievementsUIOpen();

    // --- Actions ------------------------------------------------------------
    // Player intent, resolved once per frame from Input's snapshot against the
    // title's declared action map (game/config/actions.json). See
    // docs/subsystems/ACTION.md and docs/APP_API.md.
    //
    // id / context: identifiers from the header generated at build time from
    // the declaration (tools/actions.py --emit-ids), the same precedent as
    // AchievementId.

    // Digital.
    bool ActionHeld(int id);
    bool ActionPressed(int id); // rising edge, consumes
    bool ActionReleased(int id); // falling edge, consumes
    int ActionPressedCount(int id); // transitions since last call, consumes
    bool ActionRepeatTick(int id); // synthetic press from the action's declared repeat timer

    // Analog.
    float ActionAxis1d(int id); // [-1, 1]
    void ActionAxis2d(int id, float* outX, float* outY); // shaped, on top of the platform's own deadzone
    float ActionScalar(int id); // [0, 1]

    // Prompts, for drawing a button glyph. device is "gamepad"/"keyboard"/
    // "mouse"/"touch"; source is the same name a source string in
    // game/config/actions.json uses after its device prefix (e.g.
    // "gamepad.stick_left" names device "gamepad", source "stick_left") --
    // deliberately not IsPadPressed's older, abbreviated button grammar
    // ("x"/"cir"), so a prompt and the declaration that produced it read the
    // same way. False when the action has no live binding to draw.
    bool GetActionPrompt(int id, const char** outDevice, const char** outSource);

    // Rebinding. bindingIndex addresses one of the action's live candidates
    // (0 for the common single-candidate case); sourceSlot one source within
    // it. device/source use GetActionPrompt's grammar.
    bool RebindAction(int id, int bindingIndex, int sourceSlot, const char* device, const char* source);
    void RestoreActionDefault(int id);
    bool SaveActionOverlay();

    // --- Action contexts ------------------------------------------------------
    // Pushing an unknown or already-active context is refused and reported.
    bool PushActionContext(int context);
    void PopActionContext();

    // --- Resources (async streaming; poll IsResourceReady) ----------------------
    // type: "TEXTURE","MODEL","SOUND","FONT". Returns a handle >= 0, or -1.
    int LoadResource(const char* type, const char* path);
    bool IsResourceReady(int handle);

    // --- Entities / spawning ----------------------------------------------------
    // A generic entity record produced by the level loader from compiled map data.
    // The engine knows nothing about component types: it hands the game a classname
    // plus the raw key/value properties authored in TrenchBroom and the entity's
    // origin. The game's generated Ecs_SpawnDispatch (tools/ECS/generate_ecs.py)
    // turns this into typed components. The key/value strings and props array are
    // only valid for the duration of the handler call — copy anything you keep.

    // Register the game's spawn dispatcher. Call once in GameInit(). The engine
    // invokes it for every entity found while loading a level.
    void SetSpawnHandler(SpawnHandler handler);

    // --- Levels -----------------------------------------------------------------
    // Load a compiled level by name (reads its core from the master archive and
    // spawns its entities via the registered spawn handler). Returns true on
    // success. Any previously loaded level is unloaded first.
    bool LoadLevel(const char* name);
    void UnloadLevel();

    // Set the streaming centre (world position) — the resident sector ring
    // recenters to follow it. Call each frame with the camera/player position.
    void SetStreamingCenter(float x, float y, float z);

    const int CELL_GEOMETRY = 0x01;
    const int CELL_LOD0_LOADING = 0x02;
    const int CELL_LOD0_READY = 0x04;
    const int CELL_LOD1_LOADING = 0x08;
    const int CELL_LOD1_READY = 0x10;

    /// The streaming grid of the loaded level.
    struct LevelGrid
    {
        float originX;
        float originZ;
        float cellSize;
        int cellsX;
        int cellsZ;
        int centreCellX;
        int centreCellZ;
    };

    /// Describe the loaded level's streaming grid.
    /// @param outGrid Filled with the grid and the cell the resident ring is centred on.
    /// @return False when no level is loaded or the ring has not been centred yet.
    bool GetLevelGrid(LevelGrid* outGrid);

    /// What one cell of the loaded level holds right now.
    /// @param cellX Grid cell X.
    /// @param cellZ Grid cell Z.
    /// @return A mask of the CELL_* bits: whether the cell has geometry, and whether each
    /// tier is loading or ready for it. Zero outside the grid or with no level loaded.
    int GetCellResidency(int cellX, int cellZ);

    // --- Scenes -------------------------------------------------------------
    // One thing runs at a time, and the engine — not GameUpdate — decides when
    // it starts, stops and switches. See docs/subsystems/SCENE.md.

    // Register the scene that runs first. Call once, from GameInit().
    void SetMainScene(Scene* scene);

    // Switch to a different scene. A no-op if it is already the active one —
    // use ReloadScene to force the active scene to tear down and rebuild.
    void SwitchScene(Scene* scene);

    // Tear down and restart the active scene in place. A no-op, logged, if no
    // scene is active.
    void ReloadScene();

    // True while the active scene's declared resources are still outstanding.
    bool IsSceneLoading();

    // Fraction in [0,1] of the active scene's declared resources now ready.
    // 1.0 when nothing is outstanding, including when no scene is active.
    float GetSceneLoadProgress();

} // namespace game

// --- Engine-internal (not part of the game-facing surface) ------------------
// Route a spawn record to the handler registered via game::SetSpawnHandler.
// Returns false if no handler is registered or the handler rejected the record.
// Called by the level loader (EngineLevel.cpp) when instantiating map entities.
bool Engine_Game_DispatchSpawn(const game::EntitySpawn& spawn);

// Drop the level and spawn handler the game registered, so the engine can be
// returned to a clean state without the game being involved. Called by
// Engine_ResetRuntimeState; the game re-registers both in GameInit().
void Engine_Game_ResetState();
