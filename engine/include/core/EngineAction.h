#pragma once

#include <cstdint>

// Format constants: both the on-disc overlay layout and tools/actions.py
// depend on every one of these, so they are identical on every platform. See
// docs/subsystems/ACTION.md and docs/formats/ACTION_OVERLAY.md.
#define ACTION_MAX_ENTRIES 256 // the declaration's "id" is 0..255, dense
#define ACTION_MAX_BINDINGS 8 // the declaration's "bindings" array, 1..8 items
#define ACTION_MAX_SOURCES_PER_BINDING 4 // the declaration's "sources" array, 1..4 items
#define ACTION_MAX_CONTEXTS 32
#define ACTION_CONTEXT_STACK_DEPTH 8
#define ACTION_MAX_OVERLAY_ENTRIES 64

// Bindings that need extra per-frame state beyond the aggregate ActionState —
// a combination (claim + satisfaction, for the widest-wins pass), a subset of
// one (a combination-window countdown), or a composite using the lastWins
// opposing policy (which leg pressed more recently). tools/actions.py assigns
// a dense slot to exactly the bindings that need one when it compiles the
// declaration for a platform, so this bounds real per-title usage rather than
// the worst case of every declared binding needing one.
#define ACTION_MAX_DYNAMIC_BINDINGS 64
#define ACTION_OVERLAY_MAGIC 0x4F415350u // "PSAO" read little-endian
#define ACTION_OVERLAY_VERSION 1u
#define ACTION_OVERLAY_FILE "actions.dat"

// The three actions with the HIGHEST declared ids, in this exact order, are
// always the engine's own debug intents -- tools/actions.py requires every
// title's declaration to end with these three keys, regardless of how many
// actions of its own the title declares. "Last three by id" rather than a
// fixed id keeps a title's own ids dense from zero with no gap, and no
// wasted table space between the two ranges. Engine code (not generated,
// so it cannot name a game's generated ActionId enumerators) locates them
// with Engine_Action_Reserved*() below rather than a literal id.
#define ACTION_RESERVED_COUNT 3
#define ACTION_RESERVED_KEY_DEBUG_PERF_SNAPSHOT "EngineDebugPerfSnapshot"
#define ACTION_RESERVED_KEY_DEBUG_OVERLAY_TOGGLE "EngineDebugOverlayToggle"
#define ACTION_RESERVED_KEY_DEBUG_MENU "EngineDebugMenu"

/// digital is held or not; scalar is unipolar, for a trigger; axis1d is
/// bipolar; axis2d is a direction.
enum class ActionKind : uint8_t
{
    Digital = 0,
    Axis1d,
    Axis2d,
    Scalar,

    Count
};

/// Whether a binding waits when its sources are a subset of a combination's.
enum class ActionCombination : uint8_t
{
    Deferred = 0,
    Immediate,

    Count
};

/// How two opposite composite legs held at once resolve.
enum class ActionOpposingPolicy : uint8_t
{
    Neutral = 0,
    LastWins,

    Count
};

/// What a context suppresses in contexts beneath it on the stack.
enum class ActionBlock : uint8_t
{
    Nothing = 0,
    Digital,
    Everything,

    Count
};

/// Which of the four device groups a source belongs to. Matches
/// ACTION_OVERLAY.md's source reference bits 15..13.
enum class ActionSourceDevice : uint8_t
{
    Gamepad = 0,
    Keyboard,
    Mouse,
    Touch,

    Count
};

/// The shape of control a source is, independent of which device group it
/// belongs to. Matches ACTION_OVERLAY.md's source reference bits 12..10.
enum class ActionSourceKind : uint8_t
{
    Button = 0,
    Stick,
    Trigger,
    Contact,

    Count
};

/// One source, packed exactly as ACTION_OVERLAY.md's on-disc source reference:
/// bits 15..13 device, bits 12..10 kind, bits 9..0 code. The same packed form
/// is used by the generated table and the overlay record, so replacing a
/// binding's sources on rebind is a value copy, never a repack.
using ActionSourceRef = uint16_t;

