#include "core/EngineAction.h"

#include <cmath>
#include <cstring>

#include "PlatformConstants.h" // INPUT_ANALOG_DEADZONE, resolved per platform by include order
#include "core/EngineDebug.h"
#include "core/EngineInput.h"
#include "core/EngineIO.h"
#include "graphics/Types.h"
#include "platform/Platform.h"

namespace
{
    const uint32_t FNV_OFFSET_BASIS = 2166136261u;
    const uint32_t FNV_PRIME = 16777619u;

    // --- Per-frame / per-session runtime state ----------------------------------

    struct ActionState
    {
        float axisX;
        float axisY;
        float heldSeconds; // seconds the aggregate has read held, for repeat timing
        float nextRepeatAt; // heldSeconds threshold for the next synthetic tick
        uint16_t pressTransitions;
        uint16_t releaseTransitions;
        uint16_t repeatTicks;
        bool held; // this frame's aggregate, after suppression
    };

    // Extra state for a binding that is a combination, a subset of one, or a
    // composite using the lastWins opposing policy. See ACTION_MAX_DYNAMIC_BINDINGS.
    struct ActionDynamicState
    {
        bool satisfied; // this binding's own sources currently all read true
        bool eligible; // satisfied, and (not a subset, or its combination window has elapsed)
        bool claimed; // combinations only: currently holds its sources against narrower bindings
        float pendingSeconds; // combination-window countdown while deferring
        uint8_t legWinner[2]; // composite lastWins: 0 none, 1 first leg, 2 second leg, per axis
    };

    bool s_Loaded = false;
    ActionState s_State[ACTION_MAX_ENTRIES];
    ActionDynamicState s_Dynamic[ACTION_MAX_DYNAMIC_BINDINGS];

    uint8_t s_ContextStack[ACTION_CONTEXT_STACK_DEPTH];
    uint8_t s_ContextStackDepth = 0;

    ActionOverlayEntry s_OverlayEntries[ACTION_MAX_OVERLAY_ENTRIES];
    uint32_t s_OverlayCount = 0;
    bool s_OverlayDirty = false;
    bool s_OverlayPersistent = false;
    bool s_ReportedNoStorage = false;
    bool s_ReportedOverlayFull = false;

    uint32_t Fnv1a(const uint8_t* bytes, size_t count)
    {
        uint32_t hash = FNV_OFFSET_BASIS;
        for (size_t i = 0; i < count; ++i)
        {
            hash ^= bytes[i];
            hash *= FNV_PRIME;
        }
        return hash;
    }

    // --- Overlay: load / save, mirroring EngineAchievement.cpp's Load/Save -----
    // The checksum covers only the entries, per ACTION_OVERLAY.md: magic and
    // version are checked by exact match, entryCount against the compiled
    // limit, and mapDigest against the compiled map, so none of them needs a
    // checksum of its own.

    bool OverlayPath(char* outBuf, size_t bufSize)
    {
        Platform* platform = Engine_GetPlatform();
        return platform && platform->BuildWritablePath(ACTION_OVERLAY_FILE, outBuf, bufSize);
    }

    void ClearOverlay()
    {
        s_OverlayCount = 0;
        memset(s_OverlayEntries, 0, sizeof(s_OverlayEntries));
    }

