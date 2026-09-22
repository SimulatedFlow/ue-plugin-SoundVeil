# SoundVeil — Audio Occlusion on a Budget

**Sound behind walls, done properly.** A hard per-frame trace budget shared by every source, partial
occlusion from several taps instead of one yes/no ray, attack and release per source, and a counter
box that shows you the number for each sound.

---

## The problem

Unreal's built-in occlusion fires **one line trace per source, per frame**, on one channel, and gives
you a single bit plus a low pass at a fixed frequency.

Sixty ambient sources on one floor is sixty traces every frame — including the forty on the other
side of the building the player cannot hear. And one ray cannot describe a half open door: the ray
hits, or it does not. The door is a wall, and then suddenly it is open air.

Between the engine's single ray and the heavy solutions — voxel grids, baked scenes, a whole new
spatializer — there is nothing budgeted. That gap is what this plugin is.

## What SoundVeil does

**One shared budget.** Eight traces per frame across every source in the world, by default. Near and
loud sources are measured more often, far and quiet ones less often, and **nobody starves**: every
source is in the round at least once, so the worst wait is bounded — and the bound is on screen. A
source that does not get its turn keeps the value it had. Nothing stalls, nothing pops.

**Several taps, not one ray.** Five rays across a small pattern around the line. Four of five blocked
is **0.8**, not "covered". A half open door sounds like a half open door — and at one tap it degrades
to exactly the engine's yes/no answer, so you can hear the difference on a key press.

**Attack and release per source.** 0.15 s up, 0.35 s down by default. Somebody walking through the
line of sight for two frames never reaches the mix as a click.

**Two knobs, not one switch.** Occlusion drives a volume multiplier and a low pass cut-off separately,
both through curves in a profile asset. Concrete damps differently from a curtain, and that is the
profile's business, not the code's.

**Numbers you can screenshot.** A `UCanvas` counter box — not UMG, so it survives a packaged Shipping
build — showing traces against budget, sources total and audible, the longest a source has waited, and
per source the occlusion, the volume factor and the cut-off in Hz.

## What it is not

* **Not a spatializer and not an audio engine.** It computes no reverb paths and no diffraction. It
  sets two values per source that your existing audio chain already understands.
* **Not a voxel or baked solution.** Nothing to bake, nothing to precompute — which is why moving
  doors and moving walls work at all.
* **Not a replacement for your attenuation settings.** Distance attenuation, spatialization, focus and
  reverb sends stay exactly as they are. You untick one box — *Enable Occlusion* — and SoundVeil takes
  that job over.
* **It does not fight PulseScore.** The two touch different things: PulseScore mixes the stems of one
  piece of music and switches on the beat; SoundVeil damps individual sources in the room according to
  geometry. Use both.

## What ships

* One runtime module. No editor module, no third-party libraries, no UMG dependency.
* `USoundVeilSubsystem` — the service. `USoundVeilComponent` — one component, no ticking.
  `USoundVeilProfile` — the data asset. `ASoundVeilHUD` — the counter box.
  `USoundVeilStatics` — the maths as pure Blueprint-callable functions.
* Six console commands: `SoundVeil.Show`, `.Budget`, `.Taps`, `.Bypass`, `.Freeze`, `.Stats`.
* A demo map: four rooms, a sliding door, an open doorway and 33 coloured columns whose colour **is**
  their occlusion value, measured from a listener marked on the floor rather than from the camera,
  with buttons for the door, the budget, the tap count and bypass.
* Four profiles — concrete, plaster, wooden door, curtain — and no borrowed assets.
* Automation tests over the scheduler, the tap maths, the curves and the smoothing.
* A full manual: `Docs/DOCUMENTATION.md` and
  <https://wiki.teufel-engineering.com/en/SoundVeil/documentation>

## Requirements

Unreal Engine **5.8** · Windows (Win64) · C++ source included · no external dependencies.

## Support

<teufelsilvan@gmail.com>

---

Copyright 2026 Silvan Teufel. All Rights Reserved.
