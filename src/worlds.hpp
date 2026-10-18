#pragma once
#include "save.hpp"
#include "ui.hpp"
#include <charconv>
#include <optional>
#include <chrono>
struct WorldEntry {
  std::filesystem::path file;
  std::string label;
};
inline uint32_t seedFromText(const std::string &text) {
  int64_t number = 0;
  auto result = std::from_chars(text.data(), text.data() + text.size(), number);
  if (result.ec == std::errc{} && result.ptr == text.data() + text.size())
    return uint32_t(number);
  uint32_t seed = 2166136261u;
  for (unsigned char c : text)
    seed = (seed ^ c) * 16777619u;
  return seed;
}
inline std::filesystem::path seedWorldFile(uint32_t seed) {
  return std::filesystem::path("worlds") / ("seed-" + std::to_string(seed) + ".save");
}
inline std::filesystem::path activeWorldFile() {
  std::ifstream f("active-world.txt");
  std::string marker;
  std::getline(f, marker);
  std::filesystem::path file = marker;
  if (file == "world.save" || marker == "original")
    return "world.save";
  if (file.parent_path() == "worlds" && file.extension() == ".save" &&
      std::filesystem::exists(file))
    return file;
  uint64_t seed = 0;
  auto number = std::from_chars(marker.data(), marker.data() + marker.size(), seed);
  if (number.ec == std::errc{} && number.ptr == marker.data() + marker.size() &&
      seed <= UINT32_MAX && std::filesystem::exists(seedWorldFile(uint32_t(seed))))
    return seedWorldFile(uint32_t(seed));
  return "world.save";
}
inline std::string worldLabel(const std::filesystem::path &file) {
  std::ifstream names(file.string() + ".name");
  std::string name;
  std::getline(names, name);
  if (!name.empty())
    return name.substr(0, 32);
  auto stem = file.stem().string();
  return file == "world.save"        ? "Original world"
         : stem.starts_with("seed-") ? "Seed " + stem.substr(5)
                                     : stem;
}
inline void validWorld(const std::filesystem::path &file) {
  if ((file != "world.save" && file.parent_path() != "worlds") || file.extension() != ".save" ||
      !std::filesystem::is_regular_file(file) || std::filesystem::is_symlink(file))
    throw std::runtime_error("Invalid world selection");
}
inline void renameWorld(const std::filesystem::path &file, const std::string &name) {
  validWorld(file);
  if (name.empty())
    throw std::runtime_error("Type a world name first");
  std::ofstream out(file.string() + ".name");
  out << name.substr(0, 32);
  if (!out)
    throw std::runtime_error("Could not rename world");
}
inline std::filesystem::path duplicateWorld(const std::filesystem::path &file) {
  validWorld(file);
  std::filesystem::create_directories("worlds");
  auto serial = std::chrono::high_resolution_clock::now().time_since_epoch().count();
  auto target = std::filesystem::path("worlds") / ("seed-copy-" + std::to_string(serial) + ".save");
  std::filesystem::copy_file(file, target);
  if (std::filesystem::exists(file.string() + ".map"))
    std::filesystem::copy_file(file.string() + ".map", target.string() + ".map");
  renameWorld(target, worldLabel(file) + " copy");
  return target;
}
inline void deleteWorld(const std::filesystem::path &file) {
  validWorld(file);
  std::filesystem::create_directories("worlds/trash");
  auto serial = std::chrono::high_resolution_clock::now().time_since_epoch().count();
  auto target = std::filesystem::path("worlds/trash") /
                (std::to_string(serial) + "_" + file.filename().string());
  std::filesystem::rename(file, target);
  if (std::filesystem::exists(file.string() + ".map"))
    std::filesystem::rename(file.string() + ".map", target.string() + ".map");
  if (std::filesystem::exists(file.string() + ".name"))
    std::filesystem::rename(file.string() + ".name", target.string() + ".name");
}
inline std::filesystem::path restoreWorld() {
  std::filesystem::path latest;
  if (std::filesystem::exists("worlds/trash"))
    for (const auto &file : std::filesystem::directory_iterator("worlds/trash"))
      if (file.is_regular_file() && file.path().extension() == ".save" &&
          (latest.empty() || file.path().filename().string() > latest.filename().string()))
        latest = file.path();
  bool archived = false;
  if (latest.empty() && std::filesystem::exists("worlds/recovery"))
    for (const auto &entry : std::filesystem::recursive_directory_iterator("worlds/recovery"))
      if (entry.is_regular_file() && entry.path().extension() == ".save" &&
          (latest.empty() || entry.path().generic_string() > latest.generic_string())) {
        latest = entry.path();
        archived = true;
      }
  if (latest.empty())
    throw std::runtime_error("No deleted or archived worlds to restore");
  std::string name = latest.filename().string();
  if (!archived)
    name = name.substr(name.find('_') + 1);
  auto target =
      name == "world.save" ? std::filesystem::path(name) : std::filesystem::path("worlds") / name;
  if (std::filesystem::exists(target))
    throw std::runtime_error("A world with that file already exists");
  std::filesystem::rename(latest, target);
  if (std::filesystem::exists(latest.string() + ".map"))
    std::filesystem::rename(latest.string() + ".map", target.string() + ".map");
  if (std::filesystem::exists(latest.string() + ".name"))
    std::filesystem::rename(latest.string() + ".name", target.string() + ".name");
  return target;
}
inline std::vector<WorldEntry> savedWorlds() {
  std::vector<WorldEntry> result;
  if (std::filesystem::exists("world.save"))
    result.push_back({"world.save", worldLabel("world.save")});
  if (std::filesystem::exists("worlds"))
    for (const auto &file : std::filesystem::directory_iterator("worlds")) {
      if (file.is_regular_file() && file.path().extension() == ".save")
        result.push_back({file.path(), worldLabel(file.path())});
    }
  std::sort(result.begin(), result.end(),
            [](const auto &a, const auto &b) { return a.label < b.label; });
  return result;
}
// Explicit --reset-worlds migration: preserve old saves outside the active list.
inline void resetWorldCollection() {
  auto archive =
      std::filesystem::path("worlds/recovery") /
      std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
  std::filesystem::create_directories(archive);
  for (auto &entry : savedWorlds()) {
    validWorld(entry.file);
    auto target = archive / entry.file.filename();
    for (const char *suffix : {"", ".name", ".map"})
      if (std::filesystem::exists(entry.file.string() + suffix))
        std::filesystem::rename(entry.file.string() + suffix, target.string() + suffix);
  }
  uint32_t seed = uint32_t(std::chrono::high_resolution_clock::now().time_since_epoch().count());
  Player p;
  p.seed = seed;
  worldSeed = seed;
  p.smoothTerrain = true;
  p.pos = {8, rawTerrain(8, 8) + 4, 8};
  for (int ring = 0; ring < 64; ring++) {
    bool found = false;
    for (int x = -ring; x <= ring && !found; x++)
      for (int z = -ring; z <= ring && !found; z++) {
        if (std::max(std::abs(x), std::abs(z)) != ring)
          continue;
        float wx = x * 32 + 8, wz = z * 32 + 8, y = terrain(wx, wz);
        if (y > seaLevel + 3 && entranceShape(wx, wz) > 2) {
          p.pos = {wx, y + 3, wz};
          found = true;
        }
      }
    if (found)
      break;
  }
  p.fallStart = p.pos.y;
  auto file = seedWorldFile(seed);
  Edits e;
  if (!save(p, e, file))
    throw std::runtime_error(
        "Could not create replacement world; old saves are in worlds/recovery");
  renameWorld(file, "New wilderness");
  std::ofstream marker("active-world.txt");
  marker << file.generic_string();
}
inline ButtonRect worldCreateButton(float a) { return {-.85f / a, -.31f, 1.7f / a, .13f}; }
inline ButtonRect worldEntryButton(int row, float a) {
  return {-.85f / a, -.12f + row * .115f, 1.7f / a, .10f};
}
inline ButtonRect worldPageButton(bool next, float a) {
  return {(next ? .1f : -.85f) / a, .70f, .75f / a, .12f};
}
inline ButtonRect worldBackButton(float a) { return {-.85f / a, .84f, .8f / a, .12f}; }
inline ButtonRect worldActionButton(int i, float a) {
  return {((i % 2) ? .05f : -.85f) / a, .38f + (i / 2) * .14f, .8f / a, .12f};
}
inline ButtonRect worldRestoreButton(float a) { return {.05f / a, .84f, .8f / a, .12f}; }
inline void drawWorldsPopup(std::vector<Vertex> &v, float a, float mx, float my,
                            const std::string &seed, const std::vector<WorldEntry> &worlds,
                            int page, const std::string &notice, int selected = -1,
                            bool confirming = false) {
  panel(v, -1.f / a, -.9f, 2.f / a, 1.86f, {.045f, .065f, .083f});
  text(v, "WORLDS", -textWidth("WORLDS", .06f, a) * .5f, -.82f, .06f, a);
  text(v, "Seed / selected world name", -.85f / a, -.63f, .026f, a);
  panel(v, -.85f / a, -.52f, 1.7f / a, .15f, {.02f, .03f, .04f});
  text(v, seed.empty() ? "Type a seed..." : seed, -.81f / a, -.50f, .029f, a);
  button(v, worldCreateButton(a), "Create / open seed", a, mx, my);
  for (int row = 0; row < 4 && page * 4 + row < int(worlds.size()); ++row)
    button(v, worldEntryButton(row, a),
           (selected == page * 4 + row ? "> " : "") + worlds[page * 4 + row].label, a, mx, my);
  const char *actions[] = {"Open", "Rename", "Duplicate", confirming ? "Confirm delete" : "Delete"};
  for (int i = 0; i < 4; i++)
    button(v, worldActionButton(i, a), actions[i], a, mx, my, selected >= 0);
  button(v, worldRestoreButton(a), "Restore", a, mx, my);
  button(v, worldPageButton(false, a), "Previous", a, mx, my, page > 0);
  button(v, worldPageButton(true, a), "Next", a, mx, my, (page + 1) * 4 < int(worlds.size()));
  button(v, worldBackButton(a), "Back", a, mx, my);
  if (!notice.empty())
    text(v, notice.substr(0, 44), -.85f / a, .65f, .021f, a, {1, .8f, .4f});
}
