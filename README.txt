Z3 - compact C++26/Vulkan triangle terrain sandbox

RUN
  Double-click dist/Z3.exe. The build delivers one executable, under 10 MB; playing creates save data.
  Copy just Z3.exe anywhere: no scripts, assets, shaders or bundled DLLs are
  needed. Textures, English font, default skin and compiled Vulkan shaders
  are embedded and decoded directly from memory, without asset extraction.
  GLFW, winpthread and the C++ runtime are statically linked. The game uses
  the installed Windows/Vulkan graphics driver and opens without a console.
  Saves and imported skins live beside Z3.exe; worlds are stored in worlds/.
  Existing project/AppData saves are copied on first launch when the new
  executable folder has no world data. Move that folder to move your worlds.
  Developers can rebuild with ./build.ps1 or CMake; dist is the delivery.
  GCC 14 uses experimental -std=c++26, not finalized complete C++26 support.

CONTROLS
  WASD move; mouse look; Ctrl sprint; Shift crouch/ledge protection.
  Space jump; hold in water to rise. G equips the starter elytra.
  Jump, then press Space in the air to deploy. G unequips to exit flight.
  R / RMB boosts with unlimited fireworks, using vanilla timed boost equations.
  Flight uses Do a Barrel Roll-style local-axis mouse pitch and roll. Mouse X
  rolls, mouse Y pitches; pitching while banked naturally turns the heading.
  This keeps the earlier mouse-only flight preference. A/D do not yaw in flight.
  Gravity, momentum, lift, drag, collisions and landing use the researched
  Minecraft equations instead of the previous forced-speed arcade mode.
  Banking itself no longer reduces lift. Camera/body orientation is smoothed.
  H toggles third-person free look: mouse orbits independently, while movement,
  flight, hunger, effects and world simulation continue. Head direction stays.
  Enter cycles first-person, rear third-person, front third-person. Cameras
  use a four-metre boom shortened by terrain obstruction checks.
  Esc > Select skin opens a native PNG picker. Standard 64x64 classic/slim
  and legacy 64x32 skins are supported, including modern outer layers.
  Selection is stored locally as player-skin.png and applied immediately.
  Rendering is capped at 60 FPS.
  Esc > Worlds / create opens seeded world creation and saved-world selection.
  Type a number or text seed, or leave blank for a random seed, then click
  Create / open seed. Existing seed worlds are reopened rather than replaced.
  Each world has its own save under worlds/; the original world.save remains.
  The last selected world reopens on startup. Seeds use a deterministic 32-bit
  generator; they do not match Minecraft seed geography.
  E / Tab / I opens/closes inventory. Esc closes inventory or pauses gameplay.
  Gameplay locks the cursor; inventory, pause and focus loss release it.
  Returning focus to gameplay recaptures it. Resume returns from the Esc menu.
  Gameplay shows only crosshair, hotbar, hearts, hunger and active progress bars.
  Esc > Controls opens a selectable controls popup; Back/Esc closes it.
  Eye height eases smoothly between standing, crouching, swimming and elytra.
  1-9 / wheel select hotbar. Esc > Controls shows help; F5 saves; [ / ] change view radius.
  The current continuous triangle terrain has no block breaking/placing.

INVENTORY
  Authentic Minecraft inventory background, item sprites and English bitmap
  font. Minecraft button textures are used for the pause menu.
  Only fireworks and elytra are available; old saves are filtered on load.
  36 storage/hotbar slots, four armor slots, offhand, 2x2 crafting and output.
  Click pick up, merge or swap; right click split or place one.
  Shift click transfers storage/hotbar or equips matching armor.
  1-9 over a slot swaps with hotbar; F over a slot swaps with offhand.
  Double click collects matching stacks. Left/right drag distributes stacks.
  Crafting slots remain in the layout, but the flight-only supply has no recipes.
  Chest accepts elytra or iron chestplate; armor fits its corresponding slot.
  Armor storage/equipment currently does not provide damage reduction.
  Version-4 atomic saves retain world seeds alongside armor/offhand/crafting/cursor, food counters,
  equipment wear and world edits. Version-1/2/3 saves migrate without reset
  using the original seed 2643.

PHYSICS RESEARCH
  Read PHYSICS_RESEARCH.txt for primary evidence, exact constants, numeric
  reference cases, version-specific changes and a full-parity coverage plan.
  The matching official 26.4-snapshot-3 client executable was inspected,
  not just a wiki or another clone. Its download hash was verified.
  Physics now runs at 20 Hz with interpolated camera rendering. Core movement
  uses source-based jump/gravity/friction/water/elytra/firework equations,
  crouching/swimming poses, ledge protection, fall
  damage and hunger/saturation healing. The earlier doubled jump/swim tuning,
  coyote time, jump buffering and heuristic flight cap have been removed.
  This is selected default-rule coverage, NOT certified full Minecraft parity.