    void LoadOverlay()
    {
        ClearOverlay();

        char path[IO_FILE_MAX_PATH];
        if (!OverlayPath(path, sizeof(path)))
        {
            s_OverlayPersistent = false;
            Engine_LogInfo("Action: no writable storage; rebindings are kept for this session only");
            return;
        }
        s_OverlayPersistent = true;

        Platform* platform = Engine_GetPlatform();
        FileHandle file = platform->FileOpen(path, FileMode::Read);
        if (!file)
            return;

        ActionOverlayHeader header;
        size_t read = platform->FileRead(file, &header, sizeof(header));
        if (read != sizeof(header))
        {
            platform->FileClose(file);
            Engine_LogError("Action: the overlay at '%s' is too short to be one; starting from defaults", path);
            return;
        }
        if (header.magic != ACTION_OVERLAY_MAGIC || header.version != ACTION_OVERLAY_VERSION)
        {
            platform->FileClose(file);
            Engine_LogError("Action: the overlay at '%s' is not one this build wrote; starting from defaults", path);
            return;
        }
        if (header.entryCount > ACTION_MAX_OVERLAY_ENTRIES)
        {
            platform->FileClose(file);
            Engine_LogError("Action: the overlay at '%s' declares %u entries, above the limit of %u; starting from defaults", path,
                            static_cast<unsigned>(header.entryCount), static_cast<unsigned>(ACTION_MAX_OVERLAY_ENTRIES));
            return;
        }
        if (header.mapDigest != Engine_Action_GetMapDigest())
        {
            platform->FileClose(file);
            Engine_LogError("Action: the overlay at '%s' was written against a different action map; starting from defaults", path);
            return;
        }

        ActionOverlayEntry entries[ACTION_MAX_OVERLAY_ENTRIES];
        const size_t entriesBytes = sizeof(ActionOverlayEntry) * header.entryCount;
        read = entriesBytes ? platform->FileRead(file, entries, entriesBytes) : 0;
        platform->FileClose(file);

        if (read != entriesBytes)
        {
            Engine_LogError("Action: the overlay at '%s' is missing entries; starting from defaults", path);
            return;
        }
        if (Fnv1a(reinterpret_cast<const uint8_t*>(entries), entriesBytes) != header.checksum)
        {
            Engine_LogError("Action: the overlay at '%s' failed its checksum; starting from defaults", path);
            return;
        }

        const uint32_t declaredCount = Engine_Action_DeclaredCount();
        for (uint32_t i = 0; i < header.entryCount; ++i)
        {
            const ActionOverlayEntry& e = entries[i];
            const ActionRecord* record = (e.action < declaredCount) ? Engine_Action_GetRecord(static_cast<ActionId>(e.action)) : nullptr;
            if (!record || e.binding >= record->bindingCount || e.sourceCount > ACTION_MAX_SOURCES_PER_BINDING)
            {
                Engine_LogError("Action: the overlay at '%s' names an action or binding past the compiled table; starting from defaults", path);
                ClearOverlay();
                return;
            }
        }

        memcpy(s_OverlayEntries, entries, entriesBytes);
        s_OverlayCount = header.entryCount;
    }

    bool SaveOverlayNow()
    {
        if (!s_OverlayPersistent)
            return false;

        char path[IO_FILE_MAX_PATH];
        if (!OverlayPath(path, sizeof(path)))
            return false;

        ActionOverlayHeader header;
        header.magic = ACTION_OVERLAY_MAGIC;
        header.version = static_cast<uint16_t>(ACTION_OVERLAY_VERSION);
        header.entryCount = static_cast<uint16_t>(s_OverlayCount);
        header.mapDigest = Engine_Action_GetMapDigest();
        const size_t entriesBytes = sizeof(ActionOverlayEntry) * s_OverlayCount;
        header.checksum = Fnv1a(reinterpret_cast<const uint8_t*>(s_OverlayEntries), entriesBytes);

        Platform* platform = Engine_GetPlatform();
        FileHandle file = platform->FileOpen(path, FileMode::Write);
        if (!file)
        {
            if (!s_ReportedNoStorage)
            {
                s_ReportedNoStorage = true;
                Engine_LogError("Action: cannot write '%s'; rebindings will not survive a restart", path);
            }
            return false;
        }

        bool ok = platform->FileWrite(file, &header, sizeof(header)) == sizeof(header);
        if (ok && entriesBytes)
            ok = platform->FileWrite(file, s_OverlayEntries, entriesBytes) == entriesBytes;
        platform->FileClose(file);

        if (!ok && !s_ReportedNoStorage)
        {
            s_ReportedNoStorage = true;
            Engine_LogError("Action: failed writing '%s'; rebindings will not survive a restart", path);
        }
        return ok;
    }

    const ActionOverlayEntry* FindOverlayEntry(uint16_t action, uint8_t binding)
    {
        for (uint32_t i = 0; i < s_OverlayCount; ++i)
        {
            if (s_OverlayEntries[i].action == action && s_OverlayEntries[i].binding == binding)
                return &s_OverlayEntries[i];
        }
        return nullptr;
    }

    // A binding's effective sources: the overlay's, if the player rebound it,
    // otherwise the compiled ones. The compiled table is never modified.
    void EffectiveSources(ActionId action, uint8_t bindingIndex, const ActionCompiledBinding& compiled, const ActionSourceRef** outSources, uint8_t* outCount)
    {
        const ActionOverlayEntry* overlay = FindOverlayEntry(static_cast<uint16_t>(action), bindingIndex);
        *outSources = overlay ? overlay->sources : compiled.sources;
        *outCount = overlay ? overlay->sourceCount : compiled.sourceCount;
    }

    // --- Reading a source through Input, never through Platform directly -------

