# Subsystem — Action

*Codename: Ares*

## Purpose

Report what the player *meant*, from what the [Input](INPUT.md) subsystem reports
they *did*. Input answers "is Start held on port 0"; Action answers "does the
player want to pause". The map between the two is a declaration, not code, so a
control scheme can change without a rebuild of anything that reads it, and so a
platform missing a button can answer the same question with different hardware.

This is the layer [INPUT.md](INPUT.md) previously said the engine did not have
and games should build for themselves. It is now engine-owned because every
consumer needed the same three things — a name instead of a mask, a combination
instead of a button, and a prompt glyph that matches the pad in front of the
player — and three copies of that in game code would each have gone wrong
differently.

## Contract

**An action is a name; what satisfies it is data.** Callers name an intent and
receive a state. Nothing above this subsystem may read a button mask to decide
whether an intent was expressed. A caller testing a specific button next to an
action query is the symptom of a binding that should have been declared and was
not.

**Resolution happens once per frame, between the input poll and the first
query.** Every action's state for the frame is computed from the input snapshot
in one pass, and all queries answer from that result. This inherits Input's
guarantee for the same reason Input has it: two queries in one frame must agree,
and a chord evaluated lazily inside a query could observe a different snapshot
from the query beside it. It also means resolution cost is paid once and is
independent of how many times an action is asked about.

**Bindings are ordered alternatives, resolved against capability, not against
platform identity.** An action carries a list of candidate bindings in
preference order; the first whose every source exists on the running hardware is
the one that answers. This is the same reasoning as debug chords: a handheld
with one shoulder row cannot press a binding naming the second row, and a
binding chosen by platform name would have to be revisited for every platform
added afterwards. Choosing by capability means a new platform that answers the
capability queries honestly gets correct controls with no change to the
declaration.

**A binding with no viable candidate is a build failure, not a runtime one.**
An action whose every candidate names a source the target hardware lacks is
unreachable, and unreachable controls are found by players rather than by tests.
The declaration is checked against each platform's capability answers when it is
compiled.

**A combination is satisfied only while every one of its buttons is held.**
There is no ordering requirement and no timing requirement between the presses.
Requiring an order would make a combination a gesture, which is a different
feature with different failure modes, and is deliberately not this one.

**The widest combination wins, and claims its buttons.** Candidates are
considered in descending order of how many sources they name, and a satisfied
binding marks its sources as claimed for the rest of the frame. A narrower
binding naming a claimed source is skipped. This is what makes a two-button pause
coexist with a one-button menu without either being declared aware of the other:

```
  held: Start + Select

  Pause      = Start + Select   two sources   satisfied -> claims both
  OpenMenu   = Start            one source    source claimed -> skipped
  Back       = Select           one source    source claimed -> skipped
```

Single-source bindings do not claim, so two actions may share one button
deliberately and both fire.

**A combination's buttons stay claimed until every one of them is released.**
The action itself stops the moment the combination stops being satisfied, but its
sources remain claimed while any are still down. Without this, letting go of a
combination is indistinguishable from pressing whatever is left:

```
  t0   Start down, Select down   Pause active,   claimed { Start, Select }
  t1   Start up                  Pause released, claimed { Select }   <- held
  t2   Select up                 claim cleared
  t3   Select down               Back fires, normally
```

The failure this prevents is silent and looks like a game bug rather than an
input one: pausing, then releasing the two buttons a few milliseconds apart,
lands the player back in the previous menu.

**A binding whose sources are a subset of a combination's waits before firing.**
Declared alone, a one-button action fires the instant its button goes down, which
is before the player can add the second button of a combination that starts the
same way. Such a binding is therefore withheld for a short window, during which
both its edge and its held state read as inactive:

```
  window: the declaration's combination window, in seconds

  the combination completes
    t=0      Start down            OpenMenu withheld
    t=0.04   Select down           Pause active; OpenMenu never fires

  the window expires
    t=0      Start down            OpenMenu withheld
    t=window                       OpenMenu fires, one window late

  released inside the window
    t=0      Start down            OpenMenu withheld
    t=0.06   Start up              OpenMenu fires and releases in one frame;
                                   the edge is preserved, see below
```

Which bindings are affected is derived when the declaration is compiled, not
declared by hand — a subset relationship the author has to notice is one they
will eventually fail to notice. An action may opt out where the delay matters
more than the ambiguity, and opting out while remaining a subset of a
combination is reported.

