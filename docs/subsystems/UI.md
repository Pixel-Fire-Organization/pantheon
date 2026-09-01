# Subsystem — UI

## Purpose

Draw an interface and let the player drive it, on hardware that cannot afford a
general one. Before this subsystem the engine had no interface layer and no text
at all: the only screen-space primitive was a coloured rectangle, and the one
piece of content that needed labels carried its own bitmap font. Anything that
wanted to show a value on screen — a menu, a heads-up display, the debug
testbed — had to reinvent both drawing and navigation.

## Contract

**Immediate mode.** A widget is a call made fresh every frame, not an object
that is created, retained and later destroyed. There is no widget tree and no
allocation. An interface is therefore a function of the state it reads, which is
what makes it impossible for the display to disagree with the thing displayed —
the failure a retained tree produces whenever an update is missed.

**Nothing displayed is retained.** The interface keeps a small block of
*interaction* state between frames — what is focused, what is hovered, what is
being pressed, where a region is scrolled to, whether a node is open, how long a
repeat has been held. It never keeps a value it draws. That distinction is the
whole of the guarantee above: a retained scroll position cannot disagree with
anything, because nothing reads it but the interface itself.

**Identity comes from the call, not from storage.** A widget is identified by
the scope it is asked for in and by the label it carries, so its interaction
state survives between frames without any widget owning it. Scopes nest, so two
containers may hold identically-named rows without collision. Two widgets that
would still collide are a caller error and are reported as one, because silently
sharing identity makes two controls act as one — and now also makes them share a
scroll position or an open flag — which looks like an input bug.

**A label that changes is a different widget.** Identity is derived from the
label, so text that carries mutable state -- a row reading "UNLOCK" one frame
and "DONE" the next -- silently becomes a new widget, losing focus and its
interaction state at the moment the state it displays changes. A row that
displays something changeable keeps a fixed label and puts the changing part in
a separate value.

**One submission per frame.** Widget calls accumulate into fixed buffers — one
for the interface and a smaller one for content that must draw above it — and
they are concatenated and handed to the renderer exactly once. Backends
therefore see the interface as a single batch to translate, not as a stream of
individual calls, and a backend needs no knowledge of what a widget is.

**Screen space, submitted before the frame begins.** The interface is built
during the update phase, ahead of the frame the renderer opens, exactly as the
existing screen-space rectangle path is. A backend that discarded work submitted
before its frame started would lose the entire interface while the world still
drew correctly — a failure already documented for the renderer, and one this
subsystem is the largest consumer of.

**Draw order is the contract.** Quads are submitted back to front and a backend
draws them in the order given. The interface never reorders, never batches by
state, and never merges two overlapping quads — the union of two overlapping
rectangles is generally not a rectangle, and searching for the cases where it is
would cost more than the drawing it saves. A caller can therefore reason about
what covers what by reading its own calls in order.

The two reductions it does make are both local and both exact: content outside
the current clip is dropped before it is submitted, and the rectangles making up
a single glyph are merged where they are vertically adjacent and identical.
Neither can change what is drawn, only how many quads say it.

**A solid fill never samples a font atlas.** A panel, a row background, a border
or a bar is untextured, even while a cooked font is loaded — it costs one vertex
quad and no texture fetch, which matters on a fill-rate-bound GPU where a solid
fill is the majority of the pixels drawn. The consequence is that a row of solid
fill next to glyph text is two runs, not one, so the buffer that groups quads by
texture for submission is sized with that alternation in mind rather than for a
batch that never splits.

**The interface clips itself.** Containers establish a clip rectangle, and
content is clipped against it before it is submitted rather than by the renderer
afterwards. Clipped-away content therefore costs nothing at all — not to submit,
not to draw — which is what makes a long scrolling list affordable on the
constrained platform. Clipping governs interaction as well as drawing: a control
scrolled fully out of view is not focusable and not hoverable, or it would still
be reachable while invisible.

**Text is the cost, and it is budgeted.** A glyph drawn from the built-in font
costs several quads; a glyph drawn from a cooked font costs one. Either way the
buffer has a fixed ceiling, and work past it is dropped and reported **once per
frame with a count**, never once per dropped item, because a per-item diagnostic
on the slowest platform costs more than the frame it describes. The ceiling is a
real authoring constraint: an interface is designed to fit it rather than
assumed to be free.

