#pragma once
#include "world.hpp"
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <windows.h>

inline bool readStack(std::istream &f, Stack &s, bool armor = false) {
  int item;
  if (!(f >> item >> s.count >> s.durability) || item < 0 || item > 19 || s.durability < 0 ||
      s.durability > 432)
    return false;
  s.item = Item(item);
  if (s.count < 0 || s.count > stackLimit(s.item) || ((s.item == Item::None) != (s.count == 0)))
    return false;
  if (armor && s.item != Item::None && s.item != Item::Elytra && s.item != Item::Chestplate)
    return false;
  return true;
}
inline bool writeSave(std::ostream &f, const Player &p, const Edits &edits) {
  f << "Z3 4\n"
    << p.seed << '\n'
    << std::setprecision(9) << p.pos.x << ' ' << p.pos.y << ' ' << p.pos.z << ' ' << p.yaw << ' '
    << p.pitch << ' ' << p.hunger << ' ' << p.health << ' ' << p.saturation << ' ' << p.exhaustion
    << ' ' << p.wingWear << '\n';
  f << p.inventory.selected << '\n';
  auto stack = [&](const Stack &s) {
    f << int(s.item) << ' ' << s.count << ' ' << s.durability << '\n';
  };
  for (const auto &s : p.inventory.slots)
    stack(s);
  stack(p.inventory.chest);
  stack(p.inventory.cursor);
  stack(p.inventory.helmet);
  stack(p.inventory.leggings);
  stack(p.inventory.boots);
  stack(p.inventory.offhand);
  for (const auto &s : p.inventory.crafting)
    stack(s);
  for (const auto &[k, b] : edits) {
    auto [x, y, z] = k;
    f << x << ' ' << y << ' ' << z << ' ' << b << '\n';
  }
  return bool(f);
}
inline bool readSave(std::istream &f, Player &p, Edits &edits) {
  std::string magic;
  int version;
  if (!(f >> magic >> version) || magic != "Z3" || (version < 1 || version > 4))
    return false;
  Player q;
  if (version >= 4) {
    uint64_t seed;
    if (!(f >> seed) || seed > UINT32_MAX)
      return false;
    q.seed = uint32_t(seed);
  }
  if (!(f >> q.pos.x >> q.pos.y >> q.pos.z >> q.yaw >> q.pitch >> q.hunger >> q.health))
    return false;
  for (float n : {q.pos.x, q.pos.y, q.pos.z, q.yaw, q.pitch, q.hunger, q.health})
    if (!std::isfinite(n))
      return false;
  if (q.hunger < 0 || q.hunger > 20 || q.health <= 0 || q.health > 20)
    return false;
  if (version >= 2) {
    if (!(f >> q.saturation >> q.exhaustion >> q.wingWear >> q.inventory.selected))
      return false;
    if (!std::isfinite(q.saturation) || !std::isfinite(q.exhaustion) ||
        !std::isfinite(q.wingWear) || q.saturation < 0 || q.saturation > 20 || q.exhaustion < 0 ||
        q.exhaustion > 100 || q.wingWear < 0 || q.wingWear >= 1 || q.inventory.selected < 0 ||
        q.inventory.selected > 8)
      return false;
    for (auto &s : q.inventory.slots)
      if (!readStack(f, s))
        return false;
    if (!readStack(f, q.inventory.chest, true) || !readStack(f, q.inventory.cursor))
      return false;
  }
  if (version >= 3) {
    for (int index = 37; index <= 44; index++) {
      Stack *s = q.inventory.slot(index);
      if (!readStack(f, *s) || !Inventory::accepts(index, s->item))
        return false;
    }
  }
  Edits updated;
  int x, y, z, b;
  while (f >> x >> y >> z >> b) {
    if (updated.size() >= 100000 || std::abs(double(x)) > 1000000 ||
        std::abs(double(z)) > 1000000 || y < 0 || y >= 96 || b < 0 || b > 10)
      return false;
    updated[{x, y, z}] = b;
  }
  if (!f.eof())
    return false;
  q.pitch = std::remainder(q.pitch, 6.2831853f);
  q.fallStart = q.pos.y;
  p = q;
  edits = std::move(updated);
  return true;
}
inline bool save(const Player &p, const Edits &edits,
                 const std::filesystem::path &file = "world.save") {
  auto temporary = file;
  temporary += ".tmp";
  {
    std::ofstream f(temporary);
    if (!writeSave(f, p, edits))
      return false;
  }
  // Windows replacement through MoveFileEx avoids deleting the previous save first.
  return MoveFileExW(temporary.c_str(), file.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
inline bool load(Player &p, Edits &e, const std::filesystem::path &file = "world.save") {
  std::ifstream f(file);
  return readSave(f, p, e);
}