**An edge is never lost to a frame nobody asked about.** Each action counts the
transitions observed since the last time the caller consumed them, not merely
its current and previous state. A press and release that both happen between two
queries are still both visible. This exists for the simulation running at a
different rate from the frame loop: with current-versus-previous alone, a
simulation slower than the frame rate silently drops the fastest inputs, which
reads as unreliable controls rather than as a missed edge.

**An analog action chooses one binding; it never blends two.** When more than
one candidate is live — a stick and a set of keys both bound to movement — the
one with the greatest magnitude answers, compared as a whole vector rather than
per axis. Per-axis comparison would let a slight push on a stick supply one axis
while keys supply the other, producing a direction neither device was asked for.
Ties resolve to the earlier candidate in the declaration, so the outcome is
authored rather than incidental.

**Opposing directions resolve by a declared policy.** Two opposite directions
held at once is ordinary on a keyboard and possible on a worn pad, so it has an
answer rather than an accident: cancel to neutral, or let the later press win.
Neutral is the default because it is the one that cannot be exploited.

**Contexts are a stack, and covering a context releases what it held.** Actions
belong to a context; contexts are pushed and popped in order; a blocking context
suppresses those beneath it. Suppression forces held actions to release rather
than freezing them, so opening a menu while a weapon is held produces a release
edge and the weapon is not still firing when the menu closes. A context may
declare that it blocks digital actions while letting analog ones through, for an
overlay that should not freeze the camera.

**Times are in seconds, never frames.** Two of this engine's platform variants
differ only in refresh rate, so a combination window or a repeat interval counted
in frames is a different duration on each of them, and a frame that runs long
makes it a different duration again. This is the same rule the interface theme
states for its own repeat metrics, for the same reason.

**Analog shaping sits on top of the platform's, and cannot undo it.** The
platform applies its own deadzone before this subsystem sees a stick, as a
required platform constant. Any additional shaping declared here — a tighter
response for menus than for movement — narrows what is already narrowed and
never widens it. Two consequences are part of the contract rather than
incidental. A declaration asking for a threshold below the platform's gets the
platform's, and is told so at compile time rather than behaving as though it had
been honoured. And a shaping mode that needs the values the platform already
discarded cannot be offered honestly: the platform's cut is per axis, so the
region near each axis is zero before it arrives, and a radial mode reconstructed
from that would claim a precision it does not have. Changing this is a platform
contract change, not a change here.

**A prompt is answered from the same declaration as the state.** Asking what to
draw for an action returns the binding that is actually live on this hardware,
which is the one the capability pass selected, and the glyph family comes from
the platform. An interface drawing a button prompt therefore cannot show a
control that does not exist or is not the one bound, which is the failure that
made this worth centralising.

**Rebinding is an overlay, never a rewrite.** Player changes are a sparse list of
replacements against the compiled map, stored separately and applied on top of
it. The compiled map is never modified. An overlay whose map is not the one it
was written against is discarded rather than applied, because a declaration
changed by a patch can reassign what an entry referred to. The stored layout is
[ACTION_OVERLAY.md](../formats/ACTION_OVERLAY.md).

**This is the only way anything in the engine checks for input.** Nothing in
`engine/src` — not Debug's performance-snapshot trigger, not PerfLogger's
overlay toggle, not the Testbed's open/close — reads `Input` or a platform
chord directly; each asks Action for a declared intent, the same as game
code does. Input and Action are consequently not really optional: a game
cannot omit either, because there is nothing else in the engine that would
still work. The three exceptions are the raw hardware-diagnostic testbed
scenes (gamepad state, keyboard/mouse state, the platform's own chord
table) — their entire purpose is showing state beneath any binding layer,
which an Action query cannot do by construction.

**Every declaration ends with three reserved actions, regardless of what the
title declares of its own.** `EngineDebugPerfSnapshot`, then
`EngineDebugOverlayToggle`, then `EngineDebugMenu` — always the three
highest-numbered ids, in exactly that order, checked at compile time. This is
what lets `engine/src` find them without a per-title lookup: engine code
that cannot see a title's generated identifiers asks for "the last three
declared actions" instead of a name. Reserving the *end* of the range rather
than a fixed low id keeps a title's own ids dense from zero with no gap and
no wasted table space between the two ranges. A title chooses the bindings
for these three exactly as it would for any other action — capability
fallback, per-platform pruning and rebinding all apply unchanged — which is
what replaces the old platform-fixed chord defaults; a title whose gameplay
needs different shoulder buttons is no longer stuck with them.

