## OStrands with Creature Support for SexLab and OStim

OStrands is an SKSE plugin for Skyrim SE/AE. It draws procedural strands, drips and semen in OStim and SexLab scenes.

This is Keevan2374's fork of [OStrands by LeonVanWinkle](https://gitlab.com/LilRicefield/ostrands). It adds creature support. The current version is **0.8.0.2**, based on upstream OStrands 0.8.0.

### What this fork changes

- **Creatures in SexLab and OStim scenes.** Creatures stay in the scene alongside humans. Strands, auto-pairing, body collision and semen follow each creature's own penis bones.
- **Creature penis rigs.** A creature skeleton is matched to a rig: a chain of penis bones from base to tip, their forward axis, how far the tip sits past the last bone, and the shaft radius. Rigs for BakaFactory ABC skeletons are built in.
- **Penis bones read from the mesh.** A creature whose skeleton matches no rig gets its penis bones from the erect penis mesh it wears, so other creature packs work too.
- **Erect only.** A creature only has a penis while it wears an erect mesh, such as the aroused meshes Creature Framework swaps in. A soft creature makes no strands, semen or precum.
- **Arousal precum for creatures.** An aroused male creature drips precum outside scenes, the same way people do. This only works if your arousal mod gives creatures an arousal value.

Creatures can only be the giver. Creature receivers and creature mouths aren't supported yet, because creature skeletons don't have the opening and mouth bones OStrands looks for.

The creature support is new and has only been tested in game with some creatures. Turn on `DebugLog` and check `OStrands.log` if something looks off.

### Requirements

