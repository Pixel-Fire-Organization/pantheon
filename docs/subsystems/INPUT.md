# Subsystem — Input

## Purpose

Report what the player is doing, identically on every platform, from devices that
are not identical at all. Input is a thin facade over the active platform, which
owns the device reading; the engine owns the query model.

## Contract

**One poll per frame, then a snapshot.** Every device is read once at the top of
the frame into a snapshot, and all queries answer from it. Two queries in the
same frame therefore always agree. This is a guarantee, not an optimisation: the
earlier design re-read the device inside each query, so a button could report
pressed and released within one frame, and any device requiring a pumped message
queue could not be supported at all.

**Four device groups, never blurred.** Gamepad, keyboard, mouse and touch are
separate query groups with separate names. A platform without a keyboard answers
every keyboard query with a negative rather than pretending a pad is a keyboard.
Callers that need to branch on what exists ask the platform for the capability
instead of testing the platform identity.

**Touch is not a mouse**, and the two are never mapped onto each other in either
direction. A mouse has exactly one cursor, which exists whether or not a button
is held; a touch surface has zero or more contacts, which exist only while
touched. A platform with a mouse and no touchscreen reports touch as absent even
though it has a pointer, and a platform with a touchscreen and no mouse reports
mouse as absent even though the player can point at things. Collapsing the two
would make "can the player point at this?" unanswerable.

**Edge detection is free and correct.** The previous frame snapshot is retained
alongside the current one, so rising and falling edges are derived rather than
tracked by callers. Hand-rolled edge detection in game or engine code is a
symptom of this contract not being used.

**Keys are enumerated, not numeric.** Device keys are enumerated types shared
with the platform layer, so a mismapped constant is a compile error rather than a
control that silently does the wrong thing. This matters more than it sounds: the
retired hand-written masks had all four shoulder buttons transposed against the
hardware, and the code compiled and ran.

**Debug combinations are supplied by the platform.** Engine tooling asks for an
intent, not for buttons; the platform returns the buttons. This is the same
reasoning as the capabilities above — a handheld with one shoulder row cannot
press a four-shoulder combination, and hard-coding one in shared code produces a
feature that is silently unreachable rather than reported absent.

**Device-bridging is an explicit, separate layer.** Where a platform maps one
device onto another — a keyboard driving a virtual gamepad, so that pad-only
content is playable on a desktop — that mapping is a named, disableable layer,
not something baked into the device queries. With it disabled, the keyboard still
reports as a keyboard; only the virtual pad disappears.

**The character channel is not a keyboard query.** `Platform::Keyboard_PopCharacters`
exists for text entry (`PlatformCapability::TextCharacters`) and is drained by
the UI subsystem's own text-editing widgets, never through the keyboard device
group above. Pressing A is a `KeyboardKey::A` edge to this subsystem's query
model and, on a platform that also offers the character channel, an `'a'` byte
to that separate stream at the same time. The two are never merged, for the
same reason keyboard and gamepad stay separate device groups: a caller reading
raw key state should never also be silently consuming bytes meant for whatever
text field currently has focus.

**Touch positions are normalised, not pixels.** Contacts are reported in [0,1]
over their own surface. This is not a convenience: a rear touch surface is behind
the device and has no pixel correspondence to anything on screen, so reporting it
in pixels would be inventing a mapping that does not exist. A caller wanting
screen coordinates for a front surface multiplies by the framebuffer size it
already knows.

**Touch contacts carry a stable identity** for as long as the finger stays down,
so a drag can be followed across frames without matching positions by proximity.

## Depends on

- **Platform** — device reading, the per-frame poll, and the capability
  reporting that says which groups are real. Per-platform device details are in
  the platform specs: [PS2](../ps2/PLATFORM.md), [Win32](../win32/PLATFORM.md),
  [Vita](../vita/PLATFORM.md), [PSP](../psp/PLATFORM.md), [nx](../nx/PLATFORM.md).

## Depended on by

- [Debug](DEBUG.md) — the performance overlay trigger is an input combination.
- [Action](ACTION.md) — the per-frame snapshot, edge state, and the capability
  answers that select between binding candidates. Its only dependency.
- Game code, through the public game API, which mirrors the same four-way split.

## Lifecycle

Started with the platform, since it is the platform that owns the devices. It
must be polled once per frame, before anything queries it. Shutdown releases any
device handles the platform opened.

## When not loaded

Every query reports nothing pressed and zero movement. The engine still runs, and
this is exactly the configuration a headless or automated host wants: content
that reads input sees an idle controller rather than requiring a device to exist.

## Failure modes

- **Device absent or disconnected** — queries report a neutral result and the
  capability query reports the group as unavailable. Disconnection mid-session is
  not an error; a controller unplugged during play reports as absent and may
  return.
- **Poll skipped for a frame** — the snapshot goes stale, so held state persists
  and every edge is missed. There is no detection for this; it is a bring-up
  error in the frame loop.
- **Focus loss** — on platforms with a window, held keys are cleared when focus
  leaves. Without that, a key held while switching away stays held forever, and
  the player returns to a character walking into a wall.

## Limits

- Port count for gamepads is platform-defined.
- Touch surfaces are platform-defined: a platform may have one, both, or neither,
  and the maximum simultaneous contacts is a platform limit.
- There is no gesture recognition — no taps, swipes, pinches or long-presses.
  The engine reports contacts; anything built from them is the game's.
- Analog triggers are a capability, not a guarantee: platforms without them
  report their triggers as fully released or fully pressed.
- Mouse position is in window coordinates and is meaningless on platforms with no
  window; movement deltas are still reported relative to the last poll.
- There is no input recording or playback layer in the engine. Remapping is no
  longer in this sentence: [Action](ACTION.md) is the layer that builds above
  these queries, resolving bindings by capability, combinations, contexts and
  a player-rebindable overlay. Games that want raw device state still read it
  here directly; Action is not a requirement to use Input.