**A reserved action is exempt from context suppression.** A debug intent
must stay reachable no matter what context stack a game has built — opening
the debug menu from inside a paused, menu-covered, dialog-covered state is
the case it exists for. The three reserved actions therefore resolve as
though nothing were pushed above their context, regardless of what actually
is.

## Declaration

The action map is authored once, for every platform, as a declaration under the
game's configuration space, validated against a schema owned by the cook system.
A reader compiles it at build time into a generated identifier header and a
generated table, both compiled into the binary.

| Declared | Meaning |
|---|---|
| key | The enumerator game code names. Fixed for the life of the title. |
| context | Which context the action belongs to |
| kind | Digital, one-dimensional, two-dimensional, or scalar |
| bindings | Candidates in preference order; the first viable one answers |
| sources | Devices and controls a candidate names, all of which must be satisfied |
| combination window | Seconds a subset binding waits before firing |
| repeat | Delay and interval for a held action that should retick |
| shaping | Threshold and response applied above the platform's deadzone |
| opposing | How two opposite directions resolve |

Source names are read out of the engine's own input enumerations rather than
listed a second time in the tool, so a control named in the declaration that the
engine does not have fails the build instead of producing an action that is never
satisfied. This follows the interface theme's handling of colour roles.

**The map is generated into the binary rather than cooked.** Three reasons, and
they are the contract rather than an implementation choice. It carries no
platform-varying encoding, so cooking it per platform would produce identical
output in every tree. It must be available before the filesystem, the archive and
the resource manager exist, because those subsystems are optional and this one
must work without them. And there is exactly one map per title, so the ability to
load a different one at run time — the reason the theme is cooked as well as
generated — has no consumer. The only part of this subsystem that reaches disc is
the player's overlay, which is a record rather than an asset.

## Depends on

- **[Input](INPUT.md)** — the per-frame snapshot, the four device groups, edge
  state, and the capability answers that select between binding candidates. This
  subsystem reads nothing else and calls no platform method directly, so the
  device-level quirks stay where they are documented: [PS2](../ps2/PLATFORM.md),
  [Win32](../win32/PLATFORM.md), [Vita](../vita/PLATFORM.md).

## Depended on by

- Game code, through the public game API.
- [UI](UI.md) — every navigation, pointing and cursor read.
- [Debug](DEBUG.md) — the performance-snapshot and overlay-toggle triggers,
  through the two reserved actions.
- [Testbed](TESTBED.md) — the open/close trigger, through the third reserved
  action; also depends on [UI](UI.md) for the menu itself.
- Effectively everything that reads input at all: see the Contract section
  above. `Input` and `Action` are force-enabled regardless of what a game
  requests (`Engine_Subsystems_Set`), which is what makes depending on Action
  safe for engine-internal subsystems that must not become unreachable —
  there is no configuration in which a game chose not to have it.

## Lifecycle

Brought up after Input, since it reads Input's snapshot and cannot resolve
anything before one exists. Resolution runs once per frame, after the input poll
and before game update; a frame in which resolution does not run leaves the
previous frame's result in place, with the same staleness consequences Input
states for a skipped poll. Shutdown releases nothing: all storage is fixed and
statically sized.

Both the context stack and every action's accumulated transitions are cleared by
a runtime state reset, so a scene reached through the testbed starts from a known
state rather than inheriting a context the previous scene pushed and did not pop.

## When not loaded

Not a reachable configuration in practice — `Input` and `Action` are the two
subsystems `Engine_Subsystems_Set` force-enables regardless of what a game
requests, because Debug, UI and Testbed all depend on Action and none of them
may become unreachable. The guards below exist anyway, for the same reason
every other entry point in this engine is guarded: a subsystem behaving
correctly when it is not loaded is verified by omitting it and booting, and
that verification is only honest if the code path it exercises is real
rather than assumed unreachable.

Every action query reports inactive, zero movement, and no edges. Prompt
queries report that they have no binding to draw, which the interface renders
as its own fallback text rather than as a missing glyph.

## Failure modes

- **An action is queried that the declaration does not define.** A programmer
  error, since identifiers come from a generated header; it is reported and
  answers inactive.