WORLD AND PERFORMANCE
  Full-resolution terrain now extends to 80 metres with higher-frequency detail,
  sharper rocky crags and twice the surface texture sampling density.
  Continuous triangle terrain with domain-warped fractal noise, mountain ridges,
  connected drainage channels, floodplains, conifer forests,
  valleys, elevation materials, trees, rocks and static water.
  Continental oceans and shelves transition into beaches and climate-driven
  deserts with dunes/cacti, grassland and mountain terrain. Warm shallow
  seas contain colored branching coral clusters; reef detail is limited to
  nearby chunks. Coral and cacti are cosmetic, like trees.
  Ocean water blends after seabed/coral geometry so reefs are visible.
  Brighter grass/foliage and ambient light; overall sound output reduced 25%.
  Generation changes also affect existing saves; player data remains saved.
  Default view radius is 2,048 metres, four times the previous 512 metres.
  [ / ] adjust radius from 128 to 2,560 metres in 128-metre steps.
  Nearby terrain uses detailed 16-metre chunks; the far horizon uses 128-metre
  patches on a shared coarse grid, with skirts at detail boundaries. Far
  patches now use 32-metre interiors and shared 16-metre boundaries, reducing
  horizon upload cost while retaining the new nearby detail.
  Streaming plans are cached and scheduling work is bounded per frame.
  Vertex storage follows actual mesh complexity instead of a large per-chunk
  allocation. AABB viewport/frustum culling,
  five mesh detail levels, shared boundary vertices and batched background
  generation keep distant terrain cheaper. Vegetation is cosmetic.
  Player collision follows the unit-grid triangle surface, not voxel cubes.
  Legacy block edits remain saved but do not alter this terrain.
  Vulkan depth/textures/fog and mailbox presentation when supported (FIFO
  fallback); MX550 selected on this laptop. GPU fence/acquire waits are bounded
  to keep the event loop processing input during temporary driver stalls.
  Procedural stereo water/footsteps/wind with distance, pan and terrain occlusion.
  Static terrain stays in a 96 MiB device-local arena; uploads are capped at
  3 MiB/frame. When visibility changes, GPU copies compact cached meshes into
  two draw ranges (opaque and water). Unchanged meshes upload no CPU data, and
  stable views reuse the compacted GPU ranges without rebuilding them.
  Separate surface and underground bounds avoid drawing buried terrain above
  ground. A persistent generation-tagged worker pool prioritizes nearby meshes;
  four chunks are integrated per frame and stale world jobs are discarded.
  Each worker has an isolated seed; switching worlds never waits for old terrain jobs.
  Default workers: 2. --workers=N accepts 1 through 12; more workers can compete
  with rendering and are not automatically faster. The horizon remains 2048m.
  Sustained slow frames coarsen distant mesh/vegetation detail with hysteresis.
  Near collision detail is retained. Windows frame pacing targets 60 FPS.
  The world, HUD, menus and map render at native window resolution by default.
  Resolution never drops automatically. --scene-scale=0.5 explicitly selects
  a lower 3D resolution for diagnostic benchmarks; --native-resolution resets it.
  All --smoke tests are muted without opening an audio device.
  --visible --frames=600 can accompany a --smoke preview for a muted, visible
  benchmark. Hidden-window GPU power states can distort performance measurements.
  --profile reports frame timings; completion logs include p50/p95/p99,
  terrain upload bytes, live GPU cache bytes and process working-set memory.
  Short hidden-window runs are not guarantees of sustained gameplay speed.
  Terrain LOD background: NVIDIA GPU Gems 2, chapter 2:
  https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry
  This implementation uses CPU chunk meshes, not GPU geometry clipmaps.
  Folder remains below 100,000,000 bytes including .git, captures and build.
  Recheck with ./tools/check-size.ps1. Toolchains/research jar remain external.

KNOWN GAPS
  No partial block shapes/step algorithm or dynamic fluid levels/flow,
  effect/enchantment modifiers, entity collisions/vehicles, oxygen/drowning,
  damage timers/armor protection, multiplayer or complete vanilla input state.
  Physical positions use floats rather than vanilla doubles. Source-derived
  regression fixtures have not been compared with a running vanilla server.
  Inventory lacks all-item/recipe coverage, recipe book, drop/pickup entities,
  creative menus, other containers and full component/NBT handling.
  Unlimited rocket supply and persistent unrestricted boosts are intentional
  sandbox differences. Inventory is retained on respawn. See research report.

