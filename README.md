# OStrands

This mod will create procedural fluid strands for OStim Standalone scenes. An SKSE plugin for Skyrim Special Edition. I feel like scenes are dry, so I've coded this mod. When bodies touch and part during a scene, OStrands draws thin, viscous strands that stretch, thin out, bead and snap, along with passive drips that run down and fall away.

## Features

- **Pull-out strands** between the opening and the shaft on every outstroke: vaginal, anal and blowjobs
- **Crotch strands** between the two bodies as they part after a thrust
- **Drips** that stretch down from the vagina. Women wearing a strap-on drip too
- **Scissoring, cunnilingus, rimming, fingering and masturbation**
- **Toys**, including double-ended dildos from animation packs
- **Semen** when someone with a schlong climaxes: spurting ropes that stretch and snap, a dribble that hangs from the tip, and creampies that stay inside and leak out after pulling out. Vaginal, anal and oral
- **Body collision**, so strands, drips and semen land on bodies, slide down skin and follow them as they move. Semen, strands and drips collide with the actual body mesh, so any body, BodySlide preset or physics setup works. The schlong is measured too, so semen leaves the real tip and runs down the shaft
- Contact detection that calibrates itself to each animation and speed, including very deep thrusts
- More later on. Depends on if I have time or not...

## Requirements

- Skyrim Special Edition  1.6.1170. Other versions are untested. Can work with the 1.7.xx ones
- SKSE64
- Address Library
- Latest OStim Standalone
- A female body and skeleton with genital bones (I only tested with CBBE 3BA)
- Male genitals with the `NPC Genitals01`–`06` bones (tested with The New Gentleman)
- Optional: OCum Ascended. With it, semen turns on by itself and each load is as big as OCum says. Without it, turn semen on in the menu

## Installation

Install the archive with your mod manager. There is no plugin file, no scripts, and nothing is written to your save, so OStrands can be added or removed at any time outside a scene.

The DLL is named `0_OStrands.dll` so it loads before OStim. Older SKSE builds for 1.5.97 only connect add-ons that load before OStim. If you install by hand and are updating from 0.7.0 or earlier, delete the old `Data\SKSE\Plugins\OStrands.dll`.

## Settings

In game, open OStim's MCM and go to the **Addons** page. The **OStrands** group lets you toggle each strand type and set the rates, drip interval, transparency, strand limit and body collision. **Show collision shapes** draws the body shapes, the schlong tip and the openings, so you can check they line up with your body. The **OStrands Semen** group turns semen on or off and tunes the load size, spurts, thickness, stickiness and how long it stays. Changes apply immediately and are saved to `Data/SKSE/Plugins/OStrands.ini`. Everything else, such as thickness, physics and per-act tuning, lives in the INI. After editing it, press **Reload OStrands.ini** in the menu. No need for exiting and rejoining.

### Collision inspection

Semen collides with the actual body. OStrands reads every skinned mesh an actor wears, including the body, hands, feet, head, schlong and any clothes, keeps evenly spaced points with their bone weights, and moves them with the skeleton every frame the same way the game moves the mesh. It follows animations, BodySlide and OBody presets, and jiggle physics. Hair, see-through overlays and inner surfaces such as a separate vaginal canal are left out. Strands and drips collide with the same wrap. When no mesh can be read, OStrands falls back to capsules measured from the body.

Turn on **Show collision shapes** to check it. The console-selected actor is shown when there is one, otherwise the actors in active scenes, or the player. Cyan dots sit on the skin where the wrap is, with a dot and a short line at the schlong tip and a dot at each opening fluid leaks from. Turn it off to play.

With **Debug log** on, `OStrands.log` lists which meshes went into each wrap and why any were left out. **Refit collision shapes** measures everyone again on the next update.

### Animation packs

OStrands recognises scenes by OStim action name and falls back to action tags. This keeps it working with mods that rewrite OStim's action files, such as OSSO. If an animation pack adds its own action names, map them in the `[Actions]` section of the INI:

```ini
someaction = vaginal
```

Valid values: `vaginal`, `anal`, `blowjob`, `cunnilingus`, `rimming`, `fingering`, `analfingering`, `scissoring`, `toy`, `analtoy`, `none`. With `DebugLog = true`, `OStrands.log` lists the actions of every scene that produced no strands.


## Building from source

Requires Visual Studio 2022 or newer (or the Build Tools) with the C++ desktop workload, C++ CMake tools and vcpkg, plus Git and Python 3.

```bat
git clone --depth 1 --branch v12.1.0 https://github.com/alandtse/CommonLibSSE-NG extern/CommonLibSSE-NG
build.cmd
python tools\package.py
```

The plugin is written to `build/release/0_OStrands.dll` and the release archive to `dist/`. The first build compiles CommonLibSSE-NG and takes a while. `tools/make_strand.py` regenerates the strand mesh and textures, `tools/make_fluid.py` the semen meshes and textures, and `tools/make_collision.py` the collision inspector's mesh and textures.

## License

GPL-3.0. `extern/ostim` contains OStim Standalone's plugin interface headers, unchanged and also under GPL-3.0; see `extern/ostim/NOTICE.txt`.

## Credits

- Bethesda
- [OStim Standalone](https://github.com/VersuchDrei/OStimNG) by VersuchDrei and contributors
- [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG)
- SimpleIni and spdlog
## Sloppy sex whatever the heck 
