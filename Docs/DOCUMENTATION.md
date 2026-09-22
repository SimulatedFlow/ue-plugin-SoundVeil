# SoundVeil — Audio Occlusion on a Budget

One service decides, for every playing sound source in a world, how much geometry is between it and
the listener — under a hard cap on traces per frame that every source shares.

Four things separate it from one ray per source:

* **A shared trace budget, round robin.** Eight traces a frame by default, across every source in the
  world. Near and loud sources are measured more often; far and quiet ones less often. A source that
  does not get a turn keeps the value it already had — nothing stalls, it is simply updated less often.
* **Several taps instead of one ray.** Each measurement fires `TapCount` rays (five by default) across
  a small pattern around the line. Four of five blocked is **0.8**, not "covered". A half open door
  sounds like a half open door.
* **Attack and release per source.** The measurement is a target; each source walks towards it over
  `AttackSeconds` / `ReleaseSeconds` (0.15 / 0.35 by default), so somebody crossing the line of sight
  for two frames never reaches the mix as a click.
* **Two knobs, not one switch.** Occlusion drives a **volume multiplier** and a **low pass cut-off**
  separately, both through curves in a profile. Concrete damps differently from a curtain, and that is
  the profile's business, not the code's.

**SoundVeil replaces neither your spatializer nor your audio engine**, and it computes no reverb
paths. It sets two values per source that the existing audio chain already understands.

**It runs next to PulseScore — the two touch different things.** PulseScore mixes the stems of one
piece of music and switches on the beat; SoundVeil damps individual sources in the room according to
geometry. A project can use both at once and they do not interfere.

## Contents

