# SoundVeil — Audio Occlusion on a Budget

Sound behind walls, done properly: a hard per-frame trace budget shared by every source, partial
occlusion from several taps instead of one yes/no ray, attack and release per source, and a counter
box that puts the number for each sound on the screen.

**SoundVeil is not a spatializer and not an audio engine.** It computes no reverb paths and no
diffraction. It sets two values per source — a volume multiplier and a low pass cut-off — that your
audio chain already understands, and stops there. It runs happily next to **PulseScore**: the two
touch different things. PulseScore mixes the stems of one piece of music; SoundVeil damps individual
sources in the room.

## Quick start

1. Add a **SoundVeil Source** component to any actor that has an **Audio Component**.
2. Pick a profile, or leave it empty and the built-in one is used.
3. `SoundVeil.Show 1` in the console to see what the service is doing.
4. `SoundVeil.Bypass 1` and `0` to hear the before and after.

The component does not tick. One service per world ticks, spends its trace budget, and pushes the
values into the audio components that changed.

## Why

Sixty ambient sources on one floor, each with the engine's built-in occlusion, is sixty line traces
every frame — including the forty on the other side of the building the player cannot hear. And the
answer each of those traces gives is a single bit, so a half open door is a wall until the ray happens
to slip through the gap.

SoundVeil caps the whole world at eight traces a frame by default, spends them on the sources that
are near and loud without ever starving the rest, and measures each one with five rays across a small
pattern so four of five blocked reads as **0.8** rather than as "covered".

## Documentation

**Online, free, no account needed:**
<https://wiki.teufel-engineering.com/en/SoundVeil/documentation> — install, profiles, budgets,
console commands, what to switch off in your attenuation settings, limits. The same manual ships with
the plugin as [`Docs/DOCUMENTATION.md`](Docs/DOCUMENTATION.md), so it is also available offline.

[`Docs/Fab-Store-Description.md`](Docs/Fab-Store-Description.md) describes what the plugin is and what
it explicitly is not.

## Requirements

Unreal Engine 5.8 · Windows · one runtime module, no third-party libraries.

---

Copyright 2026 Silvan Teufel. All Rights Reserved.

<!-- SF-STORE-BLOCK:BEGIN -->
## 🛒 Source-available — see before you buy

This repository contains the **full source** of a commercial Unreal Engine plugin. It is **source-available, not open source**: read it, evaluate it, then buy a license to use it. See **the Fab Content License Agreement / Unreal Engine EULA (purchase required)**.

**Get it / Buy:**
- **Buy on Fab** (this plugin): https://www.fab.com/listings/552afc89-7f22-4783-9073-523c74d11139
- Fab store — all our UE5 plugins: https://www.fab.com/sellers/Silvan%20Teufel

### 📬 **Free UE5 Snippet-Pack**

10 ready-to-use C++/Blueprint building blocks (subsystems, versioned saves, async nodes, editor tooling) — MIT licensed. Get it by joining the newsletter — plus a heads-up when something new ships. Double opt-in, unsubscribe in one click, no address sharing.

👉 **[Get the free pack](https://silvan.teufel-engineering.com/newsletter/plugins/?q=gh)**

_© 2026 Silvan Teufel. All rights reserved._
<!-- SF-STORE-BLOCK:END -->