- **No viable binding on this hardware.** Prevented at compile time. If it is
  reached at run time — a capability answered differently from how it was
  declared — the action answers inactive and reports once, naming the action,
  never once per frame.
- **Two different actions bound to the exact same combination.** Not a subset
  relationship — "the widest combination wins" only arbitrates between
  bindings of *different* width, and two bindings of equal, identical sources
  tie-break by declaration order, so the later one is permanently shadowed by
  the earlier one's claim every time both are held. This is a build failure,
  the same as an unviable binding, naming both actions and the platform,
  rather than a control a player discovers has silently never worked.
- **The context stack overflows, or a context is popped out of order.** A
  programmer error. The push or pop is refused and reported; the stack is left
  consistent rather than partially unwound.
- **A context is pushed that is already on the stack.** Refused and reported: a
  later pop could not say which of the two it ended.
- **Resolution skipped for a frame.** The previous result persists, so held state
  is stale and edges from that frame are missed. Undetectable from inside, and a
  bring-up error in the frame loop rather than a condition to recover from.
- **The overlay is for a different map.** Discarded, reported, and the compiled
  defaults are used. The player's changes are lost, which is preferable to
  applying a replacement to whichever action inherited that entry's position.
- **The overlay is full.** A further rebind is refused and reported rather than
  displacing an existing one.
- **A device is lost mid-combination.** Its sources report released, the
  combination ends, and its claim is cleared in the same frame, so a pad
  disconnected during a pause combination does not leave buttons claimed forever.

## Limits

- **No gestures, and no sequences.** A combination is a set of simultaneously
  held sources. Anything ordered — a double tap, a directional sequence, a
  charge-and-release — is the game's to build above the edge counts this
  subsystem reports.
- **No per-player binding sets.** Every port resolves against the same map.
  Split-screen with different controls per player is not supported, and would be
  a storage change in the overlay rather than a contract change here.
- **No analog output.** Rumble, lights and speakers are not addressed; they are
  not input and are not routed through an input map.
- **No button pressure.** The PS2 pad reports a force per face button and the
  platform contract does not carry it, so no binding can name it. Adding it is a
  platform contract addition first, and gated on a capability, because it is the
  only platform of the three that has it.
- **No recording or playback.** The resolved result is a fixed-size value derived
  from the input snapshot, which makes recording straightforward to add later,
  but nothing here writes or replays one.
- **The map is fixed at build time.** Only the overlay changes at run time, and
  only by replacing the sources of an existing binding — never by adding an
  action, a context, or a binding that was not declared.
- **Bindings name controls, not layouts.** A keyboard binding names a key by its
  engine enumerator; what is printed on that key on a given physical layout is
  not known to this subsystem, and a prompt for a keyboard binding shows the
  enumerator's name.

## Off-the-shelf evaluation

An input mapping layer is a well-served problem on desktop, and every candidate
was ruled out by construction rather than on merit:

| Candidate | Ruled out by |
|---|---|
| SDL game controller subsystem and its mapping database | Does not build for either console toolchain; owns device enumeration, which the platform layer already does |
| Steam Input action sets | Desktop runtime dependency; no console target |
| Generic mapping libraries built on STL containers and dynamic allocation | No STL containers, no growth, fixed budgets |
| The pad database approach generally | Solves device identification, which the platform layer solves; contributes nothing to combinations, contexts or prompts |

Written in-engine. The whole of what these libraries would supply below this
layer already exists as the platform input contract, and what is needed above it
is small, has no dependency of its own, and has to satisfy this engine's
constraints exactly.

## Budget

All storage is fixed-capacity and statically sized from format constants, in
keeping with the constants rule: the declaration reader depends on the same
limits, which makes them format constants rather than platform capabilities, and
they are therefore identical everywhere.

| Table | Scale |
|---|---|
| Compiled map — actions, bindings, sources, contexts | generated, constant, resides with the executable's read-only data rather than in a budget |
| Per-action runtime state | one small fixed record per declared action |
| Claim state | one bit per source the current frame can claim |
| Context stack | bounded by a declared depth |
| Overlay | bounded by a declared entry count |

Mutable storage totals single-digit kilobytes at the declared limits, which is
why this subsystem takes no arena and no pool allocation: it is static, it does
not grow, and a subsystem that cannot grow does not need an allocator. Resolution
cost is proportional to the number of bindings in the active contexts, once per
frame, and is bounded by the same constants.