constexpr ActionSourceRef Action_PackSource(ActionSourceDevice device, ActionSourceKind kind, uint16_t code)
{
    return static_cast<ActionSourceRef>((static_cast<uint16_t>(device) << 13) | (static_cast<uint16_t>(kind) << 10) | (code & 0x03FFu));
}

inline ActionSourceDevice Action_SourceDevice(ActionSourceRef ref) { return static_cast<ActionSourceDevice>((ref >> 13) & 0x0007u); }
inline ActionSourceKind Action_SourceKind(ActionSourceRef ref) { return static_cast<ActionSourceKind>((ref >> 10) & 0x0007u); }
inline uint16_t Action_SourceCode(ActionSourceRef ref) { return static_cast<uint16_t>(ref & 0x03FFu); }

// The generated identifier header (ActionIds.h) defines these for real; this
// forward declaration is enough for this header and EngineAction.cpp to name
// the types, and keeps the same one-way dependency EngineAchievement.h has:
// generated code includes engine headers, engine headers never include
// generated code.
enum class ActionId : uint8_t;
enum class ActionContextId : uint8_t;

/// One candidate binding, already resolved against this platform's capability
/// manifest at build time: only bindings viable on the running hardware are
/// compiled in at all, and every one of them stays live simultaneously (an
/// action never picks a single "winner" among its viable candidates — see
/// docs/subsystems/ACTION.md's analog selection rule).
struct ActionCompiledBinding
{
    uint8_t declaredIndex; // which declaration bindings[] entry this was, for prompts
    uint8_t sourceCount;
    bool isComposite; // true for a {composite: {up/down/left/right|positive/negative}} binding
    ActionSourceRef sources[ACTION_MAX_SOURCES_PER_BINDING]; // combination sources, or one source;
                                                             // for a composite, always laid out
                                                             // [up/positive, down/negative, left, right]
    uint8_t compositeLegMask; // composite only: bit0 up/positive, bit1 down/negative, bit2 left, bit3 right
    float threshold; // analog only: clamped >= this platform's INPUT_ANALOG_DEADZONE
    float scale;
    bool invert;
    bool normalize;
    bool waitsForCombination; // static: this binding is a subset of a wider one, and is deferred
    float combinationWindowSeconds; // seconds to wait while deferring; the declaration's settings.combinationWindow
    uint8_t widerSiblingCount;
    uint8_t widerSiblingDynamicSlot[4]; // dynamic slots of the wider combinations this is a subset of
    uint8_t dynamicSlot; // index into the per-frame dynamic-binding table, or 0xFF for none
};

/// One declared action, compiled for the running platform. Generated into
/// ActionTable.<platform>.cpp by tools/actions.py --emit-table.
struct ActionRecord
{
    const char* key;
    uint8_t context; // ActionContextId, as a raw index
    ActionKind kind;
    ActionCombination combination;
    ActionOpposingPolicy opposing;
    bool rebindable;
    float repeatDelay; // seconds; 0 means no repeat
    float repeatInterval;
    uint8_t bindingCount; // >= 1, or the platform's build failed to compile this action
    ActionCompiledBinding bindings[ACTION_MAX_BINDINGS];
};

/// One declared context. Generated alongside ActionRecord.
struct ActionContextRecord
{
    const char* key;
    ActionBlock blocks;
};

/// A player's rebinding, as it sits on disc. See docs/formats/ACTION_OVERLAY.md.
struct ActionOverlayEntry
{
    uint16_t action; // index into the compiled action table
    uint8_t binding; // which of that action's live candidates this replaces
    uint8_t sourceCount;
    ActionSourceRef sources[ACTION_MAX_SOURCES_PER_BINDING];
};

struct ActionOverlayHeader
{
    uint32_t magic;
    uint16_t version;
    uint16_t entryCount;
    uint32_t mapDigest;
    uint32_t checksum;
};

