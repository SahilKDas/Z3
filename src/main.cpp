#include "audio.hpp"
#include "world.hpp"
#include "ui.hpp"
#include "save.hpp"
#include "effects.hpp"
#include "terrain_mesh.hpp"
#include "worlds.hpp"
#include "skin.hpp"
#include "weather.hpp"
#include "exploration.hpp"
#include "terrain_worker.hpp"
#include <psapi.h>
#include <chrono>
#include <filesystem>
#include <future>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <set>
static int tests() {
  int failures = 0;
  auto require = [&](bool ok, const char *name) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    if (!ok)
      failures++;
  };
  auto testRoot = std::filesystem::current_path();
  std::filesystem::create_directories("build/world-manager-test");
  std::filesystem::current_path("build/world-manager-test");
  std::filesystem::create_directories("worlds");
  Player managed;
  Edits managedEdits;
  auto managedFile = seedWorldFile(987654);
  save(managed, managedEdits, managedFile);
  renameWorld(managedFile, "Test realm");
  ExplorationMap managedMap;
  managedMap.reveal({8, 0, 8});
  managedMap.add(8, 8);
  managedMap.write(managedFile, managed.seed);
  auto copied = duplicateWorld(managedFile);
  require(worldLabel(managedFile) == "Test realm" && savedWorlds().size() == 2,
          "world rename and duplicate preserve original saves");
  {
    std::ofstream marker("active-world.txt");
    marker << copied.generic_string();
  }
  require(activeWorldFile() == copied,
          "duplicate worlds reopen through a path-based active marker");
  deleteWorld(copied);
  require(!std::filesystem::exists(copied) && savedWorlds().size() == 1,
          "world deletion moves save to recoverable trash");
  require(restoreWorld() == copied && std::filesystem::exists(copied),
          "deleted world restore preserves save and metadata");
  ExplorationMap copiedMap;
  copiedMap.read(copied, managed.seed);
  require(copiedMap.explored(0, 0) && copiedMap.waypoints.size() == 1,
          "world duplicate, delete and restore preserve map fog and waypoints");
  for (auto file : {managedFile, copied}) {
    std::filesystem::remove(file);
    std::filesystem::remove(file.string() + ".name");
    std::filesystem::remove(file.string() + ".map");
  }
  std::filesystem::remove("active-world.txt");
  std::filesystem::current_path(testRoot);
  ExplorationMap survey;
  survey.reveal({-17, 100, -33});
  survey.add(-17, -33);
  survey.waypoints[0].name = "Home";
  require(survey.explored(-2, -3) && !survey.explored(1000, 1000),
          "map reveals nearby negative coordinates and conceals unexplored terrain");
  survey.recenter({-17, 0, -33});
  auto pos = survey.screenAt(42, -75, 1.6f);
  auto back = survey.worldAt(pos.x, pos.y, 1.6f);
  require(std::abs(back.x - 42) < .001f && std::abs(back.z + 75) < .001f,
          "map coordinate projection roundtrip preserves world positions");
  auto count = survey.blockCount();
  survey.centerX = 100000;
  survey.span = 8192;
  require(survey.blockCount() == count && !survey.explored(6250, 0),
          "panning and zooming never reveal land");
  auto surveyFile = std::filesystem::path("build/map-test.save");
  require(survey.write(surveyFile, 1234), "exploration map saves atomically beside world");
  ExplorationMap surveyCopy;
  surveyCopy.read(surveyFile, 1234);
  require(surveyCopy.explored(-2, -3) && surveyCopy.waypoints.size() == 1 &&
              surveyCopy.waypoints[0].name == "Home",
          "map fog and named waypoints survive reopen");
  surveyCopy.read(surveyFile, 4321);
  require(surveyCopy.blockCount() == 0 && surveyCopy.waypoints.empty(),
          "map rejects mismatched world seed");
  {
    TerrainWorker<int> worker(2);
    worker.submit([] { return 42; });
    worker.reset();
    int result = 0;
    bool stale = worker.take(result);
    worker.submit([] { return 7; });
    for (int i = 0; i < 200 && !worker.take(result); i++)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(!stale && result == 7,
            "persistent worker reset rejects obsolete generations and accepts new jobs");
  }
  {
    auto mainSeed = worldSeed;
    TerrainWorker<uint32_t> worker(2);
    worker.submit([] {
      worldSeed = 101;
      return worldSeed;
    });
    worker.reset(false);
    worker.submit([] {
      worldSeed = 202;
      return worldSeed;
    });
    uint32_t result = 0;
    for (int i = 0; i < 200 && !worker.take(result); i++)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(result == 202 && worldSeed == mainSeed,
            "nonblocking world switch rejects old jobs and isolates worker seeds");
  }
  std::filesystem::create_directories("build/reset-test");
  std::filesystem::current_path("build/reset-test");
  auto resetSeed = worldSeed;
  Player oldWorld;
  Edits oldEdits;
  save(oldWorld, oldEdits, "world.save");
  survey.write("world.save", oldWorld.seed);
  {
    std::ofstream skin("player-skin.png");
    skin << "test";
  }
  resetWorldCollection();
  auto replacement = savedWorlds();
  bool recovered = false;
  for (auto &file : std::filesystem::recursive_directory_iterator("worlds/recovery"))
    if (file.path().filename() == "world.save.map")
      recovered = true;
  require(replacement.size() == 1 && replacement[0].label == "New wilderness" && recovered &&
              std::filesystem::exists("player-skin.png"),
          "world reset archives old saves and map data, creates one fresh world, and retains skin");
  auto recoveredWorld = restoreWorld();
  require(recoveredWorld == std::filesystem::path("world.save") &&
              std::filesystem::exists("world.save.map"),
          "world manager restores archived saves and maps after a reset");
  std::filesystem::remove("world.save");
  std::filesystem::remove("world.save.map");
  for (auto &w : replacement) {
    std::filesystem::remove(w.file);
    std::filesystem::remove(w.file.string() + ".name");
  }
  std::filesystem::current_path(testRoot);
  worldSeed = resetSeed;
  bool caveSeen = false, ravineSeen = false, caveCollision = false, lavaSeen = false,
       cavernScale = false;
  for (int gx = -24; gx <= 24; gx++)
    for (int gz = -24; gz <= 24; gz++) {
      float x = gx * 512 + 256, z = gz * 512 + 256;
      auto g = geology(x, z);
      if (!g.active || g.depth < 65)
        continue;
      lavaSeen |= lavaAt(x, z);
      if (g.cave) {
        caveSeen = true;
        auto node = caveNode(gx, gz);
        cavernScale |= node.rx > 100 && node.rz > 90 && g.depth > 150;
        auto roof = geology(x + 20, z);
        Player explorer;
        explorer.smoothTerrain = true;
        explorer.pos = {x + 20, roof.floor + 10, z};
        caveCollision |= roof.roof && !explorer.collides(explorer.pos, {}) &&
                         explorer.collides({x + 20, roof.ceiling - .2f, z}, {});
      } else
        ravineSeen = true;
    }
  require(caveSeen && ravineSeen && lavaSeen,
          "seeded massive caves, deep ravines and lava pools exist");
  require(cavernScale, "large caverns exceed 200 metres across and lie deep underground");
  require(caveCollision, "cave interiors are navigable and ceilings collide");
  bool connectedHorizontal = true, connectedVertical = true, hiddenSky = false,
       variableChambers = false, lakes = false;
  float smallest = 10000, largest = 0;
  auto checkPassage = [&](CaveNode a, CaveNode b, float phase) {
    bool connected = true;
    float vx = b.x - a.x, vz = b.z - a.z, len = std::hypot(vx, vz);
    for (int step = 0; step <= 128; step++) {
      float t = step / 128.f,
            bend = std::sin(t * 3.14159265f) * std::sin(t * 6.2831853f + phase) * 38;
      auto g = geologyLayer(a.x + vx * t - vz / len * bend, a.z + vz * t + vx / len * bend, 0);
      connected &= g.active && g.ceiling - g.floor > 2;
      hiddenSky |= g.roof && g.ceiling < rawTerrain(a.x + vx * t - vz / len * bend,
                                                    a.z + vz * t + vx / len * bend) -
                                             800;
    }
    return connected;
  };
  for (int gx = -5; gx <= 5; gx++)
    for (int gz = -5; gz <= 5; gz++) {
      auto node = caveNode(gx, gz);
      smallest = std::min(smallest, node.rx);
      largest = std::max(largest, node.rx);
      connectedHorizontal &=
          checkPassage(node, caveNode(gx + 1, gz), float(hash(gx, 617, gz) % 31));
      int block = int(std::floor(gx / 4.f));
      if (gx == block * 4 + int(hash(block, 211, gz) % 4))
        connectedVertical &= checkPassage(node, caveNode(gx, gz + 1), float(gz % 13));
      if (node.lake) {
        auto g = geology(node.x, node.z);
        lakes |= caveLakeLevel(node.x, node.z) > g.floor + 3 && !lavaAt(node.x, node.z);
      }
    }
  variableChambers = smallest < 50 && largest > 120;
  require(connectedHorizontal && connectedVertical,
          "winding passages join chamber centres and cross-links continuously");
  require(variableChambers, "seeded chamber dimensions mix ordinary rooms and enormous caverns");
  bool lakeSwimming = false, dryLakeBanks = false;
  for (int gx = -16; gx <= 16 && !lakeSwimming; gx++)
    for (int gz = -16; gz <= 16 && !lakeSwimming; gz++) {
      auto node = caveNode(gx, gz);
      if (!node.lake)
        continue;
      Player swimmer;
      swimmer.smoothTerrain = true;
      swimmer.pos = {node.x, geologyBase(node.x, node.z).floor + .1f, node.z};
      swimmer.tick(.05f, 0, 0, false, false, false, false, {});
      lakeSwimming = swimmer.wet;
      float bankX = node.x + 70;
      dryLakeBanks = caveLakeLevel(bankX, node.z) < -9000 && !lavaAt(bankX, node.z);
    }
  require(lakeSwimming && dryLakeBanks,
          "cave lake water uses swimming physics and leaves a lava-free dry bank");
  require(lakes, "underground lake basins have water above their floor and exclude lava");
  require(hiddenSky, "deep network has hundreds of metres of solid rock above its ceiling");
  auto rareExit = caveExit(0, 0);
  bool entranceCoverage = true, smoothRegionFloors = true;
  for (int bx = -2; bx <= 2; ++bx)
    for (int bz = -2; bz <= 2; ++bz) {
      bool drySite = false;
      for (int x = 0; x < 4; ++x)
        for (int z = 0; z < 4; ++z)
          drySite |= rawTerrain((bx * 4 + x) * 512 + 256, (bz * 4 + z) * 512 + 256) > seaLevel + 6;
      auto entrance = caveExit(bx, bz);
      entranceCoverage &= entrance.valid == drySite;
      if (entrance.valid)
        entranceCoverage &= !geology(entrance.x, entrance.z).roof && entrance.x >= bx * 2048 &&
                            entrance.x < (bx + 1) * 2048 && entrance.z >= bz * 2048 &&
                            entrance.z < (bz + 1) * 2048;
      if (entrance.valid)
        entranceCoverage &=
            !geology(entrance.x + 180, entrance.z).roof &&
            !geology(entrance.x, entrance.z + 180).roof &&
            entranceRim(entrance.x + 240, entrance.z) > .5f &&
            terrain(entrance.x + 240, entrance.z) > rawTerrain(entrance.x + 240, entrance.z) + 9;
      for (int offset = 0; offset <= 2048; offset += 64) {
        float edge = bx * 2048, z = bz * 2048 + offset;
        smoothRegionFloors &=
            std::abs(networkFloor(edge - .01f, z) - networkFloor(edge + .01f, z)) < .1f;
      }
    }
  require(entranceCoverage,
          "every two-kilometre region with a dry chamber site has an open entrance");
  require(smoothRegionFloors, "denser entrance ramps remain continuous across region borders");
  bool entranceSlope = rareExit.valid, rampConnected = true;
  float previousFloor = 100000;
  for (int step = 0; step <= 768; step++) {
    float t = (step + .1f) / 769.f, theta = -1.5707963268f + t * 6 * 3.1415926536f;
    float r = 120 + 105 * t + 3 * std::sin((theta + 1.5707963268f) * 3);
    float x = rareExit.x + std::cos(theta) * r, z = rareExit.z + std::sin(theta) * r;
    auto g = geologyRamp(x, z);
    entranceSlope &= g.active && g.floor <= previousFloor + .1f && g.ceiling - g.floor > 20;
    if (step)
      entranceSlope &= previousFloor - g.floor < 3;
    previousFloor = g.floor;
    auto volume = geologyAt(x, z, g.floor + 1.8f);
    rampConnected &= volume.active && std::abs(volume.floor - g.floor) < 120;
  }
  require(entranceSlope && previousFloor < -1000,
          "winding entrance ramp descends gradually through three turns into deep caves");
  require(rampConnected, "stacked ramp and deep passages keep independent traversable floors");
  const WeatherState sequence[] = {WeatherState::Rain,        WeatherState::ThunderRain,
                                   WeatherState::ThunderSnow, WeatherState::Snow,
                                   WeatherState::RainAgain,   WeatherState::Clear};
  bool weatherOrder = true;
  for (int phase = 0; phase < 6; phase++)
    weatherOrder &= weatherAt(phase * 180.).state == sequence[phase] &&
                    weatherAt(phase * 180. + 179.999).state == sequence[phase];
  require(weatherOrder && weatherAt(1080).state == WeatherState::Rain,
          "weather follows all six exact three-minute phases and repeats");
  require(weatherAt(360).snow && weatherAt(360).thunder && !weatherAt(360).rain &&
              !weatherAt(900).rain && !weatherAt(900).snow,
          "storm snow and clear precipitation flags");
  Weather storm;
  storm.start(360);
  storm.tick(3.1, {8, 120, 8});
  Player weatherView;
  weatherView.pos = {8, 500, 8};
  Camera weatherCamera = cameraFor(weatherView, 1.6f, 512, 0);
  storm.apply(weatherCamera);
  std::vector<Vertex> weatherVertices;
  storm.draw(weatherVertices, weatherCamera, weatherView);
  bool bolt = false;
  for (const auto &vertex : weatherVertices)
    bolt |= vertex.hud == -204;
  require(weatherCamera.environment[2] > 0 && bolt && storm.tick(.5, {8, 120, 8}),
          "thunder snow creates lightning flash, visible bolt and delayed thunder");
  require(weatherVertices.size() <= 3690 && !weatherVertices.empty(),
          "weather precipitation has a bounded mesh budget");
  Weather clearWeather;
  clearWeather.start(900);
  weatherVertices.clear();
  clearWeather.draw(weatherVertices, weatherCamera, weatherView);
  require(weatherVertices.empty(), "clear weather has no precipitation or lightning geometry");
  Player barrel;
  barrel.barrelFlight = true;
  barrel.pos = {8, 120, 8};
  barrel.inventory.chest = {Item::Elytra, 1};
  barrel.deploy();
  Player rightTurn = barrel, leftTurn = barrel;
  rightTurn.mouseLook(.4f, 0, .016f);
  rightTurn.mouseLook(0, .3f, .016f);
  leftTurn.mouseLook(-.4f, 0, .016f);
  leftTurn.mouseLook(0, .3f, .016f);
  require(rightTurn.look().x > 0 && leftTurn.look().x < 0 && rightTurn.roll > 0 &&
              leftTurn.roll < 0,
          "mouse right banks and turns right; mouse left banks and turns left");
  Vec heading = barrel.look();
  barrel.mouseLook(1.57f, 0, .016f);
  require(length(barrel.look() - heading) < .0001f && std::abs(barrel.roll) > 1.5f,
          "barrel roll changes bank without snapping momentum or heading");
  barrel.mouseLook(0, .4f, .016f);
  require(std::abs(barrel.look().x) > .3f &&
              std::abs(dot(barrel.flightRight, barrel.flightForward)) < .0001f,
          "local pitch while banked turns heading with orthonormal orientation");
  for (int i = 0; i < 1000; i++)
    barrel.mouseLook(.04f, .03f, .016f);
  require(std::abs(length(barrel.look()) - 1) < .0001f &&
              std::abs(dot(barrel.flightRight, barrel.flightUp)) < .0001f,
          "unlocked barrel loops remain stable");
  Player vanilla;
  vanilla.pos = {8, 120, 8};
  vanilla.vel = {0, -4, -20};
  vanilla.inventory.chest = {Item::Elytra, 1};
  vanilla.deploy();
  barrel = vanilla;
  barrel.barrelFlight = true;
  barrel.deploy();
  barrel.mouseLook(1.2f, 0, .016f);
  Edits noEdits;
  barrel.tick(.05f, 0, 0, false, false, false, false, noEdits);
  vanilla.tick(.05f, 0, 0, false, false, false, false, noEdits);
  require(length(barrel.vel - vanilla.vel) < .0001f,
          "barrel banking preserves vanilla elytra physics");
  require(skinPixels("fonts/default-skin.png").size() == 16384,
          "native Minecraft skin PNG decoded");
  require(hash(-15, 7, 19) == hash(-15, 7, 19), "deterministic seed");
  bool bounded = true;
  for (int x = -256; x < 256; x += 7)
    for (int z = -256; z < 256; z += 7)
      bounded &= height(x, z) > -2000 && height(x, z) < 206;
  require(bounded, "terrain bounds");
  const uint32_t originalSeed = worldSeed;
  worldSeed = seedFromText("Coral coast");
  const float seededHeight = terrain(71, -89);
  const float seededClimate = climate(71, -89).moisture;
  worldSeed = seedFromText("Coral coast");
  require(terrain(71, -89) == seededHeight && climate(71, -89).moisture == seededClimate,
          "text seed recreates identical terrain and climate");
  worldSeed = seedFromText("Different coast");
  require(std::abs(terrain(71, -89) - seededHeight) > .01f,
          "different seeds generate different geography");
  worldSeed = originalSeed;
  require(seedFromText("0") == 0 && seedFromText("-1") == UINT32_MAX &&
              seedWorldFile(7) != seedWorldFile(8),
          "numeric seeds and separate world save paths");
  Player seededSave;
  seededSave.seed = 1234567;
  std::stringstream seededStorage;
  writeSave(seededStorage, seededSave, {});
  Player seededLoad;
  Edits seededEdits;
  require(readSave(seededStorage, seededLoad, seededEdits) && seededLoad.seed == seededSave.seed,
          "save version four retains world seed");
  Player banked;
  banked.flying = true;
  banked.vel = {0, 0, -20};
  for (int i = 0; i < 20; ++i)
    banked.mouseLook(.08f, .01f, .05f);
  require(banked.roll > .8f && std::abs(banked.yaw - 1.6f) < .0001f && banked.pitch > .19f,
          "mouse movement controls flight pitch, yaw and automatic roll");
  Player leftBank;
  leftBank.flying = true;
  for (int i = 0; i < 20; ++i)
    leftBank.mouseLook(-.08f, 0, .05f);
  require(leftBank.roll < -.8f && leftBank.yaw < 0, "mouse turns bank in the matching direction");
  auto bankCamera = cameraFor(banked, 1.6f, 512);
  auto level = banked;
  level.roll = 0;
  auto levelCamera = cameraFor(level, 1.6f, 512);
  require(std::abs(bankCamera.vp[1] - levelCamera.vp[1]) > .01f,
          "camera horizon follows elytra roll");
  float stoppedYaw = banked.yaw;
  for (int i = 0; i < 20; ++i)
    banked.mouseLook(0, 0, .05f);
  require(std::abs(banked.roll) < .001f && banked.yaw == stoppedYaw,
          "stopping mouse levels wings without autonomous heading drift");
  Player slow, fast;
  slow.flying = fast.flying = true;
  for (int i = 0; i < 30; ++i)
    slow.mouseLook(.04f, 0, 1.f / 30);
  for (int i = 0; i < 120; ++i)
    fast.mouseLook(.01f, 0, 1.f / 120);
  require(std::abs(slow.roll - fast.roll) < .0001f && std::abs(slow.yaw - fast.yaw) < .0001f,
          "mouse banking is independent of render frame rate");
  Player unlimited;
  unlimited.unrestrictedFlight = unlimited.smoothTerrain = true;
  unlimited.inventory.chest = {Item::Elytra, 1, 0};
  unlimited.grounded = unlimited.wet = true;
  require(unlimited.deploy(),
          "unrestricted elytra launches grounded, wet and with zero durability");
  unlimited.pos = {8, -50, 8};
  unlimited.health = 7;
  unlimited.tick(.05f, 0, 0, false, false, false, false, {});
  require(unlimited.flying && unlimited.pos.y == -50 &&
              std::abs(length(unlimited.vel) - 60) < .001f,
          "unrestricted flight bypasses terrain and below-world respawn at triple cruise speed");
  unlimited.firework();
  for (int i = 0; i < 100; ++i)
    unlimited.tick(.05f, 0, 0, false, false, false, false, {});
  require(unlimited.flying && unlimited.rocketTime == 1.5f &&
              unlimited.inventory.chest.durability == 0 && unlimited.health == 7 &&
              std::abs(length(unlimited.vel) - 102) < .001f,
          "triple-speed boosted flight has no timeout, wear or collision damage");
  unlimited.vel = {0, 0, -200};
  unlimited.pitch = -.8f;
  unlimited.tick(.05f, 0, 0, false, false, false, false, {});
  require(length(unlimited.vel) > 200, "dives retain high speed without a flight speed cap");
  unlimited.yaw = unlimited.pitch = 0;
  unlimited.mouseLook(.3f, 2.f, .1f);
  auto loopCamera = cameraFor(unlimited, 1.6f, 2048);
  bool loopFinite = true;
  for (float value : loopCamera.vp)
    loopFinite &= std::isfinite(value);
  require(std::abs(unlimited.yaw - .6f) < .0001f && std::abs(unlimited.pitch) > 1.48f && loopFinite,
          "unrestricted mouse steering has double sensitivity and full pitch loops");
  int oceans = 0, deserts = 0, reefs = 0;
  Vec reefSite{}, desertSite{};
  for (int x = -2048; x <= 2048; x += 32)
    for (int z = -2048; z <= 2048; z += 32) {
      float y = terrain(float(x), float(z));
      oceans += y < seaLevel - 12;
      if (y > seaLevel + 2 && climate(float(x), float(z)).aridity > .55f) {
        ++deserts;
        desertSite = {float(x), y, float(z)};
      }
      if (coralHabitat(float(x), y, float(z))) {
        ++reefs;
        reefSite = {float(x), y, float(z)};
      }
    }
  require(oceans > 20 && deserts > 20 && reefs > 20,
          "continental oceans, dry deserts and warm shallow reef habitats");
  auto reefMesh =
      meshTerrain(int(std::floor(reefSite.x / 16)), int(std::floor(reefSite.z / 16)), 1);
  auto reefFar = meshTerrain(int(std::floor(reefSite.x / 16)), int(std::floor(reefSite.z / 16)), 8);
  int coralVertices = 0;
  for (const auto &v : reefMesh.vertices)
    coralVertices += v.hud == -10 && v.y < seaLevel && v.y > terrain(v.x, v.z) + .2f;
  require(!reefMesh.water.empty() && coralVertices > 0 &&
              reefFar.vertices.size() < reefMesh.vertices.size(),
          "reef coral geometry, separate translucent water and cheaper distant detail");
  Inventory minimal = Inventory::starter();
  bool onlyFlight = true;
  for (const auto &s : minimal.slots)
    if (!s.empty())
      onlyFlight &= s.item == Item::Rocket || s.item == Item::Elytra;
  minimal.slots[9] = {Item::Stone, 64};
  minimal.crafting[0] = {Item::Log, 1};
  minimal.helmet = {Item::Helmet, 1};
  minimal.flightOnly();
  require(onlyFlight && minimal.slots[9].empty() && minimal.crafting[0].empty() &&
              minimal.helmet.empty(),
          "flight-only inventory and existing-save cleanup");
  auto fine = meshTerrain(0, 0, 1), coarse = meshTerrain(1, 0, 8);
  bool finite =
      !fine.vertices.empty() && fine.vertices.size() % 3 == 0 && coarse.vertices.size() % 3 == 0;
  for (const auto &v : fine.vertices)
    finite &= std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
  require(finite && coarse.vertices.size() < fine.vertices.size(),
          "triangle terrain generation and lower distant mesh cost");
  std::map<int, float> fineEdge, coarseEdge;
  for (const auto &v : fine.vertices)
    if (v.hud <= -101 && v.x == 16 && v.z == std::floor(v.z))
      fineEdge[int(v.z)] = v.y;
  for (const auto &v : coarse.vertices)
    if (v.hud <= -101 && v.x == 16 && v.z == std::floor(v.z))
      coarseEdge[int(v.z)] = v.y;
  bool stitched = fineEdge.size() == 17 && coarseEdge.size() == 17;
  for (auto [z, y] : fineEdge)
    stitched &= coarseEdge.contains(z) && std::abs(coarseEdge[z] - y) < .00001f;
  require(stitched, "one-metre boundary stitches adjacent triangle LOD levels");
  auto horizonPatch = meshTerrain(0, 0, 32, 128, 16);
  auto horizonNext = meshTerrain(8, 0, 32, 128, 16);
  std::map<int, float> horizonEdge, nextEdge;
  bool horizonFinite = true;
  for (const auto &vertex : horizonPatch.vertices) {
    horizonFinite &= std::isfinite(vertex.x) && std::isfinite(vertex.y) && std::isfinite(vertex.z);
    if (vertex.x == 128 && std::abs(vertex.y - terrain(vertex.x, vertex.z)) < .001f) {
      auto [entry, inserted] = horizonEdge.emplace(int(vertex.z), vertex.y);
      if (!inserted)
        entry->second = std::max(entry->second, vertex.y);
    }
  }
  for (const auto &vertex : horizonNext.vertices)
    if (vertex.x == 128 && std::abs(vertex.y - terrain(vertex.x, vertex.z)) < .001f) {
      auto [entry, inserted] = nextEdge.emplace(int(vertex.z), vertex.y);
      if (!inserted)
        entry->second = std::max(entry->second, vertex.y);
    }
  require(horizonFinite && horizonPatch.max.x - horizonPatch.min.x == 128 &&
              horizonPatch.vertices.size() + horizonPatch.water.size() <= 1200,
          "large horizon patches cover 128 metres with bounded geometry");
  require(horizonEdge.size() == 9 && horizonEdge == nextEdge,
          "adjacent large horizon patches share matching boundary heights");
  // Coarse cave-mouth cells must retain all solid surface samples.
  bool mouthCovered = rareExit.valid;
  int mouthX = int(std::floor((rareExit.x + 205) / 16));
  int mouthZ = int(std::floor(rareExit.z / 16));
  auto mouthMesh = meshTerrain(mouthX, mouthZ, 16);
  for (int x = 0; x < 16; ++x)
    for (int z = 0; z < 16; ++z) {
      Vec p{float(mouthX * 16 + x) + .37f, 0, float(mouthZ * 16 + z) + .61f};
      auto g = geology(p.x, p.z);
      if (g.active && !g.roof) continue;
      bool covered = false;
      auto cross = [](Vec a, Vec b, Vec p) {
        return (b.x - a.x) * (p.z - a.z) - (b.z - a.z) * (p.x - a.x);
      };
      for (size_t i = 0; i + 2 < mouthMesh.vertices.size(); i += 3) {
        auto at = [&](size_t j) { auto &v = mouthMesh.vertices[j]; return Vec{v.x, v.y, v.z}; };
        Vec a = at(i), b = at(i + 1), c = at(i + 2);
        if (std::abs(cross(a, b, c)) < .00001f) continue;
        float u = cross(a, b, p), v = cross(b, c, p), w = cross(c, a, p);
        covered |= (u >= -.0001f && v >= -.0001f && w >= -.0001f) ||
                   (u <= .0001f && v <= .0001f && w <= .0001f);
      }
      mouthCovered &= covered;
    }
  require(mouthCovered, "coarse entrance boundary preserves solid ground instead of deleting cells");
  Player surfacePlayer;
  surfacePlayer.smoothTerrain = true;
  surfacePlayer.pos = {8, 100, 8};
  const float ground = surfacePlayer.groundAt(surfacePlayer.pos);
  surfacePlayer.pos.y = ground + 5;
  surfacePlayer.fallStart = surfacePlayer.pos.y;
  surfacePlayer.vel = {0, -300, 0};
  surfacePlayer.tick(.05f, 0, 0, false, false, false, false, {});
  require(surfacePlayer.grounded && std::abs(surfacePlayer.pos.y - ground) < .0001f,
          "fast landing on continuous triangle surface without tunnelling");
  bool surfaceJump = true, surfaceFlight = true;
  for (Vec spawn : {Vec{8, 0, 8}, Vec{68.3720474f, 0, -71.376564f}, Vec{1500, 0, -1700}}) {
    Player jumper;
    jumper.smoothTerrain = true;
    jumper.pos = spawn;
    jumper.pos.y = jumper.groundAt(spawn);
    jumper.grounded = true;
    jumper.inventory.chest = {Item::Elytra, 1};
    float takeoff = jumper.pos.y, apex = takeoff;
    for (int tick = 0; tick < 12; ++tick) {
      jumper.tick(.05f, 0, 0, false, tick == 0, false, false, {});
      apex = std::max(apex, jumper.pos.y);
    }
    surfaceJump &= apex - takeoff > 1.2f && apex - takeoff < 1.3f;
    jumper.pos.y = takeoff;
    jumper.vel = {};
    jumper.grounded = true;
    jumper.tick(.05f, 0, 0, false, true, false, false, {});
    jumper.pitch = .65f; // Aim upward for a rocket-assisted takeoff.
    surfaceFlight &= !jumper.grounded && jumper.deploy() && jumper.firework();
    for (int tick = 0; tick < 8; ++tick)
      jumper.tick(.05f, 0, 0, false, false, false, false, {});
    surfaceFlight &= jumper.flying && jumper.pos.y > takeoff + 1.5f;
  }
  require(surfaceJump, "triangle ground jump reaches vanilla apex at varied coordinates");
  require(surfaceFlight, "ground jump deploys elytra and sustains rocket-assisted flight");
  Player observer;
  observer.pos = {0, 100, 0};
  Camera frustum = cameraFor(observer, 1.6f, 256, 0);
  require(inFrustum(frustum, {-1, 99, -12}, {1, 101, -10}) &&
              !inFrustum(frustum, {-1, 99, 10}, {1, 101, 12}) &&
              !inFrustum(frustum, {200, 99, -12}, {202, 101, -10}),
          "frustum includes visible bounds and rejects behind/offscreen terrain");
  Edits e;
  for (int x = -8; x < 24; x++)
    for (int z = -8; z < 24; z++)
      for (int y = 0; y < 96; y++)
        e[{x, y, z}] = y < 10 ? 3 : 0;
  Player p;
  p.pos = {8, 15, 8};
  p.fallStart = 15;
  for (int i = 0; i < 40; i++)
    p.tick(1.f / 20, 0, 0, false, false, false, false, e);
  require(p.grounded && p.pos.y >= 10 && p.pos.y < 10.02, "collision landing and no tunnelling");
  float start = p.pos.y;
  p.tick(1.f / 20, 1, 0, true, true, false, false, e);
  require(std::abs(p.vel.y - 6.664f) < .001f && p.pos.y > start && std::hypot(p.vel.x, p.vel.z) > 1,
          "sprint jump impulse");
  // Vanilla swimming is acceleration-driven; doubled jump/swim was superseded
  // by the user's request for Java Edition parity.
  Edits shore = e;
  for (int x = 6; x < 10; x++)
    for (int z = 6; z < 11; z++)
      for (int y = 10; y < 12; y++)
        shore[{x, y, z}] = 7;
  Player swimmer;
  swimmer.pos = {8, 10.2f, 8};
  swimmer.fallStart = swimmer.pos.y;
  swimmer.tick(.05f, 0, 0, false, true, false, false, shore);
  require(swimmer.wet && std::abs(swimmer.vel.y - .54f) < .0001f,
          "official water jump impulse, drag, and gravity order");
  p.pos = {8, 55, 8};
  p.vel = {0, -3, -14};
  p.flying = true;
  p.inventory.chest = {Item::Elytra, 1};
  p.fallStart = 55;
  for (int i = 0; i < 20; i++)
    p.tick(1.f / 20, 0, 0, false, false, false, false, e);
  require(p.pos.y > 40 && p.flying && std::isfinite(p.vel.y), "elytra lift");
  p.hunger = 0;
  p.saturation = 0;
  p.exhaustion = 5;
  p.tick(1.f / 20, 1, 0, true, false, false, false, e);
  require(p.hunger == 0, "hunger lower bound");
  Player cameraPlayer;
  CameraTransition smoothCamera;
  cameraPlayer.flying = true;
  smoothCamera.update(cameraPlayer, 1.f / 60);
  require(smoothCamera.height < 1.62f && smoothCamera.height > .4f,
          "elytra camera eases without snapping");
  CameraTransition sixtyHz, oneFortyFourHz;
  for (int i = 0; i < 60; i++)
    sixtyHz.update(cameraPlayer, 1.f / 60);
  for (int i = 0; i < 144; i++)
    oneFortyFourHz.update(cameraPlayer, 1.f / 144);
  require(std::abs(sixtyHz.height - oneFortyFourHz.height) < .00001f &&
              std::abs(sixtyHz.height - .4f) < .0001f,
          "camera transition is independent of render frame rate");
  cameraPlayer.flying = false;
  float flightHeight = smoothCamera.height;
  smoothCamera.update(cameraPlayer, 1.f / 60);
  require(smoothCamera.height > flightHeight && smoothCamera.height < 1.62f,
          "standing camera eases back from elytra");
  Player falling;
  falling.pos = {8, 80, 8};
  falling.fallStart = 80;
  falling.tick(.05f, 0, 0, false, false, false, false, e);
  require(std::abs(falling.pos.y - 80) < .00001f && std::abs(falling.vel.y + 1.568f) < .00001f,
          "official free-fall first tick: move before gravity/drag");
  falling.tick(.05f, 0, 0, false, false, false, false, e);
  require(std::abs(falling.pos.y - 79.9216f) < .00002f &&
              std::abs(falling.vel.y + 3.10464f) < .00002f,
          "official free-fall second tick reference");
  Player walker;
  walker.pos = {8, 10, 8};
  walker.grounded = true;
  walker.fallStart = 10;
  walker.tick(.05f, 1, 0, false, false, false, false, e);
  require(std::abs(walker.pos.z - 7.902f) < .00001f && std::abs(walker.vel.z + 1.07016f) < .00001f,
          "official grounded input acceleration and friction reference");
  Player glider;
  glider.pos = {8, 80, 8};
  glider.vel = {0, -4, -20};
  glider.inventory.chest = {Item::Elytra, 1};
  glider.flying = true;
  glider.fallStart = 80;
  glider.tick(.05f, 0, 0, false, false, false, false, e);
  require(std::abs(glider.vel.y + 3.8808f) < .00003f &&
              std::abs(glider.vel.z + 20.19204f) < .00003f,
          "official level-look elytra lift/steering/drag reference");
  Player boosted;
  boosted.pos = {8, 80, 8};
  boosted.inventory.chest = {Item::Elytra, 1};
  boosted.flying = true;
  boosted.fallStart = 80;
  boosted.firework();
  boosted.tick(.05f, 0, 0, false, false, false, false, e);
  require(std::abs(boosted.vel.z + 17.01782f) < .00003f &&
              std::abs(boosted.vel.y + .1764f) < .00003f,
          "official post-travel firework equation reference");
  Player healing;
  healing.pos = {8, 10, 8};
  healing.health = 10;
  healing.saturation = 6;
  healing.fallStart = 10;
  for (int i = 0; i < 10; i++)
    healing.tick(.05f, 0, 0, false, false, false, false, e);
  require(healing.health == 11 && healing.exhaustion == 6,
          "official saturation healing: half-second interval and exhaustion cost");
  Player jumper;
  jumper.pos = {8, 10, 8};
  jumper.fallStart = 10;
  jumper.grounded = true;
  float peak = 10;
  for (int i = 0; i < 20; i++) {
    jumper.tick(.05f, 0, 0, false, i == 0, false, false, e);
    peak = std::max(peak, jumper.pos.y);
  }
  require(std::abs(peak - 11.252203f) < .0001f,
          "official normal jump apex reference: 1.252203 blocks");
  e[{8, 12, 8}] = 3;
  require(solid(block(8, 12, 8, e)), "voxel edits");
  auto mesh = meshChunk(100, 100, {});
  require(!mesh.empty() && mesh.size() % 6 == 0, "chunk mesh faces");
  Inventory bag = Inventory::starter();
  bag.slots[4] = {Item::Apple, 16};
  int apples = bag.count(Item::Apple);
  bag.click(4, true);
  require(bag.cursor.count == 8 && bag.slots[4].count == 8, "inventory split stack");
  bag.click(13, true);
  bag.click(4);
  require(bag.cursor.empty() && bag.count(Item::Apple) == apples && bag.slots[13].count == 1,
          "inventory merge/place conserves items");
  bag.quickMove(13);
  require(bag.slots[13].empty() && bag.count(Item::Apple) == apples,
          "shift transfer conserves items");
  Inventory crafting;
  crafting.crafting[3] = {Item::Log, 2};
  require(crafting.craft() && crafting.cursor.item == Item::Planks && crafting.cursor.count == 4 &&
              crafting.crafting[3].count == 1,
          "2x2 shapeless log recipe conserves ingredients");
  crafting.cursor = {Item::Stone, 1};
  require(!crafting.craft() && crafting.crafting[3].count == 1,
          "incompatible crafting cursor cannot consume ingredients");
  Inventory equipment = Inventory::starter();
  equipment.slots[0] = {Item::Stone, 64};
  equipment.slots[9] = {Item::Helmet, 1, 165};
  equipment.quickMove(9);
  require(equipment.helmet.item == Item::Helmet && equipment.slots[9].empty(),
          "shift click equips matching armor slot");
  equipment.cursor = {Item::Stone, 16};
  equipment.click(37);
  require(equipment.cursor.count == 16 && equipment.helmet.item == Item::Helmet,
          "armor slot rejects unrelated items");
  equipment.distribute({13, 14, 15}, false);
  require(equipment.slots[13].count == 5 && equipment.slots[14].count == 5 &&
              equipment.slots[15].count == 5 && equipment.cursor.count == 1,
          "drag distribution conserves remainder");
  equipment.collectAll();
  require(equipment.cursor.count == 64 && equipment.count(Item::Stone) == 16,
          "double click collection respects stack limit");
  Inventory stickRecipe;
  stickRecipe.crafting[0] = {Item::Planks, 1};
  stickRecipe.crafting[2] = {Item::Planks, 1};
  require(stickRecipe.craft(true) && stickRecipe.count(Item::Stick) == 4 &&
              stickRecipe.crafting[0].empty(),
          "vertical stick recipe and shift craft");
  Player pilot;
  pilot.pos = {8, 200, 8};
  pilot.vel = {0, 0, -10};
  pilot.wet = false;
  require(!pilot.deploy(), "flight requires equipped elytra");
  pilot.inventory.selected = 1;
  require(pilot.inventory.toggleEquipment() && pilot.deploy(), "equip and deploy elytra");
  int rockets = pilot.inventory.count(Item::Rocket);
  Player unboosted = pilot;
  require(pilot.firework() && pilot.inventory.count(Item::Rocket) == rockets && pilot.firework(),
          "unlimited fireworks can refresh boost without consumption");
  for (int i = 0; i < 31; i++) {
    pilot.tick(1.f / 20, 0, 0, false, false, false, false, {});
    unboosted.tick(1.f / 20, 0, 0, false, false, false, false, {});
  }
  require(pilot.rocketTime == 0 && length(pilot.vel) > length(unboosted.vel) + 10,
          "official rocket thrust increases flight speed");
  pilot.inventory.chest.durability = 0;
  pilot.tick(1.f / 20, 0, 0, false, false, false, false, {});
  require(!pilot.flying && !pilot.deploy(), "broken elytra cannot fly");
  Player eater;
  eater.hunger = 8;
  eater.saturation = 0;
  eater.inventory.slots[5] = {Item::Steak, 16};
  eater.inventory.selected = 5;
  int beef = eater.inventory.held().count;
  require(eater.beginEating(), "food can start use when hungry");
  eater.eatTick(.8f, true);
  eater.eatTick(.8f, true);
  require(eater.hunger == 16 && eater.saturation > 0 && eater.inventory.held().count == beef - 1,
          "timed eating consumes food and replenishes hunger");
  eater.beginEating();
  eater.eatTick(.5f, true);
  eater.eatTick(.1f, false);
  require(eater.eatTime == 0 && eater.inventory.held().count == beef - 1,
          "cancelled eating preserves food");
  Player hungry, full;
  hungry.pos = full.pos = {8, 10, 8};
  hungry.grounded = full.grounded = true;
  hungry.fallStart = full.fallStart = 10;
  hungry.hunger = 6;
  for (int i = 0; i < 20; i++) {
    hungry.tick(1.f / 20, 1, 0, true, false, false, false, e);
    full.tick(1.f / 20, 1, 0, true, false, false, false, e);
  }
  require(full.pos.z < hungry.pos.z - .5f, "low hunger disables sprint");
  hungry.exhaustion = 4.1f;
  hungry.saturation = 0;
  hungry.tick(1.f / 20, 0, 0, false, false, false, false, e);
  require(hungry.hunger == 5, "exhaustion consumes hunger after saturation");
  hungry.hunger = 0;
  hungry.health = 12;
  hungry.regen = 3.999f;
  hungry.tick(1.f / 20, 0, 0, false, false, false, false, e);
  require(hungry.health == 11, "starvation damages health");
  eater.inventory.selected = 1;
  eater.inventory.toggleEquipment();
  eater.inventory.slots[4] = {Item::Apple, 16};
  eater.inventory.click(4, true);
  eater.inventory.helmet = {Item::Helmet, 1, 160};
  eater.inventory.offhand = {Item::Rocket, 64};
  eater.inventory.crafting[2] = {Item::Log, 2};
  std::stringstream storage;
  writeSave(storage, eater, e);
  Player restored;
  Edits restoredEdits;
  require(
      readSave(storage, restored, restoredEdits) && restored.inventory.chest.item == Item::Elytra &&
          restored.inventory.cursor.count == 8 && restored.inventory.slots[5].count == beef - 1 &&
          restoredEdits == e && restored.saturation == eater.saturation &&
          restored.inventory.helmet.durability == 160 && restored.inventory.offhand.count == 64 &&
          restored.inventory.crafting[2].count == 2,
      "save roundtrip preserves inventory/equipment/survival");
  std::stringstream legacy("Z3 1\n8 50 8 0 0 16 20\n8 12 8 3\n");
  require(readSave(legacy, restored, restoredEdits) && restored.hunger == 16 &&
              restored.inventory.count(Item::Rocket) == 64 && restoredEdits.size() == 1,
          "legacy save migration supplies starter equipment");
  std::stringstream invalid("Z3 2\n8 50 8 0 0 16 20 5 0 0\n99\n");
  require(!readSave(invalid, restored, restoredEdits) && restored.hunger == 16,
          "invalid save does not partially mutate state");
  const std::filesystem::path saveTest = "build/test-world.save";
  bool atomicSaved = save(eater, e, saveTest);
  eater.hunger = 7;
  atomicSaved = save(eater, e, saveTest) && atomicSaved;
  std::ifstream savedFile(saveTest);
  require(atomicSaved && readSave(savedFile, restored, restoredEdits) && restored.hunger == 7,
          "atomic on-disk save replaces an existing world");
  savedFile.close();
  std::filesystem::remove(saveTest);
  return failures ? 1 : 0;
}
int main(int argc, char **argv) {
  try {
    std::filesystem::path skinCheckPath;
    if (argc == 3 && std::string(argv[1]) == "--skin-check")
      skinCheckPath = std::filesystem::absolute(argv[2]);
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    auto executableFolder = std::filesystem::path(executable).parent_path();
    if (!std::filesystem::exists(executableFolder / "world.save") &&
        !std::filesystem::exists(executableFolder / "worlds")) {
      auto oldFolder = executableFolder.parent_path();
      if (!std::filesystem::exists(oldFolder / "CMakeLists.txt") ||
          !std::filesystem::exists(oldFolder / "world.save")) {
        wchar_t oldLocalData[32768]{};
        GetEnvironmentVariableW(L"LOCALAPPDATA", oldLocalData, 32768);
        oldFolder = std::filesystem::path(oldLocalData) / "Z3";
      }
      if (std::filesystem::exists(oldFolder / "world.save")) {
        for (const char *name : {"world.save", "active-world.txt", "player-skin.png"})
          if (std::filesystem::exists(oldFolder / name))
            std::filesystem::copy_file(oldFolder / name, executableFolder / name,
                                       std::filesystem::copy_options::skip_existing);
        if (std::filesystem::exists(oldFolder / "worlds")) {
          std::filesystem::create_directories(executableFolder / "worlds");
          for (const auto &file : std::filesystem::directory_iterator(oldFolder / "worlds"))
            if (file.is_regular_file() &&
                (file.path().extension() == ".save" || file.path().extension() == ".name"))
              std::filesystem::copy_file(file.path(),
                                         executableFolder / "worlds" / file.path().filename(),
                                         std::filesystem::copy_options::skip_existing);
        }
      }
    }
    std::filesystem::current_path(executableFolder);
    for (int i = 1; i < argc; i++)
      if (std::string_view(argv[i]) == "--test" ||
          std::string_view(argv[i]).starts_with("--smoke")) {
        std::filesystem::create_directories("build");
        break;
      }
    if (argc == 3 && std::string(argv[1]) == "--skin-check") {
      skinPixels(skinCheckPath);
      std::cout << "PASS Minecraft skin PNG\n";
      return 0;
    }
    bool previewCave = false, previewRavine = false, previewMap = false, resetWorlds = false,
         previewCache = false, smokeVisible = false;
    bool validation = false, smoke = false, previewInventory = false, previewFlight = false,
         previewPause = false, previewControls = false, verifyCapture = false, previewThird = false,
         previewFree = false, previewLive = false, profile = false, previewWorlds = false,
         previewRoll = false, previewCreate = false, verifyFreeLook = false, previewFront = false;
    int previewWeather = -1;
    int frameLimit = 0;
    int workerCount = 2;
    float fixedSceneScale = 0;
    std::string previewBiome;
    for (int i = 1; i < argc; i++) {
      std::string arg = argv[i];
      if (arg == "--test")
        return tests();
      if (arg.starts_with("--workers="))
        workerCount = std::clamp(std::stoi(arg.substr(10)), 1, 12);
      if (arg == "--native-resolution")
        fixedSceneScale = 1;
      if (arg.starts_with("--scene-scale="))
        fixedSceneScale = std::clamp(std::stof(arg.substr(14)), .4f, 1.f);
      if (arg == "--visible")
        smokeVisible = true;
      if (arg.starts_with("--frames="))
        frameLimit = std::clamp(std::stoi(arg.substr(9)), 60, 3600);
      if (arg == "--smoke-gpu-cache") {
        previewCache = true;
        smoke = true;
      }
      if (arg == "--reset-worlds")
        resetWorlds = true;
      if (arg == "--smoke-map") {
        previewMap = true;
        smoke = true;
        frameLimit = 360;
      }
      if (arg == "--profile")
        profile = true;
      if (arg == "--validate")
        validation = true;
      if (arg == "--smoke-create-world") {
        smoke = true;
        previewCreate = true;
        frameLimit = 300;
      }
      if (arg == "--smoke-worlds") {
        smoke = true;
        previewPause = true;
        previewWorlds = true;
        frameLimit = 300;
      }
      if (arg.starts_with("--smoke-weather-")) {
        previewWeather = std::stoi(arg.substr(16));
        if (previewWeather < 0 || previewWeather > 5)
          throw std::runtime_error("Weather phase must be 0 through 5");
        smoke = true;
        previewThird = true;
        frameLimit = 300;
      }
      if (arg == "--smoke-cave" || arg == "--smoke-ravine") {
        previewCave = arg == "--smoke-cave";
        previewRavine = !previewCave;
        smoke = true;
        previewFree = true;
        frameLimit = 480;
      }
      if (arg == "--smoke-roll") {
        smoke = true;
        previewFlight = true;
        previewThird = true;
        previewRoll = true;
        frameLimit = 300;
      }
      if (arg == "--smoke-reef" || arg == "--smoke-desert") {
        smoke = true;
        previewFree = true;
        frameLimit = 300;
        previewBiome = arg;
      }
      if (arg == "--smoke") {
        smoke = true;
        frameLimit = 240;
      }
      if (arg == "--smoke-inventory") {
        smoke = true;
        previewInventory = true;
        frameLimit = 240;
      }
      if (arg == "--smoke-flight") {
        smoke = true;
        previewFlight = true;
        frameLimit = 240;
      }
      if (arg == "--smoke-controls") {
        smoke = true;
        previewPause = true;
        previewControls = true;
        frameLimit = 240;
      }
      if (arg == "--smoke-capture") {
        smoke = true;
        verifyCapture = true;
      }
      if (arg == "--smoke-third-person") {
        smoke = true;
        previewThird = true;
        frameLimit = 480;
      }
      if (arg == "--smoke-front") {
        smoke = true;
        previewThird = true;
        previewFront = true;
        frameLimit = 480;
      }
      if (arg == "--smoke-freecam") {
        verifyFreeLook = true;
        smoke = true;
        previewFree = true;
        frameLimit = 480;
      }
      if (arg == "--smoke-live") {
        smoke = true;
        previewLive = true;
        frameLimit = 600;
      }
      if (arg == "--smoke-terrain") {
        smoke = true;
        previewFlight = true;
        previewFree = true;
        frameLimit = 1200;
      }
      if (arg == "--smoke-pause") {
        smoke = true;
        previewPause = true;
        frameLimit = 240;
      }
    }
    if (verifyCapture)
      frameLimit = 60;
    if (resetWorlds && !smoke)
      resetWorldCollection();
    struct TimerResolution {
      TimerResolution() { timeBeginPeriod(1); }
      ~TimerResolution() { timeEndPeriod(1); }
    } timerResolution;
    Renderer renderer(validation, smoke && !verifyCapture && !smokeVisible);
    if (previewCache) {
      auto fixed = meshTerrain(0, 0, 1);
      Player view;
      view.pos = {8, rawTerrain(8, 8) + 20, 30};
      view.pitch = -.5f;
      Camera camera = cameraFor(view, 1.6f, 512);
      std::vector<TerrainDraw> draws{{1, &fixed.vertices}};
      for (int i = 0; i < 8; i++) {
        glfwPollEvents();
        if (i == 4)
          renderer.invalidateTerrain();
        renderer.draw({}, camera, draws, 1);
        bool upload = renderer.uploadedBytes > 0;
        if (upload != (i == 0 || i == 4))
          throw std::runtime_error("Terrain cache did not reuse/invalidate mesh correctly");
      }
      std::cout << "PASS GPU terrain cache: unchanged geometry uploads zero bytes; reset reuploads "
                   "safely; smoke muted\n";
      return 0;
    }
    if (fixedSceneScale > 0)
      renderer.setSceneScale(fixedSceneScale);
    Audio audio(smoke);
    Effects effects;
    Weather weather;
    if (previewWeather >= 0)
      weather.start(previewWeather * 180.);
    Player player;
    Edits edits;
    player.pos.y = float(height(8, 8) + 4);
    player.fallStart = player.pos.y;
    std::filesystem::path activeSave =
        smoke ? std::filesystem::path("world.save") : activeWorldFile();
    if (previewCreate)
      activeSave = "build/smoke-current.save";
    if (!smoke || previewLive)
      load(player, edits, activeSave);
    worldSeed = player.seed;
    player.inventory.flightOnly();
    player.smoothTerrain = true;
    player.unrestrictedFlight = false;
    player.barrelFlight = true;
    player.pos.y = std::max(player.pos.y, player.groundAt(player.pos) + 1);
    player.fallStart = player.pos.y;
    if (previewThird)
      renderer.reloadSkin("fonts/default-skin.png");
    if (previewFlight) {
      player.pos = {8, 120, 8};
      player.pitch = .3f;
      player.inventory.selected = 1;
      player.inventory.toggleEquipment();
      player.deploy();
      player.firework();
    }
    if (previewCave || previewRavine) {
      bool found = false;
      for (int gx = -24; gx <= 24 && !found; gx++)
        for (int gz = -24; gz <= 24 && !found; gz++) {
          auto node = caveNode(gx, gz);
          float x = previewCave ? node.x : gx * 512 + 256,
                z = previewCave ? node.z : gz * 512 + 256;
          auto g = previewCave ? geologyBase(x, z) : geology(x, z);
          if (g.active && g.cave == previewCave && g.depth > 65 &&
              (!previewCave || (g.roof && g.ceiling - g.floor > 65))) {
            player.pos = {x, g.floor + 8, z};
            player.pitch = 0;
            player.roll = 0;
            player.yaw = 1.5707963f;
            found = true;
          }
        }
      if (!found)
        throw std::runtime_error("No cave/ravine found for preview");
    }
    if (!previewBiome.empty()) {
      bool found = false;
      for (int x = -1024; x <= 1024 && !found; x += 16)
        for (int z = -1024; z <= 1024 && !found; z += 16) {
          float y = terrain(float(x), float(z));
          bool suitable = previewBiome == "--smoke-reef"
                              ? coralHabitat(float(x), y, float(z))
                              : y > seaLevel + 4 && climate(float(x), float(z)).aridity > .65f;
          if (suitable) {
            player.pos = {float(x), std::max(y, seaLevel) + 2, float(z)};
            found = true;
          }
        }
      std::cout << "Biome preview " << previewBiome << " at " << player.pos.x << "," << player.pos.z
                << '\n';
    }
    if (!smoke || verifyCapture || smokeVisible) {
      glfwFocusWindow(renderer.window);
      SetForegroundWindow(glfwGetWin32Window(renderer.window));
    }
    struct Key {
      int first, second, size = 16;
      bool operator<(const Key &b) const {
        return std::tie(first, second, size) < std::tie(b.first, b.second, b.size);
      }
    };
    std::map<Key, TerrainChunk> chunks;
    using Batch = std::vector<std::pair<Key, TerrainChunk>>;
    TerrainWorker<Batch> work(workerCount);
    std::set<Key> queued;
    uint64_t meshSerial = 0;
    int detailPenalty = 0;
    double qualityClock = 0, qualityMs = 16.67;
    std::vector<TerrainDraw> terrainDraws;
    int radius = 128;
    std::vector<std::pair<Key, int>> terrainPlan;
    std::set<Key> wanted;
    int plannedX = INT32_MIN, plannedZ = INT32_MIN, plannedRadius = 0;
    size_t planCursor = 0;
    bool freecam = previewFree, thirdPerson = previewThird;
    int thirdMode = previewFront ? 2 : previewThird ? 1 : 0;
    Player freeView = player;
    freeView.flying = false;
    freeView.roll = 0;
    freeView.pos = player.pos + Vec{0, previewFree ? 40.f : 1.62f, 0};
    freeView.pitch = previewFree ? -.35f : player.pitch;
    if (!previewBiome.empty()) {
      freeView.pos = player.pos + Vec{0, 8, 10};
      freeView.pitch = -.65f;
    }
    if (previewCave || previewRavine) {
      freeView = player;
      freeView.roll = 0;
      freeView.barrelFlight = false;
      freeView.flying = false;
      freeView.pos = player.pos + Vec{0, 4, 0};
      freeView.pitch = -.08f;
      if (previewRavine) {
        freeView.pos = player.pos + Vec{-100, 0, 90};
        freeView.pos.y = rawTerrain(player.pos.x, player.pos.z) + 100;
        freeView.yaw = .84f;
        freeView.pitch = -.8f;
      }
    }
    Player lastView = player;
    Vec lastCameraEye = player.pos + Vec{0, 1.62f, 0};
    ExplorationMap worldMap;
    worldMap.read(activeSave, player.seed);
    worldMap.reveal(player.pos);
    worldMap.recenter(player.pos);
    bool mapOpen = previewMap;
    bool captured = false, paused = previewPause, controlsOpen = previewControls,
         inventoryOpen = previewInventory;
    std::string notice;
    float noticeTime = 0;
    auto message = [&](std::string s) {
      notice = std::move(s);
      noticeTime = 4;
    };
    auto toggleEquipment = [&] {
      if (player.inventory.toggleEquipment())
        message(player.equipped() ? "Elytra equipped - Space or RMB launches flight"
                                  : "Elytra unequipped");
      else
        message("Select an elytra to equip; leave space to unequip");
    };
    double mx = 0, my = 0;
    glfwGetCursorPos(renderer.window, &mx, &my);
    struct CursorGuard {
      bool owned = false;
      ~CursorGuard() {
        if (owned)
          ClipCursor(nullptr);
      }
    } cursorGuard;
    auto syncCursor = [&] {
      bool focused = glfwGetWindowAttrib(renderer.window, GLFW_FOCUSED) == GLFW_TRUE &&
                     GetForegroundWindow() == glfwGetWin32Window(renderer.window);
      bool gameplay = !paused && !inventoryOpen && !controlsOpen && !mapOpen;
      captured = gameplay && (smoke || focused);
      int desired =
          (!smoke || verifyCapture) && captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL;
      if (glfwGetInputMode(renderer.window, GLFW_CURSOR) != desired) {
        glfwSetInputMode(renderer.window, GLFW_CURSOR, desired);
        glfwGetCursorPos(renderer.window, &mx, &my);
      }
      if (glfwRawMouseMotionSupported() &&
          glfwGetInputMode(renderer.window, GLFW_RAW_MOUSE_MOTION) !=
              (desired == GLFW_CURSOR_DISABLED))
        glfwSetInputMode(renderer.window, GLFW_RAW_MOUSE_MOTION, desired == GLFW_CURSOR_DISABLED);
      if (desired == GLFW_CURSOR_DISABLED && focused) {
        RECT clip{};
        GetClientRect(glfwGetWin32Window(renderer.window), &clip);
        MapWindowPoints(glfwGetWin32Window(renderer.window), nullptr,
                        reinterpret_cast<POINT *>(&clip), 2);
        cursorGuard.owned = ClipCursor(&clip) != 0;
      } else if (cursorGuard.owned) {
        ClipCursor(nullptr);
        cursorGuard.owned = false;
      }
    };
    bool worldsOpen = previewWorlds;
    std::string seedText;
    auto worldEntries = savedWorlds();
    int worldPage = 0, worldSelection = -1;
    std::filesystem::path pendingDelete;
    syncCursor();
    CameraTransition cameraTransition;
    cameraTransition.height = cameraEyeHeight(player);
    float renderedRoll = 0;
    Vec renderedForward = player.look(),
        renderedRight{std::cos(player.yaw), 0, std::sin(player.yaw)};
    int simulationTicks = 0;
    const float initialHeadYaw = player.yaw;

    std::map<int, bool> previous;
    auto key = [&](int code) { return !smoke && glfwGetKey(renderer.window, code) == GLFW_PRESS; };
    auto pressed = [&](int code) {
      bool now = key(code), edge = now && !previous[code];
      previous[code] = now;
      return edge;
    };
    bool oldLeft = false, oldRight = false;
    double mapDragX = mx, mapDragY = my;
    double last = glfwGetTime(), accumulator = 0, stepDistance = 0;
    int frames = 0;
    auto began = std::chrono::steady_clock::now();
    std::vector<Vertex> vertices;
    bool pendingJump = false;
    bool dragging = false, dragRight = false;
    std::vector<int> dragSlots;
    double lastInventoryClick = -1;
    int lastInventorySlot = -1;
    Vec previousPosition = player.pos;
    struct InputContext {
      Inventory *bag;
      bool *worlds;
      std::string *seed;
      bool *mapOpen;
      ExplorationMap *map;
    };
    InputContext input{&player.inventory, &worldsOpen, &seedText, &mapOpen, &worldMap};
    glfwSetWindowUserPointer(renderer.window, &input);
    glfwSetCharCallback(renderer.window, [](GLFWwindow *w, unsigned int code) {
      auto *input = static_cast<InputContext *>(glfwGetWindowUserPointer(w));
      if (*input->mapOpen && input->map->renaming && code >= 32 && code <= 126 &&
          input->map->editName.size() < 32) {
        input->map->editName.push_back(char(code));
        return;
      }
      if (*input->worlds && code >= 32 && code <= 126 && input->seed->size() < 32)
        input->seed->push_back(char(code));
    });
    glfwSetScrollCallback(renderer.window, [](GLFWwindow *w, double, double amount) {
      auto *context = static_cast<InputContext *>(glfwGetWindowUserPointer(w));
      if (*context->mapOpen) {
        context->map->span =
            std::clamp(context->map->span * float(std::pow(.8, amount)), 256.f, 8192.f);
        return;
      }
      auto *bag = context->bag;
      if (glfwGetInputMode(w, GLFW_CURSOR) != GLFW_CURSOR_DISABLED)
        return;
      bag->selected = (bag->selected + (amount > 0 ? -1 : 1) + 9) % 9;
    });
    auto switchWorld = [&](const std::filesystem::path &file, std::optional<uint32_t> newSeed) {
      if (!save(player, edits, activeSave)) {
        message("Save failed - current world retained");
        return;
      }
      Player next;
      Edits nextEdits;
      if (std::filesystem::exists(file)) {
        if (!load(next, nextEdits, file)) {
          message("Could not read world");
          return;
        }
      } else if (!newSeed) {
        message("World no longer exists");
        return;
      } else
        next.seed = *newSeed;
      if (!worldMap.write(activeSave, player.seed)) {
        message("Map save failed - current world retained");
        return;
      }
      work.reset(false);
      queued.clear();
      renderer.invalidateTerrain();
      const uint32_t previousSeed = worldSeed;
      worldSeed = next.seed;
      next.smoothTerrain = true;
      next.unrestrictedFlight = false;
      next.barrelFlight = true;
      if (!std::filesystem::exists(file)) {
        next.pos = {8, 0, 8};
        bool land = false;
        for (int ring = 0; ring <= 64 && !land; ++ring)
          for (int x = -ring; x <= ring && !land; ++x)
            for (int z = -ring; z <= ring && !land; ++z) {
              if (std::max(std::abs(x), std::abs(z)) != ring)
                continue;
              Vec site{float(x * 16 + 8), 0, float(z * 16 + 8)};
              float floor = next.groundAt(site);
              if (floor > seaLevel + 2) {
                next.pos = {site.x, floor + 2, site.z};
                land = true;
              }
            }
        if (!land)
          next.pos.y = seaLevel + 3;
        next.fallStart = next.pos.y;
        std::filesystem::create_directories(file.parent_path());
        if (!save(next, nextEdits, file)) {
          worldSeed = previousSeed;
          message("Could not create world");
          return;
        }
      }
      player = next;
      edits = std::move(nextEdits);
      activeSave = file;
      worldMap.read(file, player.seed);
      worldMap.reveal(player.pos);
      worldMap.recenter(player.pos);
      mapOpen = false;
      player.inventory.flightOnly();
      player.pos.y = std::max(player.pos.y, player.groundAt(player.pos) + .1f);
      player.fallStart = player.pos.y;
      chunks.clear();
      plannedX = INT32_MIN;
      accumulator = 0;
      pendingJump = false;
      previousPosition = player.pos;
      cameraTransition.height = cameraEyeHeight(player);
      freecam = false;
      if (!smoke) {
        std::ofstream marker("active-world.txt");
        if (file == std::filesystem::path("world.save"))
          marker << "original";
        else
          marker << file.generic_string();
      }
      renderedRoll = 0;
      worldsOpen = false;
      paused = false;
      noticeTime = 0;
      worldEntries = savedWorlds();
    };
    if (previewCreate) {
      switchWorld("build/smoke-seeded.save", seedFromText("Flight Islands"));
      Player reopened;
      Edits reopenedEdits;
      if (activeSave != std::filesystem::path("build/smoke-seeded.save") ||
          !load(reopened, reopenedEdits, activeSave) ||
          reopened.seed != seedFromText("Flight Islands") ||
          !std::filesystem::exists("build/smoke-current.save"))
        throw std::runtime_error("Seeded world creation/reopen integration failed");
      std::cout << "PASS seeded world creation, previous-world preservation and reopen\n";
    }
    double autosave = 0;
    bool verifiedNativeClip = false;
    double maxFrameMs = 0, frameTotalMs = 0;
    int framesOver33Ms = 0;
    std::vector<double> frameTimes;
    uint64_t totalTerrainUploads = 0;
    double simulationMs = 0, audioMs = 0, streamMs = 0, renderMs = 0;
    while (!glfwWindowShouldClose(renderer.window)) {
      const auto frameStarted = std::chrono::steady_clock::now();
      glfwPollEvents();
      double now = glfwGetTime(), weatherElapsed = std::max(0., now - last),
             dt = std::clamp(now - last, 0., .1);
      if (weather.tick(weatherElapsed, player.pos + Vec{0, 1.62f, 0}))
        audio.thunder();
      last = now;
      noticeTime = std::max(0.f, noticeTime - float(dt));
      int windowWidth, windowHeight, width, h;
      glfwGetWindowSize(renderer.window, &windowWidth, &windowHeight);
      glfwGetFramebufferSize(renderer.window, &width, &h);
      float aspect = float(std::max(width, 1)) / std::max(h, 1);
      if (verifyCapture) {
        inventoryOpen = frames >= 15 && frames < 30;
        paused = frames >= 30 && frames < 45;
        controlsOpen = frames >= 38 && frames < 45;
        mapOpen = frames >= 45 && frames < 52;
      }
      syncCursor();
      if (verifyCapture) {
        int expected = (!paused && !inventoryOpen && !controlsOpen && !mapOpen)
                           ? GLFW_CURSOR_DISABLED
                           : GLFW_CURSOR_NORMAL;
        if (glfwGetInputMode(renderer.window, GLFW_CURSOR) != expected)
          throw std::runtime_error("Cursor state integration check failed");
        if (expected == GLFW_CURSOR_DISABLED &&
            glfwGetWindowAttrib(renderer.window, GLFW_FOCUSED) &&
            GetForegroundWindow() == glfwGetWin32Window(renderer.window)) {
          RECT clip{};
          GetClipCursor(&clip);
          if (clip.right - clip.left > windowWidth + 4 || clip.bottom - clip.top > windowHeight + 4)
            throw std::runtime_error("Gameplay cursor is not confined to the window");
          verifiedNativeClip = true;
        }
      }
      if (pressed(GLFW_KEY_M) && !worldMap.renaming && !inventoryOpen && !paused && !controlsOpen) {
        mapOpen = !mapOpen;
        worldMap.renaming = false;
        pendingJump = false;
        if (mapOpen)
          worldMap.recenter(player.pos);
        else if (!smoke)
          worldMap.write(activeSave, player.seed);
      }
      if (mapOpen && worldMap.renaming) {
        if (pressed(GLFW_KEY_BACKSPACE) && !worldMap.editName.empty())
          worldMap.editName.pop_back();
        if (pressed(GLFW_KEY_ENTER) && !worldMap.editName.empty() && worldMap.selected >= 0) {
          worldMap.waypoints[worldMap.selected].name = worldMap.editName;
          worldMap.renaming = false;
        }
      }
      if (pressed(GLFW_KEY_ESCAPE)) {
        if (mapOpen) {
          if (worldMap.renaming)
            worldMap.renaming = false;
          else {
            mapOpen = false;
            if (!smoke)
              worldMap.write(activeSave, player.seed);
          }
        } else if (worldsOpen)
          worldsOpen = false;
        else if (controlsOpen)
          controlsOpen = false;
        else if (inventoryOpen)
          inventoryOpen = false;
        else
          paused = !paused;
        pendingJump = false;
      }
      bool inventoryKey = pressed(GLFW_KEY_E), tabKey = pressed(GLFW_KEY_TAB),
           iKey = pressed(GLFW_KEY_I);
      if ((inventoryKey || tabKey || iKey) && !controlsOpen && !worldsOpen && !mapOpen) {
        inventoryOpen = !inventoryOpen;
        if (!inventoryOpen)
          paused = false;
        pendingJump = false;
        player.eatTick(0, false);
      }
      syncCursor();
      if (pressed(GLFW_KEY_F5) && !smoke)
        message(save(player, edits, activeSave) && worldMap.write(activeSave, player.seed)
                    ? "World and map saved"
                    : "Save failed");
      if (pressed(GLFW_KEY_LEFT_BRACKET))
        radius = std::max(8, radius - 8);
      if (pressed(GLFW_KEY_RIGHT_BRACKET))
        radius = std::min(160, radius + 8);
      if (captured && pressed(GLFW_KEY_H)) {
        freecam = !freecam;
        if (freecam) {
          freeView = lastView;
          freeView.roll = 0;
          freeView.flying = false;
          freeView.pos = lastCameraEye;
        }
        accumulator = 0;
        previousPosition = player.pos;
      }
      if (worldsOpen && pressed(GLFW_KEY_BACKSPACE) && !seedText.empty())
        seedText.pop_back();
      if (captured && pressed(GLFW_KEY_ENTER)) {
        thirdMode = (thirdMode + 1) % 3;
        thirdPerson = thirdMode != 0;
        freecam = false;
      }
      double nx, ny;
      glfwGetCursorPos(renderer.window, &nx, &ny);
      if (captured && !smoke) {
        Player &aim = freecam ? freeView : player;
        aim.mouseLook(float(nx - mx) * .0025f, -float(ny - my) * .0025f, float(dt));
      }
      mx = nx;
      my = ny;
      float cursorX = float(nx / std::max(windowWidth, 1) * 2 - 1),
            cursorY = float(ny / std::max(windowHeight, 1) * 2 - 1);
      bool left =
          !smoke && glfwGetMouseButton(renderer.window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
      bool right =
          !smoke && glfwGetMouseButton(renderer.window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
      bool leftEdge = left && !oldLeft, rightEdge = right && !oldRight;
      const int hoverSlot = InventoryLayout(aspect).hit(cursorX, cursorY);
      if (mapOpen) {
        if (leftEdge || rightEdge)
          worldMap.click(cursorX, cursorY, aspect, rightEdge, player.pos);
        if (left && oldLeft && worldMap.area().contains(cursorX, cursorY)) {
          worldMap.centerX -=
              float(nx - mapDragX) / std::max(windowWidth, 1) * 2 * worldMap.span * aspect / 1.5f;
          worldMap.centerZ -=
              float(ny - mapDragY) / std::max(windowHeight, 1) * 2 * worldMap.span / 1.5f;
        }
      }
      mapDragX = nx;
      mapDragY = ny;
      if (inventoryOpen && pressed(GLFW_KEY_F))
        player.inventory.swapOffhand(hoverSlot);
      for (int i = 1; i <= 9; i++)
        if (pressed(GLFW_KEY_0 + i)) {
          if (inventoryOpen)
            player.inventory.swapHotbar(hoverSlot, i - 1);
          else
            player.inventory.selected = i - 1;
        }
      if (inventoryOpen && (leftEdge || rightEdge)) {
        if (leftEdge && key(GLFW_KEY_LEFT_SHIFT))
          player.inventory.quickMove(hoverSlot);
        else if (leftEdge && hoverSlot == lastInventorySlot && now - lastInventoryClick < .25 &&
                 !player.inventory.cursor.empty())
          player.inventory.collectAll();
        else if (!player.inventory.cursor.empty() && hoverSlot >= 0 && hoverSlot < 45) {
          dragging = true;
          dragRight = rightEdge;
          dragSlots.clear();
        } else
          player.inventory.click(hoverSlot, rightEdge);
        lastInventoryClick = now;
        lastInventorySlot = hoverSlot;
      }
      if (dragging) {
        if (!inventoryOpen) {
          dragging = false;
          dragSlots.clear();
        } else {
          auto *slot = player.inventory.slot(hoverSlot);
          if (slot && Inventory::accepts(hoverSlot, player.inventory.cursor.item) &&
              (slot->empty() || Inventory::same(*slot, player.inventory.cursor)) &&
              slot->count < stackLimit(player.inventory.cursor.item) &&
              std::find(dragSlots.begin(), dragSlots.end(), hoverSlot) == dragSlots.end())
            dragSlots.push_back(hoverSlot);
          if (!(dragRight ? right : left)) {
            if (dragSlots.size() > 1)
              player.inventory.distribute(dragSlots, dragRight);
            else
              player.inventory.click(hoverSlot, dragRight);
            dragging = false;
            dragSlots.clear();
          }
        }
      }
      if (paused && !inventoryOpen && leftEdge) {
        if (worldsOpen) {
          if (worldBackButton(aspect).contains(cursorX, cursorY))
            worldsOpen = false;
          else if (worldCreateButton(aspect).contains(cursorX, cursorY)) {
            uint32_t seed =
                seedText.empty()
                    ? uint32_t(std::chrono::high_resolution_clock::now().time_since_epoch().count())
                    : seedFromText(seedText);
            switchWorld(seedWorldFile(seed), seed);
          } else if (worldRestoreButton(aspect).contains(cursorX, cursorY)) {
            try {
              restoreWorld();
              worldEntries = savedWorlds();
              worldSelection = -1;
              pendingDelete.clear();
              message("World restored");
            } catch (const std::exception &error) {
              message(error.what());
            }
          } else if (worldSelection >= 0 && worldSelection < int(worldEntries.size()) &&
                     (worldActionButton(0, aspect).contains(cursorX, cursorY) ||
                      worldActionButton(1, aspect).contains(cursorX, cursorY) ||
                      worldActionButton(2, aspect).contains(cursorX, cursorY) ||
                      worldActionButton(3, aspect).contains(cursorX, cursorY))) {
            auto selectedFile = worldEntries[worldSelection].file;
            try {
              if (worldActionButton(0, aspect).contains(cursorX, cursorY))
                switchWorld(selectedFile, std::nullopt);
              else if (worldActionButton(1, aspect).contains(cursorX, cursorY)) {
                renameWorld(selectedFile, seedText);
                message("World renamed");
              } else if (worldActionButton(2, aspect).contains(cursorX, cursorY)) {
                if (selectedFile == activeSave && !save(player, edits, activeSave))
                  throw std::runtime_error("Could not save before duplicate");
                duplicateWorld(selectedFile);
                message("World duplicated");
              } else if (selectedFile == activeSave)
                message("Open another world before deleting this one");
              else if (pendingDelete == selectedFile) {
                deleteWorld(selectedFile);
                pendingDelete.clear();
                worldSelection = -1;
                message("World moved to trash - Restore can undo");
              } else {
                pendingDelete = selectedFile;
                message("Click Confirm delete to move world to trash");
              }
              worldEntries = savedWorlds();
              if (worldSelection >= 0) {
                worldSelection = -1;
                for (int index = 0; index < int(worldEntries.size()); index++)
                  if (worldEntries[index].file == selectedFile)
                    worldSelection = index;
                if (!worldActionButton(3, aspect).contains(cursorX, cursorY))
                  pendingDelete.clear();
              }
              worldPage = std::min(worldPage, std::max(0, (int(worldEntries.size()) - 1) / 4));
            } catch (const std::exception &error) {
              message(error.what());
            }
          } else if (worldPageButton(false, aspect).contains(cursorX, cursorY) && worldPage > 0)
            --worldPage;
          else if (worldPageButton(true, aspect).contains(cursorX, cursorY) &&
                   (worldPage + 1) * 4 < int(worldEntries.size()))
            ++worldPage;
          else
            for (int row = 0; row < 4 && worldPage * 4 + row < int(worldEntries.size()); ++row)
              if (worldEntryButton(row, aspect).contains(cursorX, cursorY)) {
                auto file = worldEntries[worldPage * 4 + row].file;
                worldSelection = worldPage * 4 + row;
                seedText = worldEntries[worldSelection].label;
                pendingDelete.clear();
                break;
              }
        } else if (controlsOpen) {
          if (controlsBackButton(aspect).contains(cursorX, cursorY))
            controlsOpen = false;
        } else {
          if (pauseButton(0, aspect).contains(cursorX, cursorY)) {
            paused = false;
            leftEdge = false;
          } else if (pauseButton(1, aspect).contains(cursorX, cursorY))
            inventoryOpen = true;
          else if (pauseButton(2, aspect).contains(cursorX, cursorY))
            controlsOpen = true;
          else if (pauseButton(3, aspect).contains(cursorX, cursorY))
            message(save(player, edits, activeSave) && worldMap.write(activeSave, player.seed)
                        ? "World and map saved"
                        : "Save failed");
          else if (pauseButton(4, aspect).contains(cursorX, cursorY)) {
            worldsOpen = true;
            worldEntries = savedWorlds();
            worldPage = 0;
            worldSelection = -1;
            pendingDelete.clear();
            noticeTime = 0;
          } else if (pauseButton(5, aspect).contains(cursorX, cursorY)) {
            try {
              if (selectSkin(renderer.window)) {
                renderer.reloadSkin();
                message("Skin loaded");
              }
            } catch (const std::exception &e) {
              message(e.what());
            }
          } else if (pauseButton(6, aspect).contains(cursorX, cursorY)) {
            if (save(player, edits, activeSave) && worldMap.write(activeSave, player.seed))
              glfwSetWindowShouldClose(renderer.window, GLFW_TRUE);
            else
              message("Save failed - game remains open");
          }
        }
      }
      syncCursor();
      bool jump = pressed(GLFW_KEY_SPACE);
      if (jump && captured) {
        if (player.equipped() && (player.unrestrictedFlight || (!player.grounded && !player.wet))) {
          player.deploy();
          message("Flight active - mouse steers, R / RMB boosts");
        } else
          pendingJump = true;
      }
      if (captured && pressed(GLFW_KEY_G))
        toggleEquipment();
      bool rocketKey = pressed(GLFW_KEY_R);
      if (captured && (rocketKey || (rightEdge && (player.inventory.held().item == Item::Rocket ||
                                                   player.equipped())))) {
        if (!player.flying && player.unrestrictedFlight && player.equipped() && rightEdge)
          player.deploy();
        if (player.flying) {
          if (player.firework()) {
            audio.rocket();
            message("Firework boost - unlimited supply");
          } else
            message("Equip a usable elytra before boosting");
        } else if (rightEdge && player.inventory.held().item == Item::Rocket) {
          effects.launch(player.pos + Vec{0, 1.5f, 0} + player.look(), player.look());
          audio.rocket();
          message("Firework launched");
        } else
          message("Equip elytra and deploy in the air before boosting");
      }
      bool eating = captured && (right || key(GLFW_KEY_F)) && isFood(player.inventory.held().item);
      bool eatKey = !inventoryOpen && pressed(GLFW_KEY_F);
      if (captured && (eatKey || (rightEdge && isFood(player.inventory.held().item)))) {
        if (!isFood(player.inventory.held().item))
          message("Select an apple or cooked beef to eat");
        else if (player.hunger >= 20)
          message("Hunger is full - keep this food for later");
      }
      if (eating)
        player.beginEating();
      if (smoke && captured)
        player.mouseLook(previewRoll ? float(dt) * 1.4f : 0.f, previewRoll ? float(dt) * .9f : 0.f,
                         float(dt));
      if (verifyFreeLook)
        freeView.mouseLook(float(dt) * .35f, 0, float(dt));
      auto simulationStarted = std::chrono::steady_clock::now();
      if (captured) {
        accumulator += dt;
        while (accumulator >= 1. / 20) {
          float forward = float(key(GLFW_KEY_W)) - float(key(GLFW_KEY_S)),
                strafe = float(key(GLFW_KEY_D)) - float(key(GLFW_KEY_A));
          previousPosition = player.pos;

          ++simulationTicks;
          player.tick(1.f / 20, forward, strafe, key(GLFW_KEY_LEFT_CONTROL),
                      pendingJump || key(GLFW_KEY_SPACE), key(GLFW_KEY_LEFT_SHIFT), false, edits);
          player.eatTick(1.f / 20, eating);
          pendingJump = false;
          accumulator -= 1. / 20;
          stepDistance += std::hypot(player.vel.x, player.vel.z) / 20;
          if (player.grounded && stepDistance > 1.9) {
            audio.footstep();
            stepDistance = 0;
          }
        }
      } else {
        accumulator = 0;
        previousPosition = player.pos;
        player.eatTick(0, false);
      }
      simulationMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                simulationStarted)
                          .count();
      auto audioStarted = std::chrono::steady_clock::now();
      if (captured)
        worldMap.reveal(player.pos);
      if (captured && effects.tick(float(dt), player))
        audio.burst();
      audio.update(freecam ? freeView : player, edits,
                   weather.sample.rain ? weather.sample.thunder ? .07f : .045f : 0.f);
      audioMs +=
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - audioStarted)
              .count();
      auto streamStarted = std::chrono::steady_clock::now();
      oldLeft = left;
      oldRight = right;
      if (!smoke) {
        autosave += dt;
        if (autosave >= 30) {
          autosave = 0;
          if (!save(player, edits, activeSave) || !worldMap.write(activeSave, player.seed))
            message("Autosave failed");
        }
      }
      renderedRoll +=
          std::remainder(player.roll - renderedRoll, 6.2831853f) * (-std::expm1(-12.f * float(dt)));
      Player renderPlayer = player;
      if (player.flying && player.barrelFlight) {
        float blend = -std::expm1(-20.f * float(dt));
        renderedForward = normalized(renderedForward * (1 - blend) + player.flightForward * blend);
        renderedRight = normalized(renderedRight * (1 - blend) + player.flightRight * blend);
        renderedRight =
            normalized(renderedRight - renderedForward * dot(renderedRight, renderedForward));
        renderPlayer.flightForward = renderedForward;
        renderPlayer.flightRight = renderedRight;
        renderPlayer.flightUp = normalized(cross(renderedRight, renderedForward));
      } else {
        renderedForward = player.look();
        renderedRight = {std::cos(player.yaw), 0, std::sin(player.yaw)};
      }
      renderPlayer.roll = renderedRoll;
      if (captured && !smoke && length(player.pos - previousPosition) < 8)
        renderPlayer.pos =
            previousPosition + (player.pos - previousPosition) * float(accumulator * 20);
      if (length(player.pos - previousPosition) > 8)
        cameraTransition.height = cameraEyeHeight(player);
      else
        cameraTransition.update(player, float(captured ? dt : 0));
      Player view = renderPlayer;
      float eyeHeight = cameraTransition.height;
      if (freecam || thirdPerson) {
        Vec target = renderPlayer.pos + Vec{0, cameraTransition.height, 0};
        if (freecam) {
          view = freeView;
          view.barrelFlight = false;
          view.flying = false;
          view.roll = 0;
        }
        if (!freecam && thirdMode == 2) {
          view.barrelFlight = false;
          view.flying = false;
          view.roll = 0;
          view.yaw = std::remainder(renderPlayer.yaw + 3.14159265f, 6.2831853f);
          view.pitch = -renderPlayer.pitch;
        }
        Vec desired = target - view.look() * 4.f, eye = target;
        for (int step = 1; step <= 80; ++step) {
          Vec sample = target + (desired - target) * (step / 80.f);
          bool obstructed = false;
          for (float dx : {-.15f, .15f})
            for (float dz : {-.15f, .15f})
              obstructed |= sample.y < physicalFloor(sample.x + dx, sample.z + dz, sample.y) + .15f;
          if (obstructed)
            break;
          eye = sample;
        }
        view.pos = eye;
        eyeHeight = 0;
        if (previewFree && smoke && !verifyFreeLook) {
          view = freeView;
          eyeHeight = 0;
        }
      }
      lastView = view;
      Camera activeCamera = cameraFor(view, aspect, float(radius * 16), eyeHeight);
      weather.apply(activeCamera);
      const Vec viewer{activeCamera.eye[0], activeCamera.eye[1], activeCamera.eye[2]};
      lastCameraEye = viewer;
      int cx = int(std::floor(viewer.x / 16)), cz = int(std::floor(viewer.z / 16));
      if (std::abs(int64_t(cx) - plannedX) > 3 || std::abs(int64_t(cz) - plannedZ) > 3 ||
          radius != plannedRadius) {
        terrainPlan.clear();
        wanted.clear();
        planCursor = 0;
        plannedX = cx;
        plannedZ = cz;
        plannedRadius = radius;
        int mx = int(std::floor(float(cx) / 8)), mz = int(std::floor(float(cz) / 8));
        int macroRadius = (radius + 7) / 8;
        for (int x = -macroRadius; x <= macroRadius; ++x)
          for (int z = -macroRadius; z <= macroRadius; ++z) {
            int bx = (mx + x) * 8, bz = (mz + z) * 8;
            if (std::max(std::abs(x), std::abs(z)) <= 4) {
              for (int dx = 0; dx < 8; ++dx)
                for (int dz = 0; dz < 8; ++dz) {
                  int distance = std::max(std::abs(bx + dx - cx), std::abs(bz + dz - cz));
                  if (radius < 32 && distance > radius)
                    continue;
                  Key key{bx + dx, bz + dz, 16};
                  terrainPlan.push_back({key, (distance > 9 ? std::min(16, terrainLod(distance) *
                                                                               (1 << detailPenalty))
                                                            : terrainLod(distance))});
                  wanted.insert(key);
                }
            } else {
              Key key{bx, bz, 128};
              terrainPlan.push_back({key, detailPenalty ? 64 : 32});
              wanted.insert(key);
            }
          }
        std::sort(terrainPlan.begin(), terrainPlan.end(), [&](const auto &a, const auto &b) {
          auto distance = [&](const Key &k) {
            return std::max(std::abs(k.first - cx), std::abs(k.second - cz));
          };
          return distance(a.first) < distance(b.first);
        });
        for (auto i = chunks.begin(); i != chunks.end();)
          if (!wanted.contains(i->first))
            i = chunks.erase(i);
          else
            ++i;
      }
      Batch integrated;
      if (work.take(integrated)) {
        for (auto &[key, chunk] : integrated) {
          queued.erase(key);
          if (wanted.contains(key)) {
            chunk.serial = ++meshSerial;
            chunks[key] = std::move(chunk);
          }
        }
      }
      if (!mapOpen && !terrainPlan.empty()) {
        for (int batch = 0; batch < 2; batch++) {
          std::vector<std::pair<Key, int>> jobs;
          for (size_t scanned = 0;
               scanned < std::min(size_t(128), terrainPlan.size()) && jobs.size() < 4; ++scanned) {
            auto [key, lod] = terrainPlan[planCursor];
            planCursor = (planCursor + 1) % terrainPlan.size();
            auto cached = chunks.find(key);
            if (!queued.contains(key) && (cached == chunks.end() || cached->second.lod != lod))
              jobs.push_back({key, lod});
          }
          if (jobs.empty())
            break;
          auto task = [jobs, seed = worldSeed] {
            worldSeed = seed;
            Batch result;
            for (auto [key, lod] : jobs)
              result.push_back({key, meshTerrain(key.first, key.second, lod, key.size,
                                                 key.size == 16 ? 1 : 16)});
            return result;
          };
          if (!work.submit(task))
            break;
          for (auto [key, lod] : jobs)
            queued.insert(key);
        }
      }
      vertices.clear();
      terrainDraws.clear();
      bool undergroundView = viewer.y < rawTerrain(viewer.x, viewer.z) - 8;
      if (!mapOpen)
        for (auto &[key, chunk] : chunks) {
          if (!undergroundView && inFrustum(activeCamera, chunk.min, chunk.max))
            terrainDraws.push_back({chunk.serial * 4, &chunk.vertices});
          if (undergroundView &&
              inFrustum(activeCamera, chunk.undergroundMin, chunk.undergroundMax))
            terrainDraws.push_back({chunk.serial * 4 + 1, &chunk.underground});
          // Near an entrance both sides remain visible until thick rock occludes the surface.
          if (!undergroundView &&
              (chunk.hasOpening || (viewer.y < rawTerrain(viewer.x, viewer.z) + 100 &&
                                    entranceShape(viewer.x, viewer.z) < 2)) &&
              inFrustum(activeCamera, chunk.undergroundMin, chunk.undergroundMax))
            terrainDraws.push_back({chunk.serial * 4 + 1, &chunk.underground});
        }
      std::sort(terrainDraws.begin(), terrainDraws.end(), [&](const auto &a, const auto &b) {
        auto distance = [&](const auto &draw) {
          auto &p = draw.vertices->front();
          return (p.x - viewer.x) * (p.x - viewer.x) + (p.z - viewer.z) * (p.z - viewer.z);
        };
        if (a.vertices->empty() || b.vertices->empty())
          return a.vertices->size() > b.vertices->size();
        return distance(a) < distance(b);
      });
      size_t opaqueTerrain = terrainDraws.size();
      Player effectView = player;
      effectView.yaw = view.yaw;
      effectView.pitch = view.pitch;
      effects.draw(vertices, effectView);
      if (thirdPerson || freecam)
        drawPlayer(vertices, renderPlayer);
      if (!mapOpen)
        for (auto &[key, chunk] : chunks) {
          if (!undergroundView && !chunk.water.empty() &&
              inFrustum(activeCamera, chunk.min, chunk.max))
            terrainDraws.push_back({chunk.serial * 4 + 2, &chunk.water});
          if (undergroundView && !chunk.caveWater.empty() &&
              inFrustum(activeCamera, chunk.undergroundMin, chunk.undergroundMax))
            terrainDraws.push_back({chunk.serial * 4 + 3, &chunk.caveWater});
        }
      weather.draw(vertices, activeCamera, view);
      if (!inventoryOpen && !paused && !mapOpen) {
        drawSurvivalHud(vertices, player, aspect);
        if (captured)
          text(vertices, "+", -.013f, -.018f, .038f, aspect);
      }
      if (mapOpen) {
        vertices.clear();
        worldMap.draw(vertices, player, aspect, cursorX, cursorY);
      } else if (inventoryOpen)
        drawInventory(vertices, player, aspect, cursorX, cursorY);
      else if (paused) {
        if (worldsOpen)
          drawWorldsPopup(vertices, aspect, cursorX, cursorY, seedText, worldEntries, worldPage,
                          noticeTime > 0 ? notice : "", worldSelection, !pendingDelete.empty());
        else if (controlsOpen)
          drawControlsPopup(vertices, aspect, cursorX, cursorY);
        else {
          panel(vertices, -.79f / aspect, -.76f, 1.58f / aspect, 1.57f, {.045f, .065f, .083f});
          std::string title = "GAME PAUSED";
          text(vertices, title, -textWidth(title, .063f, aspect) * .5f, -.67f, .063f, aspect);
          const char *labels[] = {"Resume game",     "Inventory",   "Controls",     "Save world",
                                  "Worlds / create", "Select skin", "Save and quit"};
          for (int i = 0; i < 7; i++)
            button(vertices, pauseButton(i, aspect), labels[i], aspect, cursorX, cursorY);
          if (noticeTime > 0)
            text(vertices, notice, -textWidth(notice, .03f, aspect) * .5f, .69f, .03f, aspect);
        }
      }
      streamMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                            streamStarted)
                      .count();
      auto renderStarted = std::chrono::steady_clock::now();
      renderer.draw(vertices, activeCamera, terrainDraws, opaqueTerrain);
      renderMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                            renderStarted)
                      .count();
      auto deadline = frameStarted + std::chrono::nanoseconds(16666667);
      auto sleepDeadline = deadline - std::chrono::milliseconds(2);
      if (std::chrono::steady_clock::now() < sleepDeadline)
        std::this_thread::sleep_until(sleepDeadline);
      while (std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
      double frameMs =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStarted)
              .count();
      frameTimes.push_back(frameMs);
      totalTerrainUploads += renderer.uploadedBytes;
      qualityMs = qualityMs * .98 + frameMs * .02;
      qualityClock += dt;
      if (qualityClock > 2 && !mapOpen) {
        qualityClock = 0;
        int next = detailPenalty;
        if (qualityMs > 22 && next < 2)
          ++next;
        else if (qualityMs < 17.2 && next > 0)
          --next;
        if (next != detailPenalty) {
          detailPenalty = next;
          plannedRadius = -1;
        }
      }
      maxFrameMs = std::max(maxFrameMs, frameMs);
      frameTotalMs += frameMs;
      if (frameMs > 33.34)
        framesOver33Ms++;
      frames++;
      if (profile && frames % 120 == 0) {
        double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
        std::cout << "PROFILE fps=" << frames / elapsed << " worst_ms=" << maxFrameMs
                  << " simulation_ms=" << simulationMs / frames << " audio_ms=" << audioMs / frames
                  << " streaming_ms=" << streamMs / frames << " render_ms=" << renderMs / frames
                  << " chunks=" << chunks.size() << " vertices=" << vertices.size() << std::endl;
      }
      if (frameLimit && frames >= frameLimit)
        break;
    }
    if (previewFree) {
      if (simulationTicks < 1 || std::abs(player.yaw - initialHeadYaw) > .0001f ||
          (verifyFreeLook && std::abs(freeView.yaw - initialHeadYaw) < .1f))
        throw std::runtime_error("Free look simulation/head independence failed");
      std::cout << "PASS free look: player simulation continued for " << simulationTicks
                << " ticks; head heading unchanged\n";
    }
    if (verifyCapture)
      std::cout << "PASS cursor modes: gameplay, inventory, pause, controls, return to gameplay; "
                   "map, native Windows confinement "
                << (verifiedNativeClip ? "verified" : "not checked (window unfocused)") << '\n';
    work.reset();
    if (!smoke) {
      save(player, edits, activeSave);
      worldMap.write(activeSave, player.seed);
    }
    if (previewCreate) {
      std::filesystem::remove("build/smoke-current.save");
      std::filesystem::remove("build/smoke-seeded.save");
    }
    double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    std::sort(frameTimes.begin(), frameTimes.end());
    PROCESS_MEMORY_COUNTERS memory{};
    K32GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory));
    auto percentile = [&](float p) {
      return frameTimes.empty() ? 0 : frameTimes[size_t((frameTimes.size() - 1) * p)];
    };
    std::cout << "Frame percentiles p50=" << percentile(.5f) << " p95=" << percentile(.95f)
              << " p99=" << percentile(.99f) << " terrain_uploaded_bytes=" << totalTerrainUploads
              << " GPU_cache_bytes=" << renderer.cacheBytes
              << " GPU_work_ms=" << renderer.gpuFrameMs << " scene_scale=" << renderer.sceneScale()
              << " workers=" << workerCount << " working_set_bytes=" << memory.WorkingSetSize
              << " smoke_muted=" << smoke << '\n';
    std::cout << "Frames: " << frames << " elapsed: " << seconds
              << " mean FPS: " << frames / seconds << " loaded chunks: " << chunks.size()
              << " submitted vertices: " << vertices.size() + renderer.drawnTerrainVertices
              << " mean frame ms: " << frameTotalMs / std::max(frames, 1)
              << " worst frame ms: " << maxFrameMs << " frames over 33 ms: " << framesOver33Ms
              << std::endl;
    std::cout << "Mean stage ms: simulation " << simulationMs / std::max(frames, 1) << " audio "
              << audioMs / std::max(frames, 1) << " streaming/UI " << streamMs / std::max(frames, 1)
              << " render/wait " << renderMs / std::max(frames, 1) << std::endl;
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Z3: " << e.what() << std::endl;
    return 1;
  }
}