    bool ReadDigital(ActionSourceRef ref)
    {
        const ActionSourceDevice device = Action_SourceDevice(ref);
        const ActionSourceKind kind = Action_SourceKind(ref);
        const uint16_t code = Action_SourceCode(ref);

        switch (device)
        {
        case ActionSourceDevice::Gamepad:
            // Gamepad buttons are packed as a bit POSITION (ACTION_OVERLAY.md:
            // the mask does not fit ten bits and is not denser if it did), so
            // reading one converts back to the mask IsGamePadButtonPressed wants.
            if (kind == ActionSourceKind::Button)
                return code < 16 && IsGamePadButtonPressed(0, static_cast<GamepadButton>(1u << code));
            if (kind == ActionSourceKind::Trigger)
                return GetGamePadTrigger(0, static_cast<GamepadTrigger>(code)) >= INPUT_ANALOG_DEADZONE;
            return false;
        case ActionSourceDevice::Keyboard:
            return kind == ActionSourceKind::Button && IsKeyDown(static_cast<KeyboardKey>(code));
        case ActionSourceDevice::Mouse:
            return kind == ActionSourceKind::Button && IsMouseButtonDown(static_cast<MouseButton>(code));
        case ActionSourceDevice::Touch:
        case ActionSourceDevice::Count:
        default:
            return false;
        }
    }

    bool SourcesSatisfied(const ActionSourceRef* sources, uint8_t count)
    {
        if (count == 0)
            return false;
        for (uint8_t i = 0; i < count; ++i)
        {
            if (!ReadDigital(sources[i]))
                return false;
        }
        return true;
    }

    // A source's magnitude, for a scalar/axis1d/composite-leg reading. Buttons
    // read as 0/1, so a digital source can stand in for a trigger.
    float ReadUnipolar(ActionSourceRef ref)
    {
        if (Action_SourceKind(ref) == ActionSourceKind::Trigger && Action_SourceDevice(ref) == ActionSourceDevice::Gamepad)
            return GetGamePadTrigger(0, static_cast<GamepadTrigger>(Action_SourceCode(ref)));
        return ReadDigital(ref) ? 1.0f : 0.0f;
    }

    Vector2 ReadStick(ActionSourceRef ref)
    {
        const ActionSourceDevice device = Action_SourceDevice(ref);
        const ActionSourceKind kind = Action_SourceKind(ref);
        const uint16_t code = Action_SourceCode(ref);

        if (device == ActionSourceDevice::Gamepad && kind == ActionSourceKind::Stick)
            return GetGamePadAxis(0, static_cast<GamepadStick>(code));
        if (device == ActionSourceDevice::Mouse && kind == ActionSourceKind::Stick && code == static_cast<uint16_t>(MouseAxis::Delta))
            return GetMouseDelta();
        return Vector2{0.0f, 0.0f};
    }

    // positive/negative in [0,1] each; returns the signed result per opposing
    // policy. legWinner (nullable) remembers which leg won for lastWins.
    float ResolveOpposing(ActionOpposingPolicy policy, float positive, float negative, uint8_t* legWinner)
    {
        const bool posHeld = positive > 0.0f;
        const bool negHeld = negative > 0.0f;

        if (posHeld && negHeld)
        {
            if (policy == ActionOpposingPolicy::Neutral || !legWinner)
                return 0.0f;
            if (*legWinner == 0)
                *legWinner = 1; // both just became held on the same frame: keep the existing default
            return (*legWinner == 1) ? positive : -negative;
        }
        if (legWinner)
            *legWinner = posHeld ? 1 : (negHeld ? 2 : 0);
        if (posHeld)
            return positive;
        if (negHeld)
            return -negative;
        return 0.0f;
    }

    // --- Context stack -----------------------------------------------------------

    bool Stronger(ActionBlock a, ActionBlock b) { return static_cast<uint8_t>(a) > static_cast<uint8_t>(b); }

    int FindStackSlot(uint8_t context)
    {
        for (uint8_t i = 0; i < s_ContextStackDepth; ++i)
        {
            if (s_ContextStack[i] == context)
                return i;
        }
        return -1;
    }

    // effectiveBlock[i] is the strongest block declared by any context pushed
    // after (above) stack slot i -- what a context at that slot is suppressed by.
    void ComputeEffectiveBlocks(ActionBlock* effectiveBlock)
    {
        ActionBlock strongest = ActionBlock::Nothing;
        for (int i = static_cast<int>(s_ContextStackDepth) - 1; i >= 0; --i)
        {
            effectiveBlock[i] = strongest;
            const ActionContextRecord* record = Engine_Action_GetContextRecord(static_cast<ActionContextId>(s_ContextStack[i]));
            if (record && Stronger(record->blocks, strongest))
                strongest = record->blocks;
        }
    }