- SKSE and Address Library for SKSE Plugins
- The original [OStrands](https://www.nexusmods.com/skyrimspecialedition/mods/195115) of the same upstream version (the first three numbers of this fork's version, currently 0.8.0). This fork ships only the SKSE plugin and its ini; the meshes and textures come from the original.
- OStim Standalone or SexLab (SexLab P+ is supported)
- For creatures: SexLab or OStim with creature animations, creature skeletons with penis bones, and erect creature meshes (for example More Nasty Critters with Creature Framework). The built-in rigs match BakaFactory ABC.

Optional: SKSE Menu Framework (in-game settings), OCum, SexLab Aroused / SLO Aroused NG / OSL Aroused (arousal drips), PPA - Penetration Physics.

### Creature settings

These go in `Data/SKSE/Plugins/OStrands.ini`. Put your own changes in `OStrands_User.ini`, which updates never overwrite.

```ini
[Creatures]
Enabled = true

[CreatureRigs]
; name = penis bones from base to tip | forward axis (x, y or z) | tip extension | shaft radius
troll = TD 2, TD 3, TD 4, TD 5, TD 6, TD 7 | x | 0.1 | 1.1
```

- A skeleton uses the first rig whose bones it has, all of them. If none match, a creature's penis bones are read from the erect penis mesh it wears, so packs without a rig still work.
- A creature only counts as having a penis while it wears an erect mesh skinned to those bones. On its soft mesh it has none.
- Change a built-in rig by repeating its name with new values.
- Remove a built-in rig with `name = none`.
- Add a new rig with a new name.
- A shaft radius of `0` measures the shaft from the worn mesh.

The built-in rigs cover:

| | | | |
|---|---|---|---|
| bear | boar | canine | chaurus |
| deer | draugr | falmer | gargoyle |
| giant | goat | horker | horse |
| mammoth | riekling | sabrecat | skeever |
| troll | vampire lord | werewolf | |

The chaurus rig also covers spiders and reapers, and the giant rig also covers lurkers.

The Creatures toggle is also on the Strands page of the SKSE Menu Framework settings.

### Building

1. Clone CommonLibSSE-NG into `extern/`:
   ```
   git clone --depth 1 --branch v12.1.0 https://github.com/alandtse/CommonLibSSE-NG extern/CommonLibSSE-NG
   ```
2. Run `build.cmd`. It needs Visual Studio's C++ build tools and vcpkg. The plugin is written to `build/release/0_OStrands.dll`.
3. Run `package.cmd` to build the release zip in `dist/`. It contains only the plugin, `OStrands.ini`, the license and a notice; the meshes and textures in `data/` are left out because they come from the original mod.

### Download

Get the latest build from the [releases page](https://gitlab.com/Keevan2374/ostrands/-/releases).

1. Install the original [OStrands](https://www.nexusmods.com/skyrimspecialedition/mods/195115) of the matching upstream version with your mod manager.
2. Install this zip after it, and let it overwrite the original's `0_OStrands.dll` and `OStrands.ini`.

### Versions

Versions follow upstream OStrands with one extra number: `upstream.version.fork-revision`. For example, `0.8.0.2` is this fork's second release based on upstream 0.8.0. When upstream releases a new version, the fork is rebased onto it and the last number goes back to 1, so upstream 0.8.1 becomes `0.8.1.1`.

### Changelog

**0.8.0.6** (2026-11-20, personal build, based on upstream 0.8.0)
- With Anatomica: a finish that starts inside his anus stays in him while the tip is near ([Anatomica] Hold), and only [Anatomica] Overflow (10%) spills out round the shaft - Anatomica lets the rest out later. Before, half spilled at once and the rest sprayed out whenever the animation pulled back, so most of it ended on the floor.
- Its own stickiness / thickness for what Anatomica lets out of him ([Anatomica] OutStickiness 0.9, OutViscosity 70), separate from the schlong's spurts ([Semen] Stickiness); a Colossal Load clings more (x its thickness).
- A Colossal Load is drawn bigger: its globs and ropes are scaled by its thickness (Anatomica's "Strand thickness", or [ColossalLoad] Radius / [Shape] Radius without Anatomica).

**0.8.0.5** (2026-11-20, personal build, based on upstream 0.8.0)
- What is handed to Anatomica is counted in normal loads: a Colossal Load (or a big OCum load) now counts as several, so he holds, gushes and squirts that much more.

**0.8.0.4** (2026-11-20, personal build, based on upstream 0.8.0)
- [CreatureSemen] / [CreatureShape] are now **Colossal Load** ([ColossalLoad]; the old sections are still read and moved on the next save). On by default, with bigger defaults.
- Without Anatomica, a Colossal Load is for creatures only, and only when More Nasty Critters (MoreNastyCritters.esp) is loaded.
- With Anatomica 2.6c+ and [Anatomica] Load on, Anatomica decides who gets one and how big (men by schlong size or a chance per scene; with More Nasty Critters, creatures by their type or group), as multipliers on [Semen] plus an optional colour.

**0.8.0.3** (2026-11-19, personal build, based on upstream 0.8.0)

- `[CreatureSemen]` and `[CreatureShape]`: creatures get their own load size, spurt strength, spurts, thickness, clumping, stretch, colour and strand thickness (menu: Semen > Creatures, Strands > Look).
- `[Semen] Colour`: semen colour (0-255 RGB). Coloured semen uses a tinted copy of `semen.dds` that OStrands writes to `textures\OStrands\Tint` the first time a colour is used.
- `[General] NoVaginaUsesAnus`: when a scene calls for vaginal but the receiver has no vagina, the anus is used.
- `[Anatomica]`: with Anatomica 2.5w or newer the anus position is Anatomica's live hole, loads that go into an Anatomica anus are handed to Anatomica (which lets them back out through OStrands), and Anatomica's per-race cum colours are used.
- Exports for other SKSE plugins: `OStrands_ApiVersion`, `OStrands_Emit(actor, stream, point[3], velocity[3] in units/s, count, rgb[3] or null)`, `OStrands_SemenOn`.

**0.8.0.2** (2026-10-24, based on upstream 0.8.0)
- A creature only has a penis while it wears an erect mesh. A soft creature no longer makes strands, semen or precum.
- A creature whose skeleton matches no rig gets its penis bones from the erect mesh it wears.
- OStim scenes keep creatures too, when `[Creatures] Enabled` is on.
- A rig shaft radius of `0` now measures the shaft from the worn mesh.

**0.8.0.1** (2026-10-23, based on upstream 0.8.0, released as `v0.8.0-creatures`)
- Creatures in SexLab scenes, following each creature's own penis bones.
- Built-in penis rigs for BakaFactory ABC skeletons, configurable under `[CreatureRigs]`.
- Arousal precum for creatures.
- `[Creatures] Enabled` setting and a Creatures toggle in the SKSE Menu Framework settings.

### Credits

- OStrands by LeonVanWinkle (Saint), with contributions from crajjjj.
- Creature support by Keevan2374, with the erect-mesh check, mesh fallback and OStim creatures by crajjjj.
- The built-in creature rigs are based on the BakaFactory ABC overrides that ship with PPA - Penetration Physics.

### License

GNU General Public License v3.0; see `LICENSE`. Release zips include the license and a notice naming the exact source commit.

Source: https://gitlab.com/Keevan2374/ostrands
