#include "core/EngineSubsystems.h"

#include <cstdio>

#include "core/EngineDebug.h"

namespace
{
    bool s_Enabled[static_cast<uint32_t>(EngineSubsystem::Count)] = {};

    struct Dependency
    {
        EngineSubsystem subsystem;
        EngineSubsystem requires_;
    };

    // Declared here and mirrored in each subsystem spec, so the bring-up order
    // can be reviewed against the documentation rather than inferred.
    //
    // Ui/PerfLogger/Testbed depend on Action rather than Input directly: Action
    // is the only way any of them check for input (docs/subsystems/ACTION.md).
    // This edge cannot actually fire the panic below in practice, since
    // Engine_Subsystems_Set forces both Input and Action on unconditionally --
    // it is kept because it is still the true dependency, and a future change
    // to that forcing should surface here rather than silently assume it.
    const Dependency kDependencies[] = {
        {EngineSubsystem::Archive, EngineSubsystem::Io},     {EngineSubsystem::Resource, EngineSubsystem::Io},       {EngineSubsystem::Level, EngineSubsystem::Resource},
        {EngineSubsystem::Level, EngineSubsystem::Archive},  {EngineSubsystem::Sector, EngineSubsystem::Level},      {EngineSubsystem::Action, EngineSubsystem::Input},
        {EngineSubsystem::Ui, EngineSubsystem::Action},      {EngineSubsystem::PerfLogger, EngineSubsystem::Action}, {EngineSubsystem::Testbed, EngineSubsystem::Ui},
        {EngineSubsystem::Testbed, EngineSubsystem::Action},
    };

    const uint32_t kDependencyCount = sizeof(kDependencies) / sizeof(kDependencies[0]);

    const EngineSubsystem kAll[] = {
        EngineSubsystem::Io,     EngineSubsystem::Archive, EngineSubsystem::Resource,    EngineSubsystem::Level,      EngineSubsystem::Sector, EngineSubsystem::Input,
        EngineSubsystem::Action, EngineSubsystem::Ui,      EngineSubsystem::Achievement, EngineSubsystem::PerfLogger, EngineSubsystem::Scene,
    };
} // namespace

void Engine_Subsystems_Set(const EngineSubsystem* list, uint32_t count)
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(EngineSubsystem::Count); ++i)
        s_Enabled[i] = false;

    if (list)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t index = static_cast<uint32_t>(list[i]);
            if (index < static_cast<uint32_t>(EngineSubsystem::Count))
                s_Enabled[index] = true;
        }
    }

    // Input and Action are not really selectable: Action is the only way
    // anything in the engine checks for input, so a game that left either out
    // of its own list (or omitted the list, or asked for nothing) still gets
    // them. See docs/subsystems/ACTION.md and docs/subsystems/INPUT.md.
    s_Enabled[static_cast<uint32_t>(EngineSubsystem::Input)] = true;
    s_Enabled[static_cast<uint32_t>(EngineSubsystem::Action)] = true;
}

void Engine_Subsystem_Enable(EngineSubsystem subsystem)
{
    const uint32_t index = static_cast<uint32_t>(subsystem);
    if (index < static_cast<uint32_t>(EngineSubsystem::Count))
        s_Enabled[index] = true;
}

bool Engine_Subsystem_IsEnabled(EngineSubsystem subsystem)
{
    const uint32_t index = static_cast<uint32_t>(subsystem);
    return index < static_cast<uint32_t>(EngineSubsystem::Count) && s_Enabled[index];
}

const char* Engine_Subsystem_Name(EngineSubsystem subsystem)
{
    switch (subsystem)
    {
    case EngineSubsystem::Io:
        return "io";
    case EngineSubsystem::Archive:
        return "archive";
    case EngineSubsystem::Resource:
        return "resource";
    case EngineSubsystem::Level:
        return "level";
    case EngineSubsystem::Sector:
        return "sector";
    case EngineSubsystem::Input:
        return "input";
    case EngineSubsystem::Action:
        return "action";
    case EngineSubsystem::Ui:
        return "ui";
    case EngineSubsystem::Achievement:
        return "achievement";
    case EngineSubsystem::PerfLogger:
        return "perflogger";
    case EngineSubsystem::Scene:
        return "scene";
    case EngineSubsystem::Testbed:
        return "testbed";
    case EngineSubsystem::Count:
    default:
        return "<unknown>";
    }
}

void Engine_Subsystems_Validate()
{
    for (uint32_t i = 0; i < kDependencyCount; ++i)
    {
        const Dependency& d = kDependencies[i];
        if (Engine_Subsystem_IsEnabled(d.subsystem) && !Engine_Subsystem_IsEnabled(d.requires_))
        {
            char message[160];
            snprintf(message, sizeof(message), "subsystem '%s' was requested but its dependency '%s' was not", Engine_Subsystem_Name(d.subsystem), Engine_Subsystem_Name(d.requires_));
            Engine_Panic(message);
        }
    }
}

const EngineSubsystem* Engine_Subsystems_Default(uint32_t* outCount)
{
    if (outCount)
        *outCount = sizeof(kAll) / sizeof(kAll[0]);
    return kAll;
}