    void ForceRelease(ActionId action, const ActionRecord& record)
    {
        ActionState& state = s_State[static_cast<uint32_t>(action)];
        if (state.held)
        {
            state.held = false;
            state.heldSeconds = 0.0f;
            ++state.releaseTransitions;
        }
        state.axisX = 0.0f;
        state.axisY = 0.0f;

        for (uint8_t b = 0; b < record.bindingCount; ++b)
        {
            const uint8_t slot = record.bindings[b].dynamicSlot;
            if (slot != 0xFF)
            {
                s_Dynamic[slot].claimed = false;
                s_Dynamic[slot].pendingSeconds = 0.0f;
            }
        }
    }

    // --- The three sequential passes over every dynamic-slotted binding --------

    void UpdateSatisfaction()
    {
        const uint32_t actionCount = Engine_Action_DeclaredCount();
        for (uint32_t a = 0; a < actionCount; ++a)
        {
            const ActionRecord* record = Engine_Action_GetRecord(static_cast<ActionId>(a));
            if (!record)
                continue;
            for (uint8_t b = 0; b < record->bindingCount; ++b)
            {
                const ActionCompiledBinding& binding = record->bindings[b];
                if (binding.dynamicSlot == 0xFF || binding.isComposite)
                    continue;

                const ActionSourceRef* sources;
                uint8_t sourceCount;
                EffectiveSources(static_cast<ActionId>(a), b, binding, &sources, &sourceCount);
                s_Dynamic[binding.dynamicSlot].satisfied = SourcesSatisfied(sources, sourceCount);
            }
        }
    }

    void UpdateEligibility(float dt)
    {
        const uint32_t actionCount = Engine_Action_DeclaredCount();
        for (uint32_t a = 0; a < actionCount; ++a)
        {
            const ActionRecord* record = Engine_Action_GetRecord(static_cast<ActionId>(a));
            if (!record)
                continue;
            for (uint8_t b = 0; b < record->bindingCount; ++b)
            {
                const ActionCompiledBinding& binding = record->bindings[b];
                if (binding.dynamicSlot == 0xFF || binding.isComposite)
                    continue;
                ActionDynamicState& dyn = s_Dynamic[binding.dynamicSlot];

                const bool waits = binding.waitsForCombination && record->combination == ActionCombination::Deferred;
                if (!waits)
                {
                    dyn.eligible = dyn.satisfied;
                    dyn.pendingSeconds = 0.0f;
                    continue;
                }

                bool widerLive = false;
                for (uint8_t w = 0; w < binding.widerSiblingCount; ++w)
                    widerLive = widerLive || s_Dynamic[binding.widerSiblingDynamicSlot[w]].satisfied;

                if (!dyn.satisfied || widerLive)
                {
                    dyn.pendingSeconds = 0.0f;
                    dyn.eligible = false;
                }
                else
                {
                    dyn.pendingSeconds += dt;
                    dyn.eligible = dyn.pendingSeconds >= binding.combinationWindowSeconds;
                }
            }
        }
    }