**Fonts are cooked, and there is always one.** The interface prefers a cooked
font — proportional metrics, one quad per glyph, drawn from an atlas — and falls
back to a built-in bitmap font whenever one is not available: no cooked font
declared for this platform, the resource subsystem absent, the asset missing, or
its atlas refused for exceeding the texture budget. Each of those is reported
once, never per frame. The fallback is a permanent part of the contract, not a
transitional state, because the interface must be able to draw before any
content pipeline exists.

**Colour is never part of a font.** A font supplies coverage only. Colour comes
from the widget, so one font serves every colour role at no extra cost, and a
theme change recolours all text without touching any content.

**Icons and controller glyphs are cells in the same atlas as the glyphs, and
they are optional the same way a cooked font itself is.** A font may declare a
set of them, addressed by an interface-defined identifier rather than a
codepoint; an icon the active font has no cell for draws as a short piece of
text instead, so a hint bar never goes silent for a button the atlas has not
been taught to draw. Which shape a button prompt draws for a given pad button
is a platform fact resolved once per query, never a theme choice — a theme has
no way to know what hardware it is running on, and getting it wrong would show
the player a button that is not on the pad in front of them.

The families are PlayStation shapes, Xbox letters, Nintendo letters and keyboard
keys. The two letter families share their four letter cells and disagree on
which button carries which letter: Xbox puts A on the bottom and Y on top,
Nintendo puts A on the right and X on top. Which engine button a letter lands on
is decided by how the platform maps its pad — positionally for Xbox, by label
for Nintendo — so a prompt for the engine's confirm button reads *A* on both.
See [nx/PLATFORM.md](../nx/PLATFORM.md#input).

**Styling is by role, not by call site.** Colours and metrics are named for what
they mean — surface, text, accent, focus, and so on — and a widget asks for a
role rather than a colour. A theme is therefore one value that can be replaced
whole, and a widget cannot quietly opt out of it.

**A theme is colours, metrics and fonts together, not colours alone.** A theme
built for a small or low-quality display can declare a larger text scale and
wider borders as part of *being* that theme, rather than a caller composing a
colour theme with a metrics theme by hand. Exactly one theme in a declaration
is marked default; every other theme may name as few or as many of its own
colours, metrics and fonts as it wants, and whatever it does not name is
copied from the default theme, field by field. The style block itself always
holds one complete, resolved set — the composition happens before a theme is
built or cooked, never at draw time. The default theme is where that
composition bottoms out: it must name every colour role and every metric
itself, since there is nothing above it to inherit from, and a declaration
that leaves it incomplete is refused at cook time rather than producing a
theme with a missing field.

**The display's size is the platform's answer, not the theme's.** A theme
declares its spatial metrics against one reference framebuffer; a platform whose
screen is materially smaller states that as a proportion, and every style is
brought onto the display as it is installed — built-in and cooked alike, through
the single point every style passes through, so no widget and no theme has to
know. Durations are left alone: they are time, not space. No metric is allowed
to scale below one pixel, because a border or a caret that rounds to zero
disappears rather than merely shrinking.

This is deliberately not a theme's job. A theme that hard-coded a handheld's
metrics would be the wrong theme on every other platform, and the alternative —
a second set of numbers per theme per display — multiplies with both. The size
of the screen is a hardware fact and is answered where the other hardware facts
are.

**Themes are selectable, and loadable.** A set of themes is built in and
selectable with no I/O, which is what makes the interface drawable before any
filesystem exists. A game may additionally load a cooked theme on command. A
loaded theme is validated **whole** before anything is written — its identity,
its layout version, its integrity, its font references, and the sanity of the
values it carries — and committed in one step, so a theme that fails any check
is refused and the live theme is left exactly as it was.

**A theme's colours, metrics and fonts commit together.** A decoded theme is a
candidate, not an installation: its font keys are staged with its style block
and both are written at the same moment a caller applies it. Decoding a theme
therefore changes nothing on its own, which is what lets one be inspected —
the Testbed's asset browser previews them — without its font keys becoming what
the interface asks the resource manager to load next. Loading one on command is
blocking, so the call either applies the theme or refuses it; there is no state
where it has been asked for and neither has happened.

**Focus and pointing reach the same widget.** Directional navigation with an
accept and a back action works on every platform, because every platform has a
pad. Pointing is layered on top where the hardware allows it. Neither is the
primary: a control reachable only by pointing is unreachable on a console, and a
control reachable only by focus wastes a touchscreen.

**Navigation is grouped.** Containers form navigation groups: directional
movement along one axis cycles within the active group, and along the other
moves between groups. Without this a single flat order walks out of one panel
and into the next one that happened to be built after it.

Widgets placed on the same row form a navigation run, one level narrower than a
group: directional movement across the row's axis moves within the run before
it moves between groups, and movement along the other axis steps over the whole
run to the row above or below rather than through it one widget at a time. A
row holding a single widget, which is every row that never asked to share one,
is a run of one and behaves exactly as if runs did not exist.

Left and right therefore have three claimants, tried in order: **the focused
widget itself**, when it consumes that axis to edit — a slider, a stepper, a tab
strip — which takes precedence so a control's own editing gesture is never
stolen by navigation; **the run**, when the focused widget shares its row with
another; and only then **the group**, moving to an adjacent one. Without the run
tier, two widgets placed side by side would be reachable only by pointing at
them, on a console that may have no pointer at all.

**A choice cycles in place; it does not drop down.** A row of options is edited
the same way a slider or a stepper is, with Left and Right, rather than opening
a floating list. On a pad, cycling is a strictly better gesture than navigating
into and back out of a popup, and it keeps every option's draw order exactly
where the caller put it rather than in a layer the interface would have to
manage. A choice with too many options to cycle through comfortably is a
scrolling list of selectable rows, which already exists and needs no popup
either.

**A disabled scope greys out and disconnects, without moving anything.** A
widget inside one still draws, in the same place, at the same size, so the
layout around it is stable whether or not it can be used right now — nothing
before or after it shifts. It is simply unreachable: not registered for
directional navigation, not responsive to the pointer, and reporting no
activation regardless of what a player does to it. Scopes nest, and an inner
scope that does not itself ask to disable cannot re-enable one an outer scope
already established — only closing that outer scope can.

**A panel's background is a role by default, and a per-panel opt-in
otherwise.** A panel's fill ordinarily reads the theme like anything else, so
C1's rule still governs it: untextured, one quad, no atlas sample. A caller may
instead ask for a specific colour or a stretched image for one panel, set
immediately before it opens; an image still costs exactly one quad — it
replaces the fill rather than layering over it — and degrades to the ordinary
fill while its texture is not yet ready, the same way any other resource
handle degrades. Both are one-shot, exactly like the width and height
overrides above.

**A collapsible panel reuses the mechanisms a tree already has, not a new
one.** Requested per panel, one-shot, the same way its background is: the
panel remembers whether it is collapsed the same way a tree node remembers
whether it is open, its header becomes the toggle, and collapsing it clips its
content to nothing rather than shrinking or moving the panel's own declared
rectangle. Every widget between `Ui_BeginPanel` and `Ui_EndPanel` still runs
and still updates whatever state it owns while collapsed — nothing is skipped
— it simply draws and registers nothing, for free, through the same clipping
contract a scrolled-away row already relies on.

**A budget is prevented, not reported after the fact.** `Ui_BeginBudget` caps
what the widgets before its matching `Ui_EndBudget` may add to the frame's
quad count; past the cap, a widget still runs — it still reads and writes
whatever state it owns — but draws nothing, the same shape *Buffer exhausted*
already has at the whole-frame level, narrowed to one block. `Ui_RunsUsed`
answers a related but different question: not how many quads this frame
holds, but how many draw-call runs they would coalesce into, since C1 made
solid fills untextured — a row alternating solid and glyph content opens one
run per alternation, which is invisible in a quad count alone.

**A menu bar docks to the top of a frame, and an open menu's items are still
exactly one submission.** Inside a panel it takes the panel's own top edge;
outside one, the top of the screen. Its titles share one navigation run, so
Left and Right move between them the same way they would between any other
widgets sharing a row. An open menu's item list cannot know its own size before
every item in it has been asked for — the same problem a scrolling region's
thumb has — so it is sized from what was measured the frame before, the same
answer that problem already has elsewhere in this contract: exact once a menu's
item count has settled, which is the ordinary case, and off by at most one
frame on the one it opens or changes.

**A dialog and a text field pick their own mechanism, and the caller never
branches on which one ran.** A message box, a confirmation and a text entry
are one request shape (`DialogKind`/`DialogRequest` in the platform contract),
served first by the host's own dialog where `PlatformCapability::SystemDialog`
answers for it, then — for text specifically — by a direct character channel
where `PlatformCapability::TextCharacters` answers instead, and always,
failing both, by the interface's own drawn modal: a message box built from
`Ui_BeginModal`, and an on-screen keyboard built from the same row/run
navigation a menu bar's titles already share. The floor is unconditional the
same way the built-in font and the built-in themes are — nothing above it
needs to know which of the three ran, because every one of them resolves to
the same `Ui_MessageDialog`/`Ui_ConfirmDialog`/`Ui_TextDialog` result. A host
dialog that blocks the calling thread (Win32's `MessageBox`) and one that
cannot (Vita's, which renders into the title's own frame and only advances
while it keeps presenting) are both reachable through the same non-blocking
poll for exactly this reason — the contract is shaped for the stricter of the
two.

**An image may come from a resource handle or from a live render, and a caller
never branches on which.** `Ui_Image` resolves a Resource handle to a loaded
texture; `Ui_Image3D` instead asks the renderer to draw a model or primitive
into a scratch offscreen target and hands back the result — see
[Renderer](RENDERER.md)'s `RenderToImage3D`. Both are a row of a given height
that draws a placeholder frame rather than nothing when the source is not
ready: loading or absent for a resource handle; a backend with no
offscreen-target support, a request that failed, or a model not yet ready for
`Ui_Image3D`. Neither loads, unloads, nor owns anything on the caller's
behalf. `Ui_Image3D` carries one further difference worth stating plainly: the
renderer owns exactly one scratch target per backend, so a second call anywhere
on screen this frame retargets the same storage — two live previews cannot be
composited from in the same frame.

**A field being edited still holds only a draft, never a second copy of the
truth.** *Nothing displayed is retained* still holds: the text shown while
editing is the caller's own buffer, written in place as the player types, not
a value the interface keeps and later hands back. The interface retains only
which field is being edited and, where a cancel needs to restore what was
there before, a bounded snapshot taken at the moment editing opened — interaction
state, in the same sense a scroll position or a held-open tree node already is,
not displayed content. Text entry supports appending and backspacing from the
end only; there is no mid-string caret placement, on any mechanism.

**Held directions repeat, in real time.** A direction held down repeats after a
delay and then at an interval, both measured in seconds. They are never measured
in frames: two of this engine's platform variants differ only in refresh rate,
and a frame-counted repeat would run measurably faster on one of them.

**The right stick scrolls directly; the left one only ever points.** Scrolling
by directional navigation alone reaches only what is focusable — a region of
plain text with nothing to focus in it cannot be reached by Up and Down at
all, which is the whole reason a second, unconditional path exists. The right
stick moves a scroll region's band by a pixel amount proportional to
its deflection, independently of focus: a player can look ahead in a list
without moving focus off whatever is already selected, and the two compose
rather than conflict, since focus-follow only ever adjusts the band when a
focused row would otherwise leave it.

**Both paths address the region that owns the focused row, and a region owns
only the rows registered inside it.** More than one scroll region may be on
screen at once, and what is focused is a property of the frame rather than of
any one of them, so a region that took the frame's focused row as its own
would follow a row belonging to another region — or to no region at all —
interpreting coordinates that mean nothing inside its band, and would fight
the player's own scrolling every frame it did so. Focus-follow therefore moves
only the region the focused row was registered in. The stick moves that same
region, and every region while focus is outside them all, which is what keeps
the plain-text region above — the case the stick exists for — reachable.

**A tab bar also answers to the shoulder buttons.** L1 and R1 step it to the
previous or next tab directly, without first moving focus onto a tab title —
the same shape a menu's own item highlight already has, sized from how many
tabs the bar held the frame before, since this frame's count is not known
until every tab has been asked for. This is in addition to, not instead of,
picking a tab by focusing its title and accepting, or by pointing at it.

**One cursor, several sources, last mover wins.** A mouse, a front touch surface
and the left stick all drive the same cursor; whichever moved it most recently
owns it. The stick source exists on every platform, so pointing is never absent —
it is the fallback that makes a pointer-driven interface honest on a pad-only
console. The cursor is hidden while the player is navigating by direction, so a
platform with no pointing device never shows a stray one.

**A rear touch surface is not a pointer.** It is behind the device and has no
pixel correspondence to anything on screen, so mapping it to a cursor would be
inventing a relationship that does not exist. It remains a device to be read, not
a way to point.

## Off-the-shelf evaluation

An existing immediate-mode library was the obvious candidate and was rejected on
four independent constraints. Three of them still stand, and any one is
disqualifying:

- It allocates during a frame, against an engine whose memory is reserved once at
  startup and never grown.
- It emits indexed triangle lists. Neither console backend draws indexed
  geometry — one treats it as a hard error — so its output would have to be
  unpicked before it could be drawn.
- Its per-frame draw volume is written against desktop budgets. The constrained
  target draw list and screen-space queue are two to three orders of magnitude
  smaller, and no configuration reconciles that.

The fourth constraint was that it required a font atlas and a textured
screen-space path, and neither existed on the slowest platform. That one has
since been removed by this subsystem's own work and no longer applies; it is
recorded because the decision was made against it.

A small owned implementation in the same style is therefore the decision, taking
the interaction model — which is the valuable part — without the rendering
assumptions, which are the part that does not port.

## Depends on

- **Renderer** — the single per-frame submission, the framebuffer size the
  interface is laid out against, and — for `Ui_Image3D` only — the offscreen
  render-to-image capability itself, which not every backend implements.
- **Input** — the per-frame snapshot that navigation and pointing read. Which
  pointing devices are real is a platform capability, never a platform identity.

**Optionally**, and never as a dependency row:

- **Resource** — supplies the cooked font and any cooked theme, and is what a
  caller's own handle to a loaded texture resolves against for Ui_Image. The
  interface never loads that handle itself, only draws whatever it names, so
  Ui_Image is exactly as optional as the caller's own use of Resource: without
  it every handle resolves as absent and a placeholder draws.

## Depended on by

- [Testbed](TESTBED.md) — its entire interaction surface.
- [Achievement](ACHIEVEMENT.md) — the unlock notification and the achievements
  screen, where the interface is present.
- [Scene](SCENE.md) — optionally: the built-in loading screen draws through it
  when present, and a `UiScene`'s content is drawn entirely through it; absent,
  loading still completes and scenes still run, just undrawn.
- Game code that draws a heads-up display or a menu.

## Lifecycle

Brought up after the renderer and input, since it needs both. Where the resource
subsystem is also present it requests its font at that point; the font becoming
ready later is expected, and the interface draws with its built-in font until it
does.

Each frame it is opened, filled by widget calls, and closed; closing submits the
frame's quads and resolves which widget is focused for the next one.

It is reset whenever the engine resets runtime state between scenes. A reset
drops retained interaction state, focus, the open container stacks and any
queued notification, restores the default theme, and re-requests the font —
because the reset that clears it also releases every resource, including ones
that were pinned.

Shutdown releases nothing the engine did not reserve at startup.

## When not loaded

No interface is submitted and every widget call reports "not interacted with".
Callers therefore compile and run unchanged, and the engine draws the world and
nothing else. This is what a release build and a headless host both look like.

## Failure modes

- **Buffer exhausted** — the excess is dropped and reported once for the frame,
  with a count. The interface is visibly truncated rather than corrupt, and the
  report names the shortfall so the screen can be reduced.
- **Colliding widget identity** — reported once, naming the identity involved.
  Left undetected this presents as two controls responding as one, and as one
  control's scroll position or open state moving when the other is used.
- **Frame not opened, or not closed** — widget calls outside a frame are refused
  and reported. An unclosed frame submits nothing, so the interface disappears
  entirely rather than showing a half-built one.
- **Container left open** — a container not closed by end of frame is reported
  and the frame is discarded, for the same reason.
- **Clip stack or scope stack exhausted** — reported once for the frame; the
  offending container is not opened. Nesting deeper than the interface supports
  is a caller error, not a condition to absorb silently. A disabled scope past
  its own nesting ceiling is the one exception that still opens: since
  Ui_BeginDisabled reports nothing a caller could react to, the level past
  capacity is conservatively treated as disabling rather than dropped, so a
  widget can end up wrongly disabled but never wrongly left reachable.
- **Nested scrolling region** — a scrolling region opened inside another,
  including a list box inside either, is refused and reported once; the inner
  one is not opened. Not a depth limit like the clip and scope stacks above: one
  level is the limit regardless of how much of either budget remains.
- **Interaction state store full** — reported once for the frame. Widgets past
  the ceiling still draw and still respond to focus and pointing; they lose only
  what they would have remembered between frames, so a screen degrades rather
  than breaks.
- **Font unavailable** — reported once, naming which of the reasons applies, and
  the built-in font is used. Never a failure to start.
- **Theme refused** — reported once, naming the check that failed and the value
  that failed it. The live theme is unchanged, which is the property that makes
  a bad theme file survivable rather than fatal.
- **No pointing device and no directional input** — the interface still draws and
  is simply not navigable. It is never a failure to report.
- **Container budget exhausted** — a widget drawn inside an open `Ui_BeginBudget`
  scope past the cap that scope was given is dropped and reported once when
  the scope closes, with a count; the widgets before the cap was reached still
  drew. This is a narrower version of *Buffer exhausted* above, scoped to one
  block rather than the whole frame, for a caller that wants a single runaway
  list to degrade on its own instead of spending the entire screen's budget.

## Limits

- **Text coverage is the printable ASCII range.** Text outside it draws a
  substitute glyph rather than being omitted silently. The interface is not
  localised; strings are drawn as given.
- **No kerning.** Advances are per glyph, taken from the font's own metrics.
- **Layout is a cursor, not a solver.** Widgets stack in the order they are asked
  for, within an explicitly placed container or a named screen region. There is
  automatic wrapping and equal division into columns, but no automatic sizing and
  no constraint solving. The cursor moves on a second axis when a caller asks for
  it: the next widget can be placed beside the previous one instead of below it,
  at an explicit width the caller gives, rather than one the interface computes.
  Widgets placed this way form a navigation run, described under *Navigation is
  grouped* below. A caller may also override just the next widget's width or row
  height directly, with or without sharing a row: a one-shot request, consumed by
  the widget call immediately after it and not retried if the row context changes
  (a fresh panel, scroll region or column) before one arrives. The cursor itself
  may also be moved directly rather than only advanced by the widgets already
  drawn: its vertical position is an absolute jump, ending whatever row is in
  progress exactly as the spacing call already does, while its horizontal
  position for the very next widget is the same one-shot request the width and
  height overrides already are. Neither turns layout into a solver -- both still
  place exactly one widget at exactly the position asked for, nothing more.
- **Text entry has no mid-string caret.** `Ui_TextInput` and `Ui_TextDialog`
  append and backspace from the end of the buffer only; a value cannot be
  edited in the middle without retyping the tail. This holds on every
  mechanism, including the platforms with a real keyboard.
- **At most one `Ui_Image3D` result is live at a time.** It is not a general
  image-loading path with many concurrent slots the way `Ui_Image` over
  Resource is; see Renderer's own limit on `RenderToImage3D`.
- **At most one dialog or text field is open at once.** Opening a second while
  one is already open abandons the first rather than queuing it, the same
  restriction Vita's own dialog service and Win32's blocking `MessageBox`
  already impose; this interface does not relax it for the platforms that
  could support more.
- **No animation.** Nothing moves, fades or eases; a value changes between one
  frame and the next. A notification appears and disappears rather than sliding.
- **No nested scrolling containers**, and a scrolling region may not contain a
  column set. One level of each is what the layout cursor supports. A list box
  is a scrolling region under its own name, so the same limit reaches it: one
  cannot be placed inside another scrolling region either, and the attempt is
  refused and reported the same way. A table is not a scrolling region itself
  and nests inside one freely.
- **No drag-and-drop, and no movable, overlapping or dockable windows.** This is
  a decision, not a gap: containers are placed explicitly because a window the
  player must drag is unusable with a pad at television distance, and because
  overlapping windows would make draw order something the interface decides
  rather than something the caller can read off its own calls. A menu bar is
  not an exception to this: it docks to the top of a frame and nowhere else,
  which is placement, not the free movement the decision excludes. An open
  menu's items draw in the overlay layer -- one layer, concatenated after the
  base one in call order within itself, exactly like a modal or a toast -- so
  the menu's own draw order is still something the calls that built it
  determine, not something the interface is deciding on its own.
- **Submenus are not supported.** A menu opened from inside another menu's
  items is refused. One level is what exists today.
- **One font in use at a time per text role**, and role assignment is part of the
  theme rather than a per-call choice. A theme may also name a `Default` font,
  its own fallback tried before a role-specific font is found empty and before
  the built-in font; a theme that names no fonts at all inherits the default
  theme's fonts the same way it inherits unset colours and metrics.