/// Bring the Action subsystem up. Loads the player's overlay, if one exists
/// and matches the compiled map's digest.
bool Engine_Action_Init();

/// Saves the overlay if it changed since the last save.
void Engine_Action_Shutdown();

/// Resolve every action's state for this frame from Input's snapshot. Call
/// once per frame, after the input poll and before the first query.
void Engine_Action_Update(float dt);

/// Clears the context stack and every action's accumulated transitions.
void Engine_Action_ResetRuntimeState();

uint32_t Engine_Action_DeclaredCount();
uint32_t Engine_Action_DeclaredContextCount();

// Defined in the generated ActionTable.<platform>.cpp, alongside the two
// counts above. EngineAction.cpp's resolution pass reads every field of the
// compiled record every frame, so this returns the record itself rather than
// narrow per-field accessors.
const ActionRecord* Engine_Action_GetRecord(ActionId action);
const ActionContextRecord* Engine_Action_GetContextRecord(ActionContextId context);

/// A digest of the compiled map's identity (action count/keys/rebindability/
/// binding-slot-counts), baked in by tools/actions.py. A loaded overlay whose
/// mapDigest disagrees with this was written against a different declaration
/// and is discarded whole. Defined in the generated table alongside the above.
uint32_t Engine_Action_GetMapDigest();

// --- Digital -----------------------------------------------------------------
bool Engine_Action_IsHeld(ActionId action);
bool Engine_Action_WasPressed(ActionId action); // consumes one press transition
bool Engine_Action_WasReleased(ActionId action); // consumes one release transition
uint32_t Engine_Action_PressCount(ActionId action); // transitions since last consumed; consumes them all
bool Engine_Action_WasRepeatTick(ActionId action); // consumes one synthetic repeat press

// --- Analog ------------------------------------------------------------------
float Engine_Action_GetAxis1d(ActionId action); // [-1, 1]
void Engine_Action_GetAxis2d(ActionId action, float* outX, float* outY);
float Engine_Action_GetScalar(ActionId action); // [0, 1]

// --- Prompts / rebinding -------------------------------------------------------
/// @return False when the action has no live binding to draw.
bool Engine_Action_GetPrompt(ActionId action, ActionSourceDevice* outDevice, ActionSourceKind* outKind, uint16_t* outCode);

/// @param bindingIndex Which of the action's live candidates to change.
/// @param sourceSlot Which source within that candidate to change.
/// @return False when the action is not rebindable, or either index is out of range.
bool Engine_Action_Rebind(ActionId action, uint8_t bindingIndex, uint8_t sourceSlot, ActionSourceDevice device, ActionSourceKind kind, uint16_t code);

/// Removes the action's overlay entries, if any, restoring the compiled sources.
void Engine_Action_RestoreDefault(ActionId action);

/// Writes the overlay now, regardless of whether it is dirty.
bool Engine_Action_SaveOverlay();

// --- Contexts ------------------------------------------------------------------
/// Refused and reported if the stack is full or the context is already on it.
bool Engine_Action_PushContext(ActionContextId context);

/// Refused and reported if the stack would underflow.
void Engine_Action_PopContext();

ActionContextId Engine_Action_CurrentContext();

// --- Reserved engine debug intents ---------------------------------------------
// The only way Debug/PerfLogger/Testbed check for input: no engine subsystem
// reads Input or a platform chord directly for these three intents. Exempt
// from context suppression (see Engine_Action_IsReserved's use inside
// Engine_Action_Update) -- a debug intent must stay reachable no matter what
// context a game has pushed.
ActionId Engine_Action_ReservedPerfSnapshot();
ActionId Engine_Action_ReservedOverlayToggle();
ActionId Engine_Action_ReservedDebugMenu();

/// @return Whether an action id is one of the three reserved above.
bool Engine_Action_IsReserved(ActionId action);