    // Widest-combination-first claiming. Visits real combinations (sourceCount
    // > 1) in descending source count; a claimed binding keeps its sources
    // claimed every frame it stays satisfied, and a narrower eligible binding
    // naming an already-claimed source never claims. A combination is
    // digital-only (the schema has no analog "sources" combination), so a
    // context that suppresses digital actions removes it from the pool
    // entirely -- otherwise a suppressed action could still claim sources out
    // from under an action that is not suppressed.
    void ResolveClaims(const ActionBlock* effectiveBlock)
    {
        static uint8_t order[ACTION_MAX_DYNAMIC_BINDINGS];
        static const ActionCompiledBinding* orderBinding[ACTION_MAX_DYNAMIC_BINDINGS];
        uint32_t orderCount = 0;

        const uint32_t actionCount = Engine_Action_DeclaredCount();
        for (uint32_t a = 0; a < actionCount && orderCount < ACTION_MAX_DYNAMIC_BINDINGS; ++a)
        {
            const ActionRecord* record = Engine_Action_GetRecord(static_cast<ActionId>(a));
            if (!record)
                continue;

            // A reserved engine debug intent claims regardless of context --
            // see the suppression check in the main loop below for why.
            if (!Engine_Action_IsReserved(static_cast<ActionId>(a)))
            {
                const int slot = FindStackSlot(record->context);
                const ActionBlock block = (slot < 0) ? ActionBlock::Everything : effectiveBlock[slot];
                if (block != ActionBlock::Nothing) // combinations are always digital-kind, so Digital suppresses them too
                    continue;
            }

            for (uint8_t b = 0; b < record->bindingCount && orderCount < ACTION_MAX_DYNAMIC_BINDINGS; ++b)
            {
                const ActionCompiledBinding& binding = record->bindings[b];
                if (binding.dynamicSlot != 0xFF && binding.sourceCount > 1 && !binding.isComposite)
                {
                    order[orderCount] = binding.dynamicSlot;
                    orderBinding[orderCount] = &binding;
                    ++orderCount;
                }
            }
        }

        // Insertion sort, descending by source count: orders of magnitude
        // fewer than ACTION_MAX_DYNAMIC_BINDINGS in practice.
        for (uint32_t i = 1; i < orderCount; ++i)
        {
            const uint8_t keySlot = order[i];
            const ActionCompiledBinding* keyBinding = orderBinding[i];
            int j = static_cast<int>(i) - 1;
            while (j >= 0 && orderBinding[j]->sourceCount < keyBinding->sourceCount)
            {
                order[j + 1] = order[j];
                orderBinding[j + 1] = orderBinding[j];
                --j;
            }
            order[j + 1] = keySlot;
            orderBinding[j + 1] = keyBinding;
        }

        static ActionSourceRef claimedSources[ACTION_MAX_DYNAMIC_BINDINGS * ACTION_MAX_SOURCES_PER_BINDING];
        uint32_t claimedCount = 0;
        auto markClaimed = [&](const ActionCompiledBinding& binding) {
            for (uint8_t s = 0; s < binding.sourceCount && claimedCount < ACTION_MAX_DYNAMIC_BINDINGS * ACTION_MAX_SOURCES_PER_BINDING; ++s)
                claimedSources[claimedCount++] = binding.sources[s];
        };
        auto isClaimed = [&](ActionSourceRef ref) {
            for (uint32_t i = 0; i < claimedCount; ++i)
            {
                if (claimedSources[i] == ref)
                    return true;
            }
            return false;
        };

        for (uint32_t i = 0; i < orderCount; ++i)
        {
            ActionDynamicState& dyn = s_Dynamic[order[i]];
            if (!dyn.satisfied)
            {
                dyn.claimed = false;
                continue;
            }
            if (dyn.claimed)
            {
                markClaimed(*orderBinding[i]); // stays claimed; keep blocking narrower siblings
                continue;
            }
            if (!dyn.eligible)
                continue;

            bool blocked = false;
            for (uint8_t s = 0; s < orderBinding[i]->sourceCount && !blocked; ++s)
                blocked = isClaimed(orderBinding[i]->sources[s]);
            if (blocked)
                continue;

            dyn.claimed = true;
            markClaimed(*orderBinding[i]);
        }
    }
} // namespace

bool Engine_Action_Init()
{
    s_Loaded = true;
    s_ContextStackDepth = 0;
    s_ReportedNoStorage = false;
    s_ReportedOverlayFull = false;
    memset(s_State, 0, sizeof(s_State));
    memset(s_Dynamic, 0, sizeof(s_Dynamic));

    LoadOverlay();

    // The outermost declared context is the base layer: always on the stack,
    // so an action declared against it (a global pause, say) reads without the
    // game having to push anything. It is never popped from here.
    if (Engine_Action_DeclaredContextCount() > 0)
    {
        s_ContextStack[0] = 0;
        s_ContextStackDepth = 1;
    }

    Engine_LogInfo("Action: ready (%u actions, %u contexts, overlay %s)", Engine_Action_DeclaredCount(), Engine_Action_DeclaredContextCount(),
                   s_OverlayPersistent ? "on disc" : "in memory only");
    return true;
}

void Engine_Action_Shutdown()
{
    if (s_Loaded && s_OverlayDirty)
    {
        if (SaveOverlayNow())
            s_OverlayDirty = false;
    }
    s_Loaded = false;
    s_ContextStackDepth = 0;
}

