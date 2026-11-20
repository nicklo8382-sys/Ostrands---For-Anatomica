## OStrands for Anatomica

OStrands is an SKSE plugin for Skyrim SE/AE. It draws procedural strands, drips and semen in OStim and SexLab scenes.

This is a modified build of [OStrands by LeonVanWinkle (Saint)](https://gitlab.com/LilRicefield/ostrands), with contributions from crajjjj, based on [Keevan2374's fork](https://gitlab.com/Keevan2374/ostrands). It links OStrands to [Anatomica - Anatomical Anus for HIMBO](#anatomica-link) and adds a few settings of its own. The current version is **0.8.0.7**, based on upstream OStrands 0.8.0.

All credit for OStrands itself goes to its original authors - see [Credits](#credits).

### What this build adds

- **Anatomica link.** With Anatomica 2.6 or newer, strands and semen start at his real opening, and a finish inside him is handed to Anatomica: he holds it, it seeps out over time, and he gushes, floods or squirts it out later. A finish stays in him while the tip is near, so it no longer sprays out whenever the animation pulls back.
- **Colossal Load.** A bigger, thicker, heavier finish. Anatomica decides who gets one and how big (by schlong size or a chance per scene), and its globs and ropes are drawn bigger to match.
- **Semen colour.** Any colour, set in the ini or given per man by Anatomica (per race, skin tint or skin rules).
- **Men as the receiver.** When a scene calls for vaginal but the receiver has no vagina, the anus is used.
- **Exports for other SKSE plugins** to add semen of their own.

Without Anatomica, OStrands works as normal; the Anatomica settings simply do nothing.

### Requirements

- SKSE and Address Library for SKSE Plugins
- The original [OStrands](https://www.nexusmods.com/skyrimspecialedition/mods/195115) of the same upstream version (the first three numbers of this version, currently 0.8.0). This build ships only the SKSE plugin and its ini; the meshes and textures come from the original.
- OStim Standalone or SexLab (SexLab P+ is supported)

Optional: Anatomica 2.6+ (for the link), SKSE Menu Framework (in-game settings), OCum, SexLab Aroused / SLO Aroused NG / OSL Aroused (arousal drips), PPA - Penetration Physics.

### Install

1. Install the original OStrands with your mod manager.
2. Install this build after it and let it overwrite the original's `0_OStrands.dll` and `OStrands.ini` (in MO2, put this below OStrands). Anatomica's installer can do this for you with its "OStrands fluids" option.

Put your own changes in `OStrands_User.ini`, which updates never overwrite.

### Settings

These go in `Data/SKSE/Plugins/OStrands.ini` (or `OStrands_User.ini`). Most are also in the SKSE Menu Framework settings.

```ini
[General]
; a scene asks for vaginal but the receiver has no vagina (men): use the anus
NoVaginaUsesAnus = true

[Anatomica]
Enabled = true
; strands and semen start at his real opening
Hole = true
; what goes into an Anatomica anus is given to Anatomica, which lets it out later
HandOver = true
; each man's cum colour comes from Anatomica
Colour = true
; Anatomica decides who gets a Colossal Load and how big
Load = true
; a finish inside him stays in while the tip is near
Hold = true
; share that spills out round the shaft (0-1); Anatomica lets the rest out later
Overflow = 0.1
; what Anatomica lets out of him: how much it clings (0-5) and how thick (0-100)
OutStickiness = 0.9
OutViscosity = 70.0

[ColossalLoad]
; strand thickness for a Colossal Load
Shape = true
Radius = 0.14

[Semen]
; 0-255 red, green, blue
Colour = 236, 233, 222
; glowing semen
Glow = false
GlowCumColour = true
GlowColour = 140, 190, 255
GlowStrength = 1.0

[Spawn]
; strings between the rim and the tip as it slides out (0 = off)
PullStrings = 3
```

How big a Colossal Load is, and who gets one, is set in Anatomica's menu (Integrations page > Colossal Load).

### For other SKSE plugins

- `OStrands_ApiVersion()`
- `OStrands_Emit(actor, stream, point[3], velocity[3] in units/s, count, rgb[3] or null)` - adds semen at a point
- `OStrands_EmitGlow(actor, stream, point[3], velocity[3], count, rgb[3] or null, glow[4] or null)` - the same, glowing (glow: rgb 0-1 and strength)
- `OStrands_SemenOn()` - whether semen is enabled

### Building

1. Clone CommonLibSSE-NG into `extern/`:
   ```
   git clone --depth 1 --branch v12.1.0 https://github.com/alandtse/CommonLibSSE-NG extern/CommonLibSSE-NG
   ```
2. Run `build.cmd`. It needs Visual Studio's C++ build tools and vcpkg. The plugin is written to `build/release/0_OStrands.dll`.
3. Run `package.cmd` to build the release zip in `dist/`. It contains only the plugin, `OStrands.ini`, the license and a notice; the meshes and textures come from the original mod.

### Versions

Versions follow upstream OStrands with one extra number: `upstream.version.revision`. `0.8.0.7` is based on upstream 0.8.0.

### Changelog

**0.8.0.7**
- Strings on pull-out: as the tip slides right out, strings stretch between the rim and the tip, thin and snap (`[Spawn] PullStrings`, 0 = off; Strands page).
- Glowing semen: `[Semen] Glow` - in its own colour or `GlowColour`, with `GlowStrength` (Semen page). With Anatomica 2.6i+, Anatomica can make only some men's glow.
- Beaded strings: strings gather into beads as they stretch thin (`[Shape] Beads`).
- Splats: drops that land on skin flatten and spread (`[Semen] Splats`).
- Thick cords: a Colossal Load comes out in thick, unbroken cords (`[ColossalLoad] Cords`).
- `OStrands_EmitGlow` for other SKSE plugins.

**0.8.0.6**
- A finish that starts inside an Anatomica anus stays in him while the tip is near (`[Anatomica] Hold`), and only `[Anatomica] Overflow` (10%) spills out round the shaft; Anatomica lets the rest out later. Before, most of it ended up on the floor.
- Its own stickiness and thickness for what Anatomica lets out of him (`OutStickiness`, `OutViscosity`), separate from the schlong's spurts.
- A Colossal Load is drawn bigger: its globs and ropes are scaled by its thickness.

**0.8.0.5**
- What is handed to Anatomica is counted in normal loads, so a Colossal Load (or a big OCum load) counts as several.

**0.8.0.4**
- Colossal Load (`[ColossalLoad]`), with Anatomica deciding who gets one and how big, as multipliers on `[Semen]` plus an optional colour.

**0.8.0.3**
- `[Semen] Colour`: semen colour (0-255 RGB), drawn with a tinted copy of `semen.dds` written to `textures\OStrands\Tint`.
- `[General] NoVaginaUsesAnus`.
- `[Anatomica]`: the anus position comes from Anatomica, finishes inside an Anatomica anus are handed to Anatomica, and Anatomica's cum colours are used.
- Exports for other SKSE plugins.

**0.8.0.1 - 0.8.0.2**
- Keevan2374's fork releases; see [its page](https://gitlab.com/Keevan2374/ostrands).

### Credits

- LeonVanWinkle (Saint) for OStrands: the plugin, the strand simulation, and the strand mesh and textures, with contributions from crajjjj.
- Keevan2374 for the fork this build is based on.
- VersuchDrei and the OStim Standalone team for OStim and its plugin interface (GPL v3.0).
- alandtse and the CommonLibSSE-NG maintainers for CommonLibSSE-NG (MIT).
- The SKSE team for the Skyrim Script Extender, and meh321 for Address Library for SKSE Plugins.
- Brodie Thatcher for SimpleIni (MIT) and Gabi Melman for spdlog (MIT).
- Bethesda for Skyrim.

### License

GNU General Public License v3.0; see `LICENSE`. The changes in this build are listed in `NOTICE.txt`.

Original source: https://gitlab.com/LilRicefield/ostrands
