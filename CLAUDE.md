# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

OStrands is an SKSE plugin for Skyrim SE/AE (`0_OStrands.dll`, C++23 on CommonLibSSE-NG). During OStim and SexLab scenes it draws procedural fluid strands and drips, and it runs a particle fluid simulation for semen. It hooks into OStim, SexLab (plus SexLab P+ / Accurate Penetration), OCum, SexLab Aroused (and its NG C API) and SKSE Menu Framework, and it has to work whether or not each of them is installed.

## Build & package

There is no test suite and no linter. To verify a change, build it and run it in game, then check `Documents/My Games/Skyrim Special Edition/SKSE/OStrands.log` (set `DebugLog = true` in the ini to get the detailed per-scene and per-frame logging).

- The build needs CommonLibSSE-NG cloned into `extern/` first. This folder is gitignored, and CMake stops with an error if it's missing:
  `git clone --depth 1 --branch v12.1.0 https://github.com/alandtse/CommonLibSSE-NG extern/CommonLibSSE-NG`
- `build.cmd` finds Visual Studio with vswhere, sets up vcvars64, falls back to VS's bundled vcpkg when `VCPKG_ROOT` is unset, then runs `cmake --preset release` and `cmake --build --preset release`. The output is `build/release/0_OStrands.dll`. Only the `release` preset exists (Ninja, `x64-windows-static-md`).
- `package.cmd` (which runs `tools/package.py`) zips the DLL, everything under `data/SKSE` (the ini), the license and the generated notice into `dist/OStrands <version>.zip`. The meshes and textures in `data/` are deliberately not shipped: the fork changes only the plugin, so players get them from the original OStrands of the same upstream version. It reads the version from `project(OStrands VERSION ...)` in `CMakeLists.txt`. Bump `vcpkg.json`'s `version` at the same time.
- **Versioning.** This is a fork that is rebased onto each upstream OStrands release, so versions are `upstream.version.fork-revision`, for example `0.8.0.2`. The first three parts always mirror the upstream release. The fourth part counts this fork's releases on top of it, and goes back to 1 when upstream releases a new version (upstream 0.8.1 → `0.8.1.1`). Tag releases `v<version>` and add a changelog entry to the README. The first creature release was tagged `v0.8.0-creatures` before this scheme existed, and counts as 0.8.0.1.
- New `.cpp` files have to be added by hand to the `SOURCES` list in `CMakeLists.txt`. There is no glob.

## Architecture

**Threading model (the key thing to understand).** `Hooks.cpp` installs a `FrameDriver` input-event sink. On each input event it queues one SKSE main-thread task, and that task runs the whole per-frame pipeline in order: `Body::BeginFrame` → `SexLabBridge::Update` → `Arousal::Update` → `StrandManager::Update` → `FluidManager::Update`, then hotkeys. The driver skips frames while the main menu is open, the game is paused, or the RaceSex menu is open. OStim listeners, mod events and other callbacks arrive on other threads. They never touch scene-graph state directly. Instead they push into a mutex-guarded pending queue (`StrandManager::SetPairs` / `_threads`, `FluidManager::_pending` events), and `Update` applies that queue on the main thread. `Hooks::ResetSession` (on load, new game and main menu) clears every subsystem.

**Startup** (`main.cpp`): settings load at plugin load. Hooks go in at `kInputLoaded`. The OStim bridge and the settings menu connect at `kPostPostLoad`. OCum, SexLab and Arousal connect at `kDataLoaded`, because they need plugin and form lookups. If the legacy `OStrands.dll` (from 0.7.0 or earlier) is present, the plugin disables itself.

**Scene input → `Pair`s.** A `Pair` is giver + receiver + `Act` (defined in `Anatomy.h`). The bridges translate each framework's scene into pairs:
- `OStimBridge` maps OStim action names to `Act` through a table built in code and overridable from the ini's `[Actions]` section, and pushes pairs for each thread ID.
- `SexLabBridge`, `OStimBridge` and `Arousal` include creatures when `[Creatures] Enabled` is on. `SexLabBridge` has no action data. It polls and uses `AutoPairs::Detector` to work out pairs from geometry, or the Accurate Penetration DLL API when that is loaded. It also reads P+ orgasm counts to trigger climaxes. SexLab thread IDs are offset by `kThreadBase = 0x10000` so they don't collide with OStim's.
- `OCumBridge` forwards OCum's `ocum_play_cum_shoot_effect` mod event as `FluidManager::Load`. `Arousal` polls SLA faction rank or the NG `SLA_GetArousal` export, and feeds `StrandManager::SetAroused` for wetness and precum drips.