void Engine_Action_Update(float dt)
{
    if (!s_Loaded)
        return;

    ActionBlock effectiveBlock[ACTION_CONTEXT_STACK_DEPTH];
    ComputeEffectiveBlocks(effectiveBlock);

    UpdateSatisfaction();
    UpdateEligibility(dt);
    ResolveClaims(effectiveBlock);

    const uint32_t actionCount = Engine_Action_DeclaredCount();
    for (uint32_t a = 0; a < actionCount; ++a)
    {
        const ActionId id = static_cast<ActionId>(a);
        const ActionRecord* record = Engine_Action_GetRecord(id);
        if (!record)
            continue;
        ActionState& state = s_State[a];

        // A reserved engine debug intent (PerfSnapshot/OverlayToggle/DebugMenu)
        // is never suppressed: it must stay reachable no matter what context a
        // game has pushed, which is the entire reason Debug/PerfLogger/Testbed
        // can check for input through Action at all rather than needing their
        // own bypass.
        if (!Engine_Action_IsReserved(id))
        {
            const int slot = FindStackSlot(record->context);
            const ActionBlock block = (slot < 0) ? ActionBlock::Everything : effectiveBlock[slot];
            if (block == ActionBlock::Everything || (block == ActionBlock::Digital && record->kind == ActionKind::Digital))
            {
                ForceRelease(id, *record);
                continue;
            }
        }

        if (record->kind == ActionKind::Digital)
        {
            bool aggregate = false;
            for (uint8_t b = 0; b < record->bindingCount && !aggregate; ++b)
            {
                const ActionCompiledBinding& binding = record->bindings[b];
                if (binding.dynamicSlot != 0xFF)
                    aggregate = binding.sourceCount > 1 ? s_Dynamic[binding.dynamicSlot].claimed : s_Dynamic[binding.dynamicSlot].eligible;
                else
                {
                    const ActionSourceRef* sources;
                    uint8_t sourceCount;
                    EffectiveSources(id, b, binding, &sources, &sourceCount);
                    aggregate = SourcesSatisfied(sources, sourceCount);
                }
            }

            if (aggregate != state.held)
            {
                if (aggregate)
                    ++state.pressTransitions;
                else
                    ++state.releaseTransitions;
            }
            state.held = aggregate;

            if (state.held)
            {
                state.heldSeconds += dt;
                if (record->repeatDelay > 0.0f && state.heldSeconds >= record->repeatDelay)
                {
                    if (state.nextRepeatAt < record->repeatDelay)
                        state.nextRepeatAt = record->repeatDelay;
                    const float interval = record->repeatInterval > 0.0f ? record->repeatInterval : record->repeatDelay;
                    if (state.heldSeconds >= state.nextRepeatAt)
                    {
                        ++state.repeatTicks;
                        state.nextRepeatAt += interval;
                    }
                }
            }
            else
            {
                state.heldSeconds = 0.0f;
                state.nextRepeatAt = 0.0f;
            }
        }
        else
        {
            // Analog: winner-take-all by magnitude among the action's live
            // bindings, shaped (threshold -> scale -> invert -> normalize) on
            // top of whatever the platform's own deadzone already clamped.
            float bestMag = -1.0f;
            float bestX = 0.0f, bestY = 0.0f;

            for (uint8_t b = 0; b < record->bindingCount; ++b)
            {
                const ActionCompiledBinding& binding = record->bindings[b];
                float x = 0.0f, y = 0.0f;

                if (binding.isComposite)
                {
                    ActionDynamicState* dyn = (binding.dynamicSlot != 0xFF) ? &s_Dynamic[binding.dynamicSlot] : nullptr;
                    const float positive = (binding.compositeLegMask & 0x01u) ? ReadUnipolar(binding.sources[0]) : 0.0f;
                    const float negative = (binding.compositeLegMask & 0x02u) ? ReadUnipolar(binding.sources[1]) : 0.0f;
                    const float yAxis = ResolveOpposing(record->opposing, positive, negative, dyn ? &dyn->legWinner[1] : nullptr);

                    if (record->kind == ActionKind::Axis2d)
                    {
                        const float right = (binding.compositeLegMask & 0x08u) ? ReadUnipolar(binding.sources[3]) : 0.0f;
                        const float left = (binding.compositeLegMask & 0x04u) ? ReadUnipolar(binding.sources[2]) : 0.0f;
                        x = ResolveOpposing(record->opposing, right, left, dyn ? &dyn->legWinner[0] : nullptr);
                        y = yAxis;
                    }
                    else
                    {
                        x = yAxis; // axis1d: positive/negative maps onto the single value
                    }
                }
                else if (record->kind == ActionKind::Axis2d)
                {
                    const ActionSourceRef* sources;
                    uint8_t sourceCount;
                    EffectiveSources(id, b, binding, &sources, &sourceCount);
                    if (sourceCount > 0)
                    {
                        const Vector2 v = ReadStick(sources[0]);
                        x = v.x;
                        y = v.y;
                    }
                }
                else
                {
                    const ActionSourceRef* sources;
                    uint8_t sourceCount;
                    EffectiveSources(id, b, binding, &sources, &sourceCount);
                    x = sourceCount > 0 ? ReadUnipolar(sources[0]) : 0.0f;
                }

                float mag = sqrtf(x * x + y * y);
                if (mag < binding.threshold)
                {
                    x = 0.0f;
                    y = 0.0f;
                }
                x *= binding.scale;
                y *= binding.scale;
                if (binding.invert)
                {
                    x = -x;
                    y = -y;
                }
                if (binding.normalize)
                {
                    const float len = sqrtf(x * x + y * y);
                    if (len > 1.0f)
                    {
                        x /= len;
                        y /= len;
                    }
                }

                mag = sqrtf(x * x + y * y);
                if (mag > bestMag)
                {
                    bestMag = mag;
                    bestX = x;
                    bestY = y;
                }
            }

            state.axisX = bestX;
            state.axisY = (record->kind == ActionKind::Axis2d) ? bestY : 0.0f;
        }
    }
}