1. [Supported engine and platforms](#supported-engine-and-platforms)
2. [Installation](#installation)
3. [The demo map](#the-demo-map)
4. [Quick start — five minutes](#quick-start--five-minutes)
5. [SoundVeil next to the engine's occlusion: what you must switch off](#soundveil-next-to-the-engines-occlusion-what-you-must-switch-off)
6. [How it works](#how-it-works)
7. [Class and API overview](#class-and-api-overview)
8. [Profiles](#profiles)
9. [Budgets and tuning](#budgets-and-tuning)
10. [Code examples](#code-examples)
11. [Console commands](#console-commands)
12. [Reading the counter box](#reading-the-counter-box)
13. [Automation tests](#automation-tests)
14. [Troubleshooting](#troubleshooting)
15. [Limits](#limits)
16. [Support](#support)

## Supported engine and platforms

| | |
|---|---|
| Engine | Unreal Engine **5.8** |
| Platform | **Win64** (`PlatformAllowList` in the `.uplugin`) |
| Modules | One runtime module, `LoadingPhase: PreDefault` |
| Dependencies | `Core`, `CoreUObject`, `Engine`, `DeveloperSettings`, `AudioMixer`, `AudioExtensions`, `PhysicsCore`, `RenderCore` |
| Third-party code | None |
| Editor-only code | None that ships. The editor viewport counter box is behind `WITH_EDITOR` inside the runtime module. |

### Other platforms

Nothing in the plugin is Windows-specific: it is line traces, curves and `UAudioComponent`. The
allow-list is there because Win64 is what is tested. To try another platform, remove
`PlatformAllowList` from `SoundVeil.uplugin` and rebuild — and test it, because untested is untested.

## Installation

### From Fab

1. Install to your engine version from the Epic Games Launcher.
2. In your project: **Edit → Plugins → Audio → SoundVeil**, tick **Enabled**, restart.

### Into a single project

1. Copy the `SoundVeil` folder into `<YourProject>/Plugins/`.
2. Right-click the `.uproject` → **Generate Visual Studio project files**.
3. Build. The plugin is enabled automatically once it is in `Plugins/`.

### Verifying the install

Play the level, open the console (`~`) and type:

```
SoundVeil.Show 1
```

A counter box appears at the top left. If it says `Sources 0`, nothing has registered yet — see
[Quick start](#quick-start--five-minutes).

### Packaging

Nothing to do. The whole plugin ships: the counter box is drawn on `UCanvas` from `AHUD`, not UMG, so
it survives a cooked Shipping build. `Config/FilterPlugin.ini` lists the extra files that go into a
Fab package.

## The demo map

`Content/SoundVeil/Maps/L_SoundVeilDemo` — a floor plan seen from above, with **161 registered
sources, 43 of them audible**.

Two halves, and the second one exists because a customer asked for it:

- **The four rooms** with a **sliding door** between two of them and an open doorway. This is where
  a single wall, a single door and a single open passage are compared side by side.
- **A corridor with three doors in a row**, leading to a far room that has **another room inside
  it**. This is the cumulative case: a source in the inner room sits behind an inner wall, three
  corridor doors and the outer wall, roughly 40 m from the ears. Open the corridor doors one after
  another and the same source climbs out of the noise floor in four steps.

The **118 silent sources** are the load. They are invisible and their volume is zero, so they cost
exactly what matters — trace budget — without turning the map into noise. They are what makes the
difference between budget 1 and 64 visible in "Longest wait". **F3** unregisters and re-registers
them if you want to see the counter box with and without that load.

Three source sounds are in play at once, so you hear occlusion on different material: a **hum** in
the old rooms, **footsteps** in the corridor, a **gunshot** in the far room and a **two-voice
dialogue** in the inner room. Speech is the one that makes the low pass obvious.

The sources are **coloured columns, and their colour is the occlusion value**: green means an open
line to the listener, red means covered, and everything in between is in between. Occlusion is the
one effect that cannot be photographed by pointing a camera at it, so the demo puts the number onto
the geometry — and the counter box shows the same numbers as text.

**The camera is the listener**, which is the default: with no `SetListenerActor` call, SoundVeil
resolves the listener from `GetPlayerViewPoint()` and ignores the view target in its traces, so the
player's own capsule never blocks a tap. The demo starts you at head height inside the west room and
you walk from there.

> **If you do move SoundVeil's listener, move Unreal's too.** `SetListenerActor` tells *SoundVeil*
> where to measure from. It does not touch Unreal's own audio listener, which stays on the player —
> and Unreal's distance attenuation is applied on top of whatever SoundVeil writes. Put the two in
> different places and the engine can attenuate a source to silence before SoundVeil is ever
> consulted; the counter box will still show healthy occlusion values for a source you cannot hear.
> If you need a fixed listening point, pair `SetListenerActor` with `SetAudioListenerOverride` on the
> player controller so both sets of ears sit in the same place, and check your falloff distances
> against that position — a source 40 m from the ears needs a falloff that reaches 40 m, whatever the
> camera is doing.
>
> A second trap follows from the first: an **overhead camera in a roofless map hears no occlusion at
> all**, because nothing is between it and the sources. That is correct behaviour and it looks like a
> broken plugin. Keep the listener where the walls are.

Every room is a different wall material, which is what a profile is for:

| Room | Profile | What separates it from the listener |
|---|---|---|
| Bottom right | `DA_SoundVeil_Plaster` | nothing — the listener stands here |
| Bottom left | `DA_SoundVeil_WoodenDoor` | the sliding door |
| Top right | `DA_SoundVeil_Curtain` | one wall, with an open doorway through it |
| Top left | `DA_SoundVeil_Concrete` | two walls and a corner |

A panel of buttons along the bottom drives the plugin live — no console needed:

1. **The door.** **Open** and **Close**. The whole left room walks from red to green and back, and the
   counter box's occlusion column walks 1.00 → 0.30 → 0.00 with it. Press **1 (one ray)** and the same
   door only ever produces 1.00 or 0.00 — that is the engine's answer, and the difference is the point
   of the plugin. **5 (profile)** hands the tap count back to each profile.
2. **The budget.** **1**, **8**, **64**, with 33 sources playing. Watch **Traces** and **Longest wait**
   in the counter box: at a budget of 1 the traces line reads `1 / 1 (naive 33)` and the worst wait
   climbs towards 60 frames. Nothing stops, nothing pops — the far columns simply change colour more
   slowly.
3. **Bypass.** **Bypass** and **Apply**. The measuring carries on, so the box still shows what it
   would have done and the columns still change colour; only the applying stops. This is the A/B.

The same three things are on the console as `SoundVeil.Taps`, `SoundVeil.Budget` and
`SoundVeil.Bypass` if you would rather type them.

### Controls

The demo starts in **panel mode**: the mouse cursor is visible and the buttons are one click away.
**Tab** switches to **look mode** — cursor off, free mouse look — and **Tab** switches back.

| Input | Panel mode (start) | Look mode |
|---|---|---|
| **Tab** | → look mode | → panel mode |
| **F3** | the 118 silent load sources off / on | same |
| **C** | drops a curtain where the mouse points; press C on a curtain to take it away | — |
| **R** | turns the curtain the mouse points at by 45 degrees | — |
| **X** | silences the source the mouse points at, and un-silences it | — |
| **Page Up / Page Down** | all audible sources loud / back to normal | same |

Every column carries the name of the sound it plays — **hum**, **beacon**, **rattle**, **steps**,
**shot**, **voice** — so it is obvious which noise comes from where. The 118 load sources are
invisible and silent and carry no label.

**Open** and **Close** drive **every** door in the map, the sliding door in the four rooms as well
as the three in the corridor and the one into the inner room.
| Left mouse | the buttons on the panel | — |
| Mouse move | cursor | free look |
| W A S D, E / Q | move the camera | move the camera |
| `` ` `` | console, for the three commands above | same |

The curtain is a plain thin wall (`BP_SoundVeilDemoCurtain`, 12 x 300 x 310 cm) with collision on
the trace channel and nothing else — put one in a doorway and the sources behind it turn red while
you watch. It is the fastest way to convince yourself that the plugin measures geometry rather than
distance. A doorway in this map is 270 x 300 cm, so one curtain seals it with a little to spare;
**R** turns the curtain in 45-degree steps, which is what you need for the walls that run the
other way.

Moving the camera **moves the ears** — it is the same viewpoint. Step behind a wall and the hum on
the far side drops and dulls; walk through the doorway and it opens up again. Do that before
touching the panel: it is the shortest way to hear what the plugin does. Hold **E** to fly up for
the overhead view of all four rooms at once, but expect it to go open and bright up there — above a
roofless map nothing is in the way, so there is nothing to occlude.

Up to 1.0.0 the demo HUD put the player into *UI Only* input mode, so nothing but the panel
answered at all — no camera, no keys. Since 1.0.1 the panel hands keyboard focus back to the game
viewport after every button press, which is what keeps W A S D and Tab working once you have
clicked something.

The demo's game mode uses a HUD derived from `ASoundVeilHUD`, which is all that is needed for the
counter box to draw in a packaged build; the HUD adds the button panel on top of it.

### What is in the demo folder

| Path under `Content/SoundVeil/` | |
|---|---|
| `Maps/L_SoundVeilDemo` | the map |
| `Blueprints/BP_SoundVeilDemoSource` | column + `UAudioComponent` + **SoundVeil Source**, coloured from `OnOcclusionChanged` |
| `Blueprints/BP_SoundVeilDemoDoor` | the sliding door |
| `Blueprints/BP_SoundVeilDemoListener` | a floor marker; the ears are the camera, this is left as a landmark |
| `Blueprints/BP_SoundVeilDemoHUD` | `ASoundVeilHUD` subclass that also spawns the panel |
| `Blueprints/BP_SoundVeilDemoGameMode`, `BP_SoundVeilDemoViewPawn` | game mode and the free-flying camera |
| `UI/WBP_SoundVeilDemoPanel` | the button panel |
| `Profiles/DA_SoundVeil_*` | concrete, plaster, wooden door, curtain |
| `Audio/S_SoundVeil_*` | three looping ambiences |
| `Materials/M_SoundVeilSource` | the occlusion-coloured column material (`Occlusion` scalar parameter) |

## Quick start — five minutes

### 1. Put a component next to an audio component

On any actor with a `UAudioComponent`, add a **SoundVeil Source** component. That is the whole setup:
it registers the audio component on Begin Play and hands its own volume back on End Play.

If the actor has several audio components, set **Audio Component Name** to pick one. A machine with a
hum and an alarm may well want the hum occluded through concrete and the alarm not at all.

### 2. Pick a profile

Leave **Profile** empty and the project's default is used (and if that is empty too, a built-in
plaster-wall profile). Or make your own: **Content Browser → Miscellaneous → Data Asset →
SoundVeil Profile**.

### 3. See the numbers

```
SoundVeil.Show 1
```

Or set the game mode's HUD class to **SoundVeil HUD**, which shows the box from Begin Play and is what
you want for a screenshot or a packaged build.

### 4. Hear the difference

```
SoundVeil.Bypass 1     // measure but do not apply
SoundVeil.Bypass 0     // apply again
```

### 5. Drive something with the value

The component broadcasts **On Occlusion Changed** whenever the value moves by more than
`OcclusionChangeThreshold`. The demo columns use it to set a material parameter — occlusion is a good
input for anything that should react to "the player cannot really hear this from here".

## SoundVeil next to the engine's occlusion: what you must switch off

**Unreal has its own occlusion, and if you leave it on you will get both.** It lives in the sound's
**Attenuation Settings** and it is exactly the thing SoundVeil is replacing: one trace per source per
frame, on one channel, giving a yes/no answer plus a low pass at a fixed frequency.

Switch it off, per attenuation asset:

1. Open the **Sound Attenuation** asset your sources use (or the attenuation settings inline on the
   sound cue / MetaSound source).
2. Find the **Attenuation (Occlusion)** section.
3. Untick **Enable Occlusion**.

That is all. If you leave it ticked, three things go wrong and none of them is obvious:

* Every source pays for its own trace again, every frame, which is the cost SoundVeil exists to
  remove — and those traces are **not** counted in SoundVeil's budget, because the engine fires them.
* Two low pass filters are applied in series, so a covered source sounds far more muffled than either
  system intended.
* Your A/B is meaningless: `SoundVeil.Bypass 1` stops SoundVeil applying, but the engine's occlusion
  carries on, so "bypassed" still sounds occluded.

**What to keep:** everything else in the attenuation asset. Distance attenuation, spatialization,
air absorption, reverb sends, focus — SoundVeil touches none of it. It only writes the audio
component's volume multiplier and its low pass filter.

**One more thing to watch:** if something else in your project also writes
`SetVolumeMultiplier` on the same audio component, the two will fight. SoundVeil multiplies onto the
volume the component had **at registration**, so a system that sets the volume afterwards will be
overwritten on the next change. Set your own level before registering, or turn off **Apply Volume**
on the component and use `Get Volume Multiplier` yourself.

## How it works

### The round

Every audible source is put into a **scheduling round**. The round contains each source at least
once — that is what bounds the waiting — plus extra turns for the ones that score high on loudness,
nearness and profile priority, up to `MaxTurnsPerSource`. The extra turns are spread across the round
rather than bunched, so a loud source is measured at the start, the middle and the end of a round
rather than three frames in a row.

Each frame the service walks the round from where it left off and spends its trace budget. When the
round runs out it is rebuilt from the current state of the world.

When every source scores the same — forty identical ambient loops, say — the weights collapse to one
turn apiece and the round is plain round robin, which is the right schedule for that case and gives
the shortest possible worst-case wait.

### The budget

`TraceBudgetPerFrame` (default 8) is a hard cap on traces **started** in one frame, for the whole
world. A measurement costs `TapCount` traces. If fewer are left in the frame's ration, the measurement
still happens with what is left rather than being skipped — at one tap it degrades to exactly the
engine's yes/no answer, which is a worse measurement but never a missing one.

So `EvaluationsPerFrame = ceil(Budget / TapCount)`, and no source waits longer than
`ceil(RoundLength / EvaluationsPerFrame)` frames. Both numbers are in the counter box, and the
measured wait is shown against the limit.

The comparison worth making: sixty sources with the engine's occlusion is **sixty traces every
frame**, forever, growing with your level. SoundVeil is **eight**, whatever the level does.

### The taps

A measurement is `TapCount` rays. Tap 0 is the straight centre line — exactly the ray the engine's
occlusion would have fired, which is why one tap is a strict downgrade to that behaviour rather than a
different one. The rest sit on a circle of `PatternRadius` around the source, perpendicular to the
line, with the listener end pushed the opposite way by a third of that.

The ends cross on purpose. A set of parallel rays through a door frame either all pass or all fail,
which is the yes/no answer again with extra cost; crossed rays fan through the opening and count how
much of it is open.

The pattern is deterministic — no random jitter. A jittered pattern makes a stationary source's
occlusion shimmer between measurements, and the smoothing would then be hiding the plugin's own noise
rather than the world's.

### The traces are asynchronous

Taps are fired with `AsyncLineTraceByChannel` and read back on the following frame. A synchronous
trace fired from the audio update blocks the game thread on the physics scene, which is precisely the
cost this plugin exists to remove — firing sixty of them cheaply would be no improvement at all.

The price is one frame of latency on a new measurement, which against attack and release times of
150–350 ms is not audible. If you need the answer inside the same frame you asked, turn off
**Use Async Traces** in the project settings; the budget still holds.

### Smoothing

Each source approaches its target occlusion exponentially, with one time constant for rising
(`AttackSeconds`) and another for falling (`ReleaseSeconds`). The step is always a fraction of the
distance left, so the value can never overshoot — which matters, because an overshoot would push
occlusion past 1 and run the curve lookup off the end of the curve.

Release is longer than attack by default. A door closing should be heard closing; somebody walking
through the line of sight for two frames should not be heard at all.

### Past Max Sources

`MaxSources` (default 128) is how many sources get their own occlusion value. Everything past it is
**not dropped and not silenced**: the extras are grouped, share one value measured from the
best-scoring member of the group, and appear in the counter box as a single collected line. A level
with four hundred ambient loops still sounds right; the four hundredth simply does not get its own
answer.

### What costs nothing

A source that is not playing, is at zero volume, or is beyond its profile's `MaxAudibleDistance` is
left out of the round entirely. It keeps its last value and costs no traces at all. In a large level
this is where most of the saving actually comes from.

## Class and API overview

### `USoundVeilSubsystem` — the service

One per world, ticks itself. `World->GetSubsystem<USoundVeilSubsystem>()`, or
**Get Sound Veil** in Blueprint.

| Function | What it does |
|---|---|
| `RegisterSource(AudioComponent, Profile)` | Put an audio component under the service's care. Returns a handle. |
| `UnregisterSource(Handle)` | Take it out and hand its own volume back. |
| `GetOcclusion(Handle)` | Smoothed occlusion, 0..1. |
| `GetSourceState(Handle, OutState)` | Occlusion, volume, cut-off, distance, wait, tap count. |
| `GetLoudestSources(Count, OutStates)` | What the counter box lists. |
| `GetStats()` | What the service did this frame. |
| `SetBudget(n)` | Move the shared per-frame trace budget. |
| `SetTapCountOverride(n)` / `ClearTapCountOverride()` | Force a tap count on everything. |
| `SetBypass(bool)` | Keep measuring, stop applying. |
| `Freeze(bool)` | Hold every value and stop measuring. |
| `SetListenerActor(Actor)` | Measure from somewhere other than the local player's camera. |
| `DrawStats(Canvas)` | Draw the counter box. One line in your own `AHUD`. |

### `USoundVeilComponent` — the source side

Add next to a `UAudioComponent`. Does not tick.

| Property | Default | What it does |
|---|---|---|
| `Profile` | none | How this source's material sounds. Empty uses the project default. |
| `AudioComponentName` | none | Which audio component on the actor. Empty takes the first. |
| `bAutoRegister` | true | Register on Begin Play. |
| `bApplyVolume` | true | Let the service drive the volume multiplier. |
| `bApplyLowpass` | true | Let the service drive the low pass filter. |
| `OcclusionChangeThreshold` | 0.02 | How far the value must move before the delegate fires. |
| `OnOcclusionChanged` | — | Broadcast when it does. |

`Register()`, `Unregister()`, `IsSourceRegistered()`, `SetProfile()`, `GetOcclusion()`,
`GetVolumeMultiplier()`, `GetLowpassFrequency()`, `GetSourceHandle()`.

> The query is called `IsSourceRegistered`, not `IsRegistered`: `UActorComponent` already has an
> `IsRegistered` and it means something else entirely — whether the component itself is registered
> with the world.

### `USoundVeilProfile` — how a material sounds

A `UPrimaryDataAsset`. See [Profiles](#profiles).

### `USoundVeilStatics` — the library (Blueprint category `SoundVeil`)

The maths, as pure functions with no world: `EvaluateOcclusion`, `BuildTapPattern`, `ScoreSource`,
`RankSources`, `SelectTrackedSources`, `ApplyCurves`, `SmoothOcclusion`, `EvaluationsPerFrame`,
`WaitLimitFrames`. The subsystem calls exactly these — there is no second copy of the maths hiding
inside the tick, which is why the automation tests can hold the real scheduler against a number.

Plus the one-call-per-button half: `SetTraceBudget`, `SetTapCountOverride`, `ClearTapCountOverride`,
`SetBypass`, `SetFrozen`, `SetShowStats`, `GetStats`, `GetSoundVeil`.

### `ASoundVeilHUD` — the counter box

Set it as your game mode's HUD class, or copy the one line out of `DrawHUD` into your own HUD.

### `USoundVeilSettings` — project settings

**Project Settings → Plugins → SoundVeil**, stored in `DefaultGame.ini`. Read once when a world's
service comes up; everything can be moved at runtime afterwards.

## Profiles

One profile per **kind of wall**, not one per sound. Concrete, a curtain and a pane of glass all block
the same rays — what differs is what happens to the sound once they do.

| Property | Default | Notes |
|---|---|---|
| `TraceChannel` | `Visibility` | Give the plugin its own channel to make a grate acoustically open but visually solid. |
| `TapCount` | 5 | Odd numbers keep the pattern symmetric around the centre tap. |
| `PatternRadius` | 60 cm | The smallest opening the measurement can resolve. Roughly a doorway. |
| `MinDistance` | 120 cm | Below this the source runs open — two points that close have geometry between them only because they are almost the same point. |
| `MaxAudibleDistance` | 8000 cm | Past this the source asks for no budget. Set it a little beyond the sound's own attenuation radius. |
| `Priority` | 1.0 | A scheduling weight, 0..1. Never a volume. |
| `AttackSeconds` | 0.15 | Time to rise towards a newly blocked measurement. |
| `ReleaseSeconds` | 0.35 | Time to fall back towards a clear one. |
| `OcclusionToVolume` | 1 → 0.62 → 0.25 | Occlusion in, volume multiplier out. |
| `OcclusionToLowpass` | 20000 → 3500 → 700 Hz | Occlusion in, cut-off out. |

The default volume curve ends at **0.25, not at zero**, because a wall you can still faintly hear
through is what a wall sounds like. Pull the end to zero for a vault door.

Suggested starting points:

| | Volume at full occlusion | Cut-off at full occlusion |
|---|---|---|
| Concrete | 0.12 | 400 Hz |
| Plaster / stud wall | 0.25 | 700 Hz |
| Wooden door | 0.4 | 1400 Hz |
| Curtain | 0.75 | 4500 Hz |
| Glass | 0.5 | 2500 Hz |

## Budgets and tuning

| Setting | Default | What it costs to raise |
|---|---|---|
| `TraceBudgetPerFrame` | 8 | Linear in traces per frame. This is the number. |
| `DefaultTapCount` | 5 | Each tap is a trace. Doubling taps halves the measurement rate at a fixed budget. |
| `MaxTurnsPerSource` | 3 | Lengthens the round, so the worst-case wait grows. |
| `MaxSources` | 128 | Individually tracked sources. The rest share one value. |

Rules of thumb:

* Start at the defaults. Eight traces and five taps is one and a bit measurements a frame, which for
  forty sources means every source is re-measured about three times a second — plenty, because attack
  and release are slower than that anyway.
* If doors feel late, raise the budget before you shorten the attack. A late measurement and a slow
  attack sound the same, but only one of them costs anything to fix.
* If the counter box's **Longest wait** sits at the limit and the limit is large, either the budget is
  too small or too many sources are audible. `MaxAudibleDistance` on the profiles is usually the
  cheaper fix.
* Taps below 3 stop resolving partial openings. Taps above 7 rarely change the answer.

## Code examples

### C++ — occluding an audio component directly

```cpp
#include "SoundVeilSubsystem.h"
#include "Components/AudioComponent.h"

void AMyEmitter::BeginPlay()
{
    Super::BeginPlay();

    if (USoundVeilSubsystem* Veil = GetWorld()->GetSubsystem<USoundVeilSubsystem>())
    {
        VeilHandle = Veil->RegisterSource(AudioComponent, ConcreteProfile);
    }
}

void AMyEmitter::EndPlay(const EEndPlayReason::Type Reason)
{
    if (USoundVeilSubsystem* Veil = GetWorld()->GetSubsystem<USoundVeilSubsystem>())
    {
        Veil->UnregisterSource(VeilHandle);
    }
    Super::EndPlay(Reason);
}
```

### C++ — reacting to the value

```cpp
void AMyEmitter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (const USoundVeilSubsystem* Veil = GetWorld()->GetSubsystem<USoundVeilSubsystem>())
    {
        const float Occlusion = Veil->GetOcclusion(VeilHandle);

        // Dark when covered, bright when open - the demo columns do exactly this.
        DynamicMaterial->SetScalarParameterValue(TEXT("Occlusion"), Occlusion);
    }
}
```

### C++ — the counter box in your own HUD

```cpp
void AMyHUD::DrawHUD()
{
    Super::DrawHUD();

    if (USoundVeilSubsystem* Veil = GetWorld()->GetSubsystem<USoundVeilSubsystem>())
    {
        Veil->DrawStats(Canvas);
    }
}
```

### C++ — moving the budget at runtime

```cpp
// A cut-scene wants every source right, now. Put it back afterwards.
Veil->SetBudget(64);
// ...
Veil->SetBudget(8);
```

### C++ — measuring from somewhere other than the camera

```cpp
// Third person, ears at the character rather than at the camera.
Veil->SetListenerActor(GetPawn());
```

### Blueprint — the same four things

1. **Add Component → SoundVeil Source** on the actor. Done, it registers itself.
2. **On Occlusion Changed** → **Set Scalar Parameter Value** on a dynamic material.
3. A button: **Set Trace Budget** (World Context, 64).
4. Another: **Set Bypass** (World Context, true).

Every node above lives in the **SoundVeil** category and takes a world context, so it can be called
from any Blueprint — actor, widget or game mode — without a reference to the subsystem. The demo's
button panel is exactly this and nothing more: one node per button.

| Button in `WBP_SoundVeilDemoPanel` | Node |
|---|---|
| Budget 1 / 8 / 64 | **Set Trace Budget** |
| Taps 1 | **Set Tap Count Override** (1) |
| Taps 5 (profile) | **Clear Tap Count Override** |
| Bypass / Apply | **Set Bypass** |
| Show the box | **Set Show Stats** |

### Blueprint — the maths without a world

`EvaluateOcclusion`, `BuildTapPattern`, `ScoreSource`, `RankSources`, `SelectTrackedSources`,
`ApplyCurves`, `SmoothOcclusion`, `EvaluationsPerFrame` and `WaitLimitFrames` are static and need no
world at all. They are useful outside the plugin: `ApplyCurves` will give you the same volume and
cut-off a profile would produce for any occlusion value you invent, which is how you audition a
profile's curves against a slider before putting it on a source.

## Console commands

| Command | What it does |
|---|---|
| `SoundVeil.Show 0\|1` | Show or hide the counter box. |
| `SoundVeil.Budget <n>` | Hard cap on traces per frame, shared by every source. |
| `SoundVeil.Taps <n>\|Clear` | Force a tap count on every source. `1` is the engine's own yes/no answer. |
| `SoundVeil.Bypass 0\|1` | Keep measuring, stop applying. The before/after switch. |
| `SoundVeil.Freeze 0\|1` | Hold every value and stop measuring. For screenshots and pause menus. |
| `SoundVeil.Stats` | Write the state of every source to the log. |

## Reading the counter box

```
SoundVeil
Traces           8 / 8   (naive 40)
Sources          40   (40 audible)
Measured         2 this frame
Longest wait     17 frames   (limit 20)
Round length     40
Taps             5 (profile)
Mean occlusion   0.41
Update           0.031 ms
Source            occl   vol    lowpass
BP_Source_12      0.80   0.42    1420 Hz
BP_Source_07      0.20   0.86   12800 Hz
```

* **Traces** — started this frame, against the budget. Never above it. `naive` is what one ray per
  audible source would have cost, which is the number that grows with your level while the left one
  does not.
* **Measured** — sources that got a measurement this frame.
* **Longest wait** — the worst wait in the world right now, against the limit implied by the budget,
  the tap count and the round length. It stays under the limit; if it does not, something is wrong and
  the number turns amber.
* **Round length** — how many turns a full round has. Longer than the source count when loud near
  sources have earned extra turns.
* **Source list** — the loudest sources with their occlusion, volume factor and cut-off. The line
  itself is drawn darker the more occluded the source is, so the box agrees with the coloured columns
  in the demo at a glance.
* **`+n grouped`** — sources past `MaxSources` sharing one value.

## Automation tests

The plugin ships its own tests, compiled only into builds that have automation enabled
(`WITH_DEV_AUTOMATION_TESTS`, so nothing of them reaches a Shipping build). Run them from
**Tools → Session Frontend → Automation**, filter `SoundVeil`, or from a commandlet:

```
UnrealEditor-Cmd.exe <YourProject>.uproject -ExecCmds="Automation RunTests SoundVeil" -unattended -nopause -testexit="Automation Test Queue Empty"
```

| Test | What it pins down |
|---|---|
| `SoundVeil.Maths.EvaluateOcclusionIsAFraction` | Four of five blocked taps is 0.8, not "covered". Zero taps fired is 0, not 1. |
| `SoundVeil.Maths.TapPatternSamplesTheOpening` | Tap 0 is the straight centre line; the rest cross the ends so a doorway is sampled rather than the wall beside it. |
| `SoundVeil.Maths.ApplyCurvesFollowsTheProfile` | The curves are obeyed and the result clamps at both ends. |
| `SoundVeil.Maths.SmoothingIsMonotoneAndDoesNotOvershoot` | Attack and release reach the target monotonically and never pass it. |
| `SoundVeil.Schedule.RanksNearAndLoudFirst` | Near and loud outranks far and quiet, priority breaks the tie. |
| `SoundVeil.Schedule.NoSourceWaitsLongerThanTheLimit` | The starvation bound actually holds: no source waits longer than `ceil(RoundLength / EvaluationsPerFrame)`. |
| `SoundVeil.Schedule.OverflowBecomesOneLineAndLosesNothing` | Past `MaxSources` the extras become exactly one grouped line, and tracked + grouped equals the input count. |

These test the same functions the subsystem calls — there is no second copy of the maths inside the
tick — which is why the third one is worth anything at all.

## Troubleshooting

**The box says `Sources 0`.** Nothing registered. Either no **SoundVeil Source** component has run
Begin Play, or the actors it is on have no `UAudioComponent`. The log says so: search for
`SoundVeil:` warnings.

**Nothing is ever occluded — occlusion stays 0.** The trace channel is the usual cause: the profile
traces on `Visibility` by default, and geometry set to `Ignore` on that channel is invisible to the
measurement. Check the wall's collision preset. `SoundVeil.Taps 1` plus a debug line trace on the same
channel will tell you in a minute.

**Everything is occluded — occlusion stays 1.** Something is blocking every tap. Usually the source's
own actor has collision the traces hit, or the listener's pawn does. The service ignores both the
source's owner and the player's view target; anything else (a vehicle the player is inside, a helmet
mesh) has to be dealt with by giving the plugin its own trace channel.

**It sounds doubly muffled.** The engine's own occlusion is still on in the attenuation asset. See
[what you must switch off](#soundveil-next-to-the-engines-occlusion-what-you-must-switch-off).

**Doors sound like a switch, not a swing.** Tap count is 1, or `PatternRadius` is far smaller than the
gap. `SoundVeil.Taps 5` and a radius near the width of the doorway.

**A source is quiet after unregistering.** Something else wrote its volume multiplier while SoundVeil
owned it. SoundVeil restores the volume the component had at registration, which is the correct
behaviour but will overwrite anything set in between. Turn off **Apply Volume** and apply the value
yourself if two systems must share one component.

**The counter box does not draw in a packaged build.** Set the game mode's HUD class to
**SoundVeil HUD**, or call `DrawStats(Canvas)` from your own HUD. The editor-only viewport path does
not exist in a cooked build.

**Occlusion updates feel slow.** Look at **Longest wait**. If it is at the limit, raise the budget or
cut the number of audible sources. If it is small, the attack and release times are what you are
hearing.

## Limits

* **It is not a spatializer.** No sound paths, no diffraction, no reverb. Two values per source.
* **Line of sight, not sound propagation.** Sound that would realistically travel round a corner or
  through a vent is occluded here, because there is geometry on the straight line. The tap pattern
  softens this; it does not solve it.
* **One listener.** Split screen gets one occlusion value per source, measured from the listener you
  nominate with `SetListenerActor`.
* **Traces read back one frame later** in the default async mode.
* **Grouped sources past `MaxSources`** share one value.
* **No moving-geometry prediction.** A door that swings is measured while it swings, at whatever rate
  the budget allows. That is exactly the intended behaviour, but a very fast door at a very small
  budget will read behind itself.
* **Windows only** as shipped — see [Supported engine and platforms](#supported-engine-and-platforms).

## Support

* Documentation: <https://wiki.teufel-engineering.com/en/SoundVeil/documentation>
* Email: <teufelsilvan@gmail.com>

---

Copyright 2026 Silvan Teufel. All Rights Reserved.