VERIFY
  ./build/z3.exe --test
  ./build/z3.exe --smoke --validate
  ./build/z3.exe --smoke-inventory --validate
  ./build/z3.exe --smoke-flight --validate
  ./build/z3.exe --smoke-pause --validate
  ./build/z3.exe --smoke-controls --validate
  ./build/z3.exe --smoke-capture --validate
  ./build/z3.exe --smoke-live --validate
  ./build/z3.exe --smoke-third-person --validate
  ./build/z3.exe --smoke-freecam --validate
  ./build/z3.exe --smoke-reef --validate
  ./build/z3.exe --smoke-desert --validate
  ./build/z3.exe --smoke-map --validate
  ./build/z3.exe --smoke-gpu-cache --validate
  ./build/z3.exe --smoke-worlds --validate
  ./build/z3.exe --smoke-create-world --validate
  ./build/z3.exe --smoke-roll --validate
  Automated smoke windows are hidden except the native capture check.
  tools/bake_font.py builds the selected snapshot's English bitmap glyph atlas
  plus the inventory background, using Pillow. Runtime font is Minecraft,
  not the previously used Nunito; the old Nunito source/license are retained.

REPOSITORY / ASSETS
  Local shallow sparse clone of:
  https://github.com/InventivetalentDev/minecraft-assets/tree/26.4-snapshot-3
  Upstream: 6394a653aa6bebd69afc1d8591f322d9f6a78163; origin retained.
  Do not fetch all assets/history into this folder under the size constraint.
  Minecraft assets belong to Mojang/Microsoft; upstream does not grant a new
  license. Runtime dependency licenses: licenses/. Nunito license: fonts/OFL.txt.

SHADER REFERENCES AND COVERAGE
  Inspected the matching official snapshot's light.glsl, fog.glsl and
  lightmap.fsh in its verified client jar. This renderer now uses vanilla's
  cylindrical linear render-distance fog, environmental underwater fog,
  directional terrain face brightness, and 0.6 directional / 0.4 ambient
  two-light entity shading with 0.1 alpha cutout. Terrain receives a local
  horizon ambient-occlusion approximation during meshing.
  This is closer vanilla visual behavior, not complete shader parity:
  no propagated block lightmap, dimension effects or animated water.
  Continuous triangle terrain uses sharp dominant-axis texture projection.
  Do a Barrel Roll reference: https://github.com/enjarai/do-a-barrel-roll
  The mod changes orientation/camera and retains vanilla flight physics.

WEATHER AND ROLL DIRECTION
  Mouse-right banks right; mouse-left banks left. Pitching up while banked
  turns toward that bank. This corrects the previously inverted roll input.
  Weather starts with Rain and changes every 180 seconds of session time:
  Rain -> Lightning+Rain -> Lightning+Snow -> Snow -> Rain -> Clear -> repeat.
  All biomes receive the same phase, including deserts. The clock continues
  through menus/focus loss and world changes; restarting starts at Rain.
  Rain/snow particles, overcast sky/fog, lightning bolts/flashes, quiet rain
  ambience and delayed thunder are cosmetic (no terrain accumulation/damage).
  The previous conifer-shaped trees have been restored.

WORLD MANAGEMENT AND RECOVERY
  Esc > Worlds / create supports Open, Rename, Duplicate, Delete and Restore.
  World names are separate from seeds. Duplicates retain geography, explored
  map cells and waypoints; deleted worlds and their maps go to worlds/trash.
  Restore recovers the latest deletion, or an archived save after a world reset.
  --reset-worlds moves existing saves, names and maps to worlds/recovery and
  creates one fresh randomly seeded world. Player skins are retained.

CAVES AND ENTRANCES
  A seeded branching network mixes roughly 12-30m winding passages, occasional
  wider corridors, dead ends, cross-links, loops and 200-300m large chambers.
  Chamber shapes, floor heights and ceiling profiles vary with seeded noise.
  Main passages lie around Y=-1200. Three-turn entrance ramps descend gradually
  through independently stacked volumes; their ends merge into the backbone.
  Roof, floor and wall collisions sample the same fields used for triangle meshes.
  One dry entrance is selected per 2048m region when suitable land exists.
  Circular mouths are 410m across; ravines are 410 by 420m. Raised exposed stone
  rims reach 18m and remain clear of trees. Broad bowls lead into winding ramps.
  Deep, gently sloping chambers can contain bounded underground lakes with
  swimming physics and dry banks. Lava remains separate, glows and burns.
  Stone uses deepslate below Y=0. Nearby actual lava pools cast warm local light.

EXPLORATION MAP
  M opens a north-up shaded terrain map; M or Esc closes it. Gameplay pauses
  and the mouse is released while open. Visited positions reveal a 192m radius,
  including during flight; panning/zooming never reveal unexplored terrain.
  Wheel zooms, left drag pans, Recenter returns to the player. Right click explored
  land to place a named waypoint; use the sidebar to select, rename or delete it.
  Gold marks discovered entrance centres; cyan marks saved waypoints. Player
  heading, position, depth and hovered explored terrain height are displayed.
  Underground positions project onto the surface map, without revealing caves.
  Fog and waypoints save atomically as <world>.save.map beside each world save.
  State is bounded to 4096 sparse 512m exploration blocks, 128 waypoints, and
  16384 cached coloured cells; at most 64 new cell colours are generated per frame.
  No external map images, shader files, scripts or DLLs are needed to play.