void Engine_Action_ResetRuntimeState()
{
    memset(s_State, 0, sizeof(s_State));
    memset(s_Dynamic, 0, sizeof(s_Dynamic));
    s_ContextStackDepth = 0;
    if (Engine_Action_DeclaredContextCount() > 0)
    {
        s_ContextStack[0] = 0;
        s_ContextStackDepth = 1;
    }
}

bool Engine_Action_IsHeld(ActionId action)
{
    if (!s_Loaded || static_cast<uint32_t>(action) >= Engine_Action_DeclaredCount())
        return false;
    return s_State[static_cast<uint32_t>(action)].held;
}

bool Engine_Action_WasPressed(ActionId action)
{
    if (!s_Loaded || static_cast<uint32_t>(action) >= Engine_Action_DeclaredCount())
        return false;
    ActionState& state = s_State[static_cast<uint32_t>(action)];
    if (state.pressTransitions == 0)
        return false;
    --state.pressTransitions;
    return true;
}

bool Engine_Action_WasReleased(ActionId action)
{
    if (!s_Loaded || static_cast<uint32_t>(action) >= Engine_Action_DeclaredCount())
        return false;
    ActionState& state = s_State[static_cast<uint32_t>(action)];
    if (state.releaseTransitions == 0)
        return false;
    --state.releaseTransitions;
    return true;
}

uint32_t Engine_Action_PressCount(ActionId action)
{
    if (!s_Loaded || static_cast<uint32_t>(action) >= Engine_Action_DeclaredCount())
        return 0;
    ActionState& state = s_State[static_cast<uint32_t>(action)];
    const uint32_t count = state.pressTransitions;
    state.pressTransitions = 0;
    return count;
}

bool Engine_Action_WasRepeatTick(ActionId action)
{
    if (!s_Loaded || static_cast<uint32_t>(action) >= Engine_Action_DeclaredCount())
        return false;
    ActionState& state = s_State[static_cast<uint32_t>(action)];
    if (state.repeatTicks == 0)
        return false;
    --state.repeatTicks;
    return true;
}

float Engine_Action_GetAxis1d(ActionId action)
{
    if (!s_Loaded || static_cast<uint32_t>(action) >= Engine_Action_DeclaredCount())
        return 0.0f;
    return s_State[static_cast<uint32_t>(action)].axisX;
}

void Engine_Action_GetAxis2d(ActionId action, float* outX, float* outY)
{
    float x = 0.0f, y = 0.0f;
    if (s_Loaded && static_cast<uint32_t>(action) < Engine_Action_DeclaredCount())
    {
        const ActionState& state = s_State[static_cast<uint32_t>(action)];
        x = state.axisX;
        y = state.axisY;
    }
    if (outX)
        *outX = x;
    if (outY)
        *outY = y;
}

float Engine_Action_GetScalar(ActionId action)
{
    if (!s_Loaded || static_cast<uint32_t>(action) >= Engine_Action_DeclaredCount())
        return 0.0f;
    return s_State[static_cast<uint32_t>(action)].axisX;
}

bool Engine_Action_GetPrompt(ActionId action, ActionSourceDevice* outDevice, ActionSourceKind* outKind, uint16_t* outCode)
{
    if (!s_Loaded)
        return false;
    const ActionRecord* record = Engine_Action_GetRecord(action);
    if (!record || record->bindingCount == 0)
        return false;

    const ActionSourceRef* sources;
    uint8_t sourceCount;
    EffectiveSources(action, 0, record->bindings[0], &sources, &sourceCount);
    if (sourceCount == 0)
        return false;

    if (outDevice)
        *outDevice = Action_SourceDevice(sources[0]);
    if (outKind)
        *outKind = Action_SourceKind(sources[0]);
    if (outCode)
        *outCode = Action_SourceCode(sources[0]);
    return true;
}

