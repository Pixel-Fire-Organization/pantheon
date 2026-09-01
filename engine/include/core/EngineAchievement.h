#pragma once

#include <cstdint>

#define ACHV_MAX_ENTRIES 128
#define ACHV_RECORD_BYTES (ACHV_MAX_ENTRIES / 8)
#define ACHV_RECORD_MAGIC 0x56484341u
#define ACHV_RECORD_VERSION 1u
#define ACHV_RECORD_FILE "achievements.dat"

/// What a player has earned, as it sits on disc.
struct AchievementRecord
{
    uint32_t magic;
    uint32_t checksum;
    uint16_t version;
    uint16_t declared;
    uint8_t unlocked[ACHV_RECORD_BYTES];
};

/// Presentation tier of a declared achievement. A set has at most one platinum,
/// earned by earning everything else.
enum class AchievementGrade : uint8_t
{
    Platinum = 0,
    Gold,
    Silver,
    Bronze
};

/// @return How many achievements the title declares.
uint32_t Engine_Achievement_DeclaredCount();

/// @param id Achievement identifier.
/// @return Its display name, or an empty string when the identifier is unknown.
const char* Engine_Achievement_GetName(uint32_t id);

/// @param id Achievement identifier.
/// @return How it is earned, or an empty string.
const char* Engine_Achievement_GetDetail(uint32_t id);

/// @param id Achievement identifier.
/// @return Its tier; the lowest tier for an unknown identifier, since a caller
///         drawing one is better served by something than by nothing.
AchievementGrade Engine_Achievement_GetGrade(uint32_t id);

/// @param id Achievement identifier.
/// @return Whether it is concealed until earned.
bool Engine_Achievement_IsHidden(uint32_t id);

/// Bring the Achievement subsystem up.
/// @param commId Communication id, or null to use the one the platform was built with.
/// @return True, including on platforms with no achievement support.
bool Engine_Achievement_Init(const char* commId);

void Engine_Achievement_Close();

/// @return Whether an unlock will be recorded. The engine keeps the record, so
///         this does not depend on the platform having achievements of its own.
bool Engine_Achievement_IsAvailable();

/// @return Whether unlocks are also being sent to the platform's own
///         achievement system. False is ordinary: most platforms have none.
bool Engine_Achievement_IsMirrored();

/// @return Why unlocks are not reaching the platform's own system, or null when
///         they are. Never a reason the engine's own record failed.
const char* Engine_Achievement_GetMirrorReason();

/// @return Whether the record survives the session. False when the platform has
///         nowhere to write, in which case unlocks still work until power-off.
bool Engine_Achievement_IsPersistent();

/// Advance whatever the platform could not finish while binding, now that
/// frames are running. Called once per frame until it answers false.
/// @return True while binding is still settling, so a caller that must wait for
///         a settled answer knows not to read one yet.
bool Engine_Achievement_PumpStartup();

/// Record an achievement as earned. Idempotent, one-way, and safe on every platform.
/// @param id Achievement identifier.
/// @return Whether the platform accepted it.
bool Engine_Achievement_Unlock(uint32_t id);

/// @param id Achievement identifier.
/// @return False when not unlocked, unknown, unavailable, or not loaded.
bool Engine_Achievement_IsUnlocked(uint32_t id);

/// @return Achievements this title declares, whether or not any can be recorded.
uint32_t Engine_Achievement_GetCount();

/// @return How many achievements the platform itself believes this title has.
///         Short of the declared count means the platform never took delivery
///         of the set, which is a different fault from being unable to record.
int32_t Engine_Achievement_GetConsoleCount();

/// Show the achievements screen. The caller decides when: the engine binds no
/// input to it, so a key it claimed would be one a game could not use.
void Engine_Achievement_OpenScreen();

void Engine_Achievement_CloseScreen();

/// @return Whether the screen is being drawn.
bool Engine_Achievement_IsScreenOpen();

/// Draw whatever the subsystem is showing: an unlock the player has not seen
/// yet, and the screen when it is open. Does nothing when neither applies.
/// @param dt Seconds since the previous frame.
void Engine_Achievement_Update(float dt);