**`Anatomy`** finds openings, shafts, mouth, vulva and fingers by skeleton bone name (the `Names` struct) and detects schlongs and strap-ons. Always get the giver's shaft through `Anatomy::ShaftOf`, never by looking up `NPC Genitals01-06` directly. `ShaftOf` returns a cached chain of bones from base to tip, plus the bones' forward axis, tip extension and radius. It tries each creature rig in `Settings::rigs` (the built-in list, overridable in the ini's `[CreatureRigs]`) then, for a creature, the chain read off the erect penis mesh it wears (`FindMeshShaft`, rig name `mesh`), and falls back to the human `NPC Genitals01-06` chain, which points along +Y. Baka ABC creature chains are animated and point along +X, except the boar's, which points along +Y. A creature counts as having a schlong only while a mesh is skinned to its shaft bones (a soft mesh is skinned to the pelvis), and never when its actor base is female. A rig radius of 0 means the radius measured from the mesh. **`Body`** builds per-actor collision capsules, holes, tips and a `Fluid::Skin` surface grid once per frame. Strands and fluid both collide against these.

**`StrandManager`** handles thin strands (bridges, contact or crotch strands, and drips). It tracks the stroke and contact swing for each pair, spawns strands, and simulates each one as a 10-bone Verlet chain. It poses them by moving the bones of a cloned skinned `strand.nif`, and it also handles glints.

**`FluidManager` + `src/fluid/`** handle semen. Climax events create `Emitter`s (timed pulses) and `Reservoir`s (fluid held inside an opening that leaks out later). Particles live in `Domain`s, which are local simulation spaces tied to a cell and to host actors. Each domain runs `Fluid::Solver`, a position-based fluid solver with cohesion, viscosity, anchors and collision against capsules, the skin and the ground. `Clusters` / `Surface` (a density field) then turn the particles into posed `semen_blob.nif` blobs and 16-bone `semen_rope.nif` ropes. `Settings::semen` is tri-state: `auto` (-1) means on when OCum or SexLab is present.

**Settings** (`Settings.h/.cpp`) are a global struct guarded by a recursive mutex. Always hold `auto guard = Settings::Lock();` while reading or writing it. `Load` reads the shipped `data/SKSE/Plugins/OStrands.ini`, then `OStrands_User.ini` on top. `Save` writes only the values that differ from the defaults into `OStrands_User.ini`, atomically through `AtomicFile.h`. Adding a setting usually means changing all of these together: the field plus its min/max constants in `Settings.h`, `ReadFile` / `Validate` / `Save` in `Settings.cpp`, the default in `OStrands.ini`, and an entry in `SettingsMenu.cpp` (the SKSE Menu Framework pages Strands / Semen / Arousal / Alignment).

## Assets

The meshes and textures in `data/` are generated by the scripts in `tools/` (`make_strand.py`, `make_fluid.py`, `make_collision.py`, `make_glint.py <source.dds>`). These scripts write NIF/DDS binaries directly and need no external tools. Bone counts and bone names are shared between the scripts and the C++: `make_strand.py` `BONES = 10` ↔ `StrandManager::kBones`, and `make_fluid.py` `ROPE_BONES = 16` ↔ `FluidManager::kRopeBones`. Change both sides together and regenerate the asset, or else mesh validation fails at runtime ("is strand.nif from this version?"). Because releases don't ship these assets, never change them (or the bone counts and names above) in the fork: the plugin has to keep working with the original mod's meshes for the same upstream version.

## Conventions

- Match the existing style: tabs, CommonLib naming (`a_` parameters, `_` members, PascalCase methods), `logger::` (SKSE log) for output, and almost no code comments.
- Every integration is optional. Check whether it's present (`LookupModByName`, `GetModuleHandle` / `GetProcAddress`, interface queries) and degrade with a log line instead of failing.
- `extern/ostim` and `extern/SKSEMenuFramework` are vendored third-party headers. Don't edit them.