bool Engine_Action_Rebind(ActionId action, uint8_t bindingIndex, uint8_t sourceSlot, ActionSourceDevice device, ActionSourceKind kind, uint16_t code)
{
    if (!s_Loaded)
        return false;
    const ActionRecord* record = Engine_Action_GetRecord(action);
    if (!record || !record->rebindable || bindingIndex >= record->bindingCount)
        return false;

    const ActionCompiledBinding& compiled = record->bindings[bindingIndex];
    if (sourceSlot >= compiled.sourceCount)
        return false;

    const uint16_t actionIndex = static_cast<uint16_t>(action);
    ActionOverlayEntry* entry = nullptr;
    for (uint32_t i = 0; i < s_OverlayCount; ++i)
    {
        if (s_OverlayEntries[i].action == actionIndex && s_OverlayEntries[i].binding == bindingIndex)
        {
            entry = &s_OverlayEntries[i];
            break;
        }
    }
    if (!entry)
    {
        if (s_OverlayCount >= ACTION_MAX_OVERLAY_ENTRIES)
        {
            if (!s_ReportedOverlayFull)
            {
                s_ReportedOverlayFull = true;
                Engine_LogError("Action: rebind refused; the overlay is full (%u entries)", static_cast<unsigned>(ACTION_MAX_OVERLAY_ENTRIES));
            }
            return false;
        }
        entry = &s_OverlayEntries[s_OverlayCount++];
        entry->action = actionIndex;
        entry->binding = bindingIndex;
        entry->sourceCount = compiled.sourceCount;
        memcpy(entry->sources, compiled.sources, sizeof(ActionSourceRef) * compiled.sourceCount);
    }

    entry->sources[sourceSlot] = Action_PackSource(device, kind, code);
    s_OverlayDirty = true;
    if (SaveOverlayNow())
        s_OverlayDirty = false;
    return true;
}

void Engine_Action_RestoreDefault(ActionId action)
{
    if (!s_Loaded)
        return;
    const uint16_t actionIndex = static_cast<uint16_t>(action);
    uint32_t write = 0;
    bool changed = false;
    for (uint32_t read = 0; read < s_OverlayCount; ++read)
    {
        if (s_OverlayEntries[read].action == actionIndex)
        {
            changed = true;
            continue;
        }
        if (write != read)
            s_OverlayEntries[write] = s_OverlayEntries[read];
        ++write;
    }
    s_OverlayCount = write;
    if (changed)
    {
        s_OverlayDirty = true;
        if (SaveOverlayNow())
            s_OverlayDirty = false;
    }
}

bool Engine_Action_SaveOverlay()
{
    if (!s_Loaded)
        return false;
    const bool ok = SaveOverlayNow();
    if (ok)
        s_OverlayDirty = false;
    return ok;
}

bool Engine_Action_PushContext(ActionContextId context)
{
    if (!s_Loaded)
        return false;
    if (static_cast<uint32_t>(context) >= Engine_Action_DeclaredContextCount())
    {
        Engine_LogError("Action: PushContext of an undeclared context refused");
        return false;
    }
    if (s_ContextStackDepth >= ACTION_CONTEXT_STACK_DEPTH)
    {
        Engine_LogError("Action: context stack is full (%u); push refused", static_cast<unsigned>(ACTION_CONTEXT_STACK_DEPTH));
        return false;
    }
    if (FindStackSlot(static_cast<uint8_t>(context)) >= 0)
    {
        Engine_LogError("Action: context %u is already on the stack; push refused", static_cast<unsigned>(context));
        return false;
    }
    s_ContextStack[s_ContextStackDepth++] = static_cast<uint8_t>(context);
    return true;
}

void Engine_Action_PopContext()
{
    if (!s_Loaded)
        return;
    if (s_ContextStackDepth <= 1)
    {
        Engine_LogError("Action: pop refused; the base context stays on the stack for the life of the subsystem");
        return;
    }
    --s_ContextStackDepth;
}

ActionContextId Engine_Action_CurrentContext()
{
    if (!s_Loaded || s_ContextStackDepth == 0)
        return static_cast<ActionContextId>(0);
    return static_cast<ActionContextId>(s_ContextStack[s_ContextStackDepth - 1]);
}

ActionId Engine_Action_ReservedPerfSnapshot()
{
    const uint32_t count = Engine_Action_DeclaredCount();
    return static_cast<ActionId>((count >= ACTION_RESERVED_COUNT) ? (count - 3) : 0);
}

ActionId Engine_Action_ReservedOverlayToggle()
{
    const uint32_t count = Engine_Action_DeclaredCount();
    return static_cast<ActionId>((count >= ACTION_RESERVED_COUNT) ? (count - 2) : 0);
}

ActionId Engine_Action_ReservedDebugMenu()
{
    const uint32_t count = Engine_Action_DeclaredCount();
    return static_cast<ActionId>((count >= ACTION_RESERVED_COUNT) ? (count - 1) : 0);
}

bool Engine_Action_IsReserved(ActionId action)
{
    const uint32_t count = Engine_Action_DeclaredCount();
    return count >= ACTION_RESERVED_COUNT && static_cast<uint32_t>(action) >= count - ACTION_RESERVED_COUNT;
}
