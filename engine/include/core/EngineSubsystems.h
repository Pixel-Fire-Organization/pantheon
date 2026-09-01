#pragma once

#include <cstdint>

enum class EngineSubsystem : uint8_t
{
    Io = 0, // asynchronous reads
    Archive, // container mounting and resolution   (needs Io)
    Resource, // asset lifetime and handles          (needs Io)
    Level, // world core, materials, entities     (needs Resource, Archive)
    Sector, // streamed world geometry             (needs Level)
    Input, // gamepad, keyboard, mouse
    Action, // player intent, resolved from Input by a declared map (needs Input)
    Ui, // immediate-mode interface        (needs Input)
    Achievement, // player-visible achievements, where the platform has them
    PerfLogger, // on-demand performance snapshot
    Scene, // one game::Scene active at a time
    Testbed, // debug scene catalogue          (needs Ui; debug builds only)
    Count
};

// Record the requested set. Called once, before Engine_Init.
void Engine_Subsystems_Set(const EngineSubsystem* list, uint32_t count);

// True when the subsystem was requested AND its dependencies were satisfied.
bool Engine_Subsystem_IsEnabled(EngineSubsystem subsystem);

// Turn one subsystem on regardless of what the game asked for. Only the
// testbed uses this, because it must not need the game's cooperation to
// exist. Call between Engine_Subsystems_Set and Engine_Subsystems_Validate.
void Engine_Subsystem_Enable(EngineSubsystem subsystem);

const char* Engine_Subsystem_Name(EngineSubsystem subsystem);

// Verify every requested subsystem has its dependencies. Panics naming both
// sides when one is missing: asking for assets with no way to read them is a
// configuration mistake, and failing later would report it far from its cause.
void Engine_Subsystems_Validate();

// The set used when a game expresses no preference: everything.
const EngineSubsystem* Engine_Subsystems_Default(uint32_t* outCount);
