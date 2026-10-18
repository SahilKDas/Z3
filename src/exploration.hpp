#pragma once
#include "ui.hpp"
#include <array>
#include <deque>
#include <filesystem>
#include <set>

struct Waypoint {
  float x = 0, z = 0;
  std::string name;
};
class ExplorationMap {
  using Cell = std::pair<int, int>;
  std::map<Cell, std::array<uint64_t, 16>> blocks;
  std::map<Cell, Vec> colours;
  std::deque<Cell> colourOrder;
  Vec lastReveal{1e20f, 0, 1e20f};
  static int block(int c) { return int(std::floor(c / 32.f)); }

public:
  std::vector<Waypoint> waypoints;
  float centerX = 0, centerZ = 0, span = 1024;
  int selected = -1, page = 0;
  bool renaming = false;
  std::string editName;
  bool explored(int x, int z) const {
    int bx = block(x), bz = block(z), bit = (z - bz * 32) * 32 + x - bx * 32;
    auto i = blocks.find({bx, bz});
    return i != blocks.end() && (i->second[bit / 64] & (uint64_t(1) << (bit % 64)));
  }
  size_t blockCount() const { return blocks.size(); }
  void reveal(Vec p) {
    if (std::hypot(p.x - lastReveal.x, p.z - lastReveal.z) < 8)
      return;
    lastReveal = p;
    int cx = int(std::floor(p.x / 16)), cz = int(std::floor(p.z / 16));
    for (int x = cx - 12; x <= cx + 12; x++)
      for (int z = cz - 12; z <= cz + 12; z++) {
        if (std::hypot(x * 16 + 8 - p.x, z * 16 + 8 - p.z) > 192)
          continue;
        int bx = block(x), bz = block(z), bit = (z - bz * 32) * 32 + x - bx * 32;
        if (blocks.size() >= 4096 && !blocks.contains({bx, bz}))
          continue;
        blocks[{bx, bz}][bit / 64] |= uint64_t(1) << (bit % 64);
      }
  }
  void recenter(Vec p) {
    centerX = p.x;
    centerZ = p.z;
  }
  ButtonRect area() const { return {-.94f, -.75f, 1.35f, 1.5f}; }
  Vec worldAt(float x, float y, float aspect) const {
    auto r = area();
    return {centerX + (x - r.x - r.w * .5f) * span * aspect / r.h, 0,
            centerZ + (y - r.y - r.h * .5f) * span / r.h};
  }
  Vec screenAt(float x, float z, float aspect) const {
    auto r = area();
    return {r.x + r.w * .5f + (x - centerX) * r.h / (span * aspect),
            r.y + r.h * .5f + (z - centerZ) * r.h / span, 0};
  }
  void add(float x, float z) {
    if (waypoints.size() >= 128)
      return;
    waypoints.push_back({x, z, "Waypoint " + std::to_string(waypoints.size() + 1)});
    selected = int(waypoints.size()) - 1;
    page = selected / 6;
  }
  bool write(const std::filesystem::path &world, uint32_t seed) const {
    auto target = world.string() + ".map", temp = target + ".tmp";
    std::ofstream f(temp, std::ios::binary | std::ios::trunc);
    auto put = [&](auto v) { f.write(reinterpret_cast<const char *>(&v), sizeof(v)); };
    put(uint32_t(0x31504d5a));
    put(seed);
    put(uint32_t(blocks.size()));
    for (auto &[key, bits] : blocks) {
      put(int32_t(key.first));
      put(int32_t(key.second));
      for (auto b : bits)
        put(b);
    }
    put(uint32_t(waypoints.size()));
    for (auto &w : waypoints) {
      put(w.x);
      put(w.z);
      put(uint32_t(w.name.size()));
      f.write(w.name.data(), w.name.size());
    }
    f.close();
    if (!f)
      return false;
    return MoveFileExW(std::filesystem::path(temp).c_str(), std::filesystem::path(target).c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
  }
  void read(const std::filesystem::path &world, uint32_t seed) {
    *this = ExplorationMap{};
    std::ifstream f(world.string() + ".map", std::ios::binary);
    auto get = [&](auto &v) { return bool(f.read(reinterpret_cast<char *>(&v), sizeof(v))); };
    uint32_t magic = 0, stored = 0, count = 0;
    if (!get(magic) || magic != 0x31504d5a || !get(stored) || stored != seed || !get(count) ||
        count > 4096)
      return;
    ExplorationMap next;
    for (uint32_t i = 0; i < count; i++) {
      int32_t x = 0, z = 0;
      std::array<uint64_t, 16> bits{};
      if (!get(x) || !get(z) || std::abs(int64_t(x)) > 1000000 || std::abs(int64_t(z)) > 1000000)
        return;
      for (auto &v : bits)
        if (!get(v))
          return;
      next.blocks[{x, z}] = bits;
    }
    if (!get(count) || count > 128)
      return;
    for (uint32_t i = 0; i < count; i++) {
      Waypoint w;
      uint32_t n = 0;
      if (!get(w.x) || !get(w.z) || !std::isfinite(w.x) || !std::isfinite(w.z) || !get(n) || n > 32)
        return;
      w.name.resize(n);
      if (!f.read(w.name.data(), n))
        return;
      for (unsigned char c : w.name)
        if (c < 32 || c > 126)
          return;
      next.waypoints.push_back(w);
    }
    *this = std::move(next);
  }
  ButtonRect control(int i) const { return {.48f, -.63f + i * .12f, .46f, .095f}; }
  ButtonRect row(int i) const { return {.48f, -.05f + i * .095f, .46f, .085f}; }
  void click(float mx, float my, float aspect, bool right, Vec player) {
    if (right && area().contains(mx, my)) {
      Vec p = worldAt(mx, my, aspect);
      if (explored(int(std::floor(p.x / 16)), int(std::floor(p.z / 16))))
        add(p.x, p.z);
      return;
    }
    if (right)
      return;
    if (control(0).contains(mx, my))
      recenter(player);
    if (control(1).contains(mx, my) && selected >= 0) {
      renaming = true;
      editName = waypoints[selected].name;
    }
    if (control(2).contains(mx, my) && selected >= 0) {
      waypoints.erase(waypoints.begin() + selected);
      selected = -1;
      renaming = false;
    }
    if (control(3).contains(mx, my) && waypoints.size() > 6)
      page = (page + 1) % int((waypoints.size() + 5) / 6);
    for (int i = 0; i < 6; i++)
      if (row(i).contains(mx, my) && page * 6 + i < int(waypoints.size())) {
        selected = page * 6 + i;
        centerX = waypoints[selected].x;
        centerZ = waypoints[selected].z;
      }
  }
  void draw(std::vector<Vertex> &v, const Player &p, float aspect, float mx, float my) {
    panel(v, -1, -1, 2, 2, {.025f, .035f, .045f});
    text(v, "EXPLORED WORLD  /  NORTH UP", -.94f, -.91f, .035f, aspect);
    auto r = area();
    panel(v, r.x, r.y, r.w, r.h, {.065f, .075f, .08f});
    int step = std::max(1, int(std::ceil(span / 1536))); // bound visible map cells
    auto low = worldAt(r.x, r.y, aspect), high = worldAt(r.x + r.w, r.y + r.h, aspect);
    int budget = 64;
    for (int x = int(std::floor(low.x / (16 * step))) * step; x * 16 < high.x; x += step)
      for (int z = int(std::floor(low.z / (16 * step))) * step; z * 16 < high.z; z += step) {
        bool known = true;
        for (int dx = 0; dx < step; dx++)
          for (int dz = 0; dz < step; dz++)
            known &= explored(x + dx, z + dz);
        if (!known)
          continue;
        Cell key{x, z};
        auto it = colours.find(key);
        if (it == colours.end() && budget > 0) {
          --budget;
          float wx = x * 16 + 8, wz = z * 16 + 8, y = rawTerrain(wx, wz);
          auto c = climate(wx, wz);
          Vec col = y < seaLevel       ? Vec{.07f, .3f, .52f}
                    : c.aridity > .55f ? Vec{.77f, .69f, .4f}
                    : y > 95           ? Vec{.8f, .85f, .88f}
                                       : Vec{.22f, .53f, .24f};
          float shade = std::clamp(
              .8f + (rawTerrain(wx - 16, wz - 16) - rawTerrain(wx + 16, wz + 16)) * .018f, .5f,
              1.2f);
          if (entranceShape(wx, wz) < 1)
            col = {.08f, .075f, .07f};
          if (colours.size() >= 16384) {
            colours.erase(colourOrder.front());
            colourOrder.pop_front();
          }
          colourOrder.push_back(key);
          it = colours.emplace(key, col * shade).first;
        }
        auto a = screenAt(x * 16, z * 16, aspect),
             b = screenAt((x + step) * 16, (z + step) * 16, aspect);
        float ax = std::max(a.x, r.x), ay = std::max(a.y, r.y), bx = std::min(b.x, r.x + r.w),
              by = std::min(b.y, r.y + r.h);
        if (bx > ax && by > ay)
          panel(v, ax, ay, bx - ax, by - ay,
                it == colours.end() ? Vec{.15f, .2f, .15f} : it->second);
      }
    auto marker = [&](float x, float z, Vec col) {
      auto s = screenAt(x, z, aspect);
      if (r.contains(s.x, s.y))
        panel(v, s.x - .005f, s.y - .005f, .01f, .01f, col);
    };
    int bx = int(std::floor(low.x / 2048)), bz = int(std::floor(low.z / 2048));
    for (int x = bx; x <= int(std::floor(high.x / 2048)); x++)
      for (int z = bz; z <= int(std::floor(high.z / 2048)); z++) {
        auto e = caveExit(x, z);
        if (e.valid && explored(int(std::floor(e.x / 16)), int(std::floor(e.z / 16))))
          marker(e.x, e.z, {1, .65f, .12f});
      }
    for (auto &w : waypoints)
      marker(w.x, w.z, {.3f, .9f, 1});
    auto player = screenAt(p.pos.x, p.pos.z, aspect);
    if (r.contains(player.x, player.y)) {
      float sy = std::sin(p.yaw), cy = std::cos(p.yaw);
      Vec pts[] = {{player.x + sy * .019f / aspect, player.y - cy * .019f, 0},
                   {player.x - cy * .012f / aspect - sy * .01f / aspect,
                    player.y - sy * .012f + cy * .01f, 0},
                   {player.x + cy * .012f / aspect - sy * .01f / aspect,
                    player.y + sy * .012f + cy * .01f, 0}};
      for (auto q : pts)
        v.push_back({q.x, q.y, 0, .5f / 512, 32.5f / 512, 1, 1, 1, 1});
    }
    text(v, "X " + std::to_string(int(p.pos.x)) + "  Z " + std::to_string(int(p.pos.z)), .48f,
         -.87f, .026f, aspect);
    text(v,
         "Y " + std::to_string(int(p.pos.y)) + " / depth " +
             std::to_string(int(std::max(0.f, rawTerrain(p.pos.x, p.pos.z) - p.pos.y))),
         .48f, -.79f, .022f, aspect);
    const char *labels[] = {"Recenter", "Rename", "Delete", "Next page"};
    for (int i = 0; i < 4; i++)
      button(v, control(i), labels[i], aspect, mx, my, (i != 1 && i != 2) || selected >= 0);
    for (int i = 0; i < 6 && page * 6 + i < int(waypoints.size()); i++)
      button(v, row(i), waypoints[page * 6 + i].name.substr(0, 18), aspect, mx, my);
    if (area().contains(mx, my)) {
      auto hover = worldAt(mx, my, aspect);
      if (explored(int(std::floor(hover.x / 16)), int(std::floor(hover.z / 16))))
        text(v, "Height " + std::to_string(int(rawTerrain(hover.x, hover.z))) + "m", .48f, .66f,
             .022f, aspect);
    }
    text(v, "Gold: caves / Cyan: waypoints", .48f, .74f, .018f, aspect);
    text(v, std::to_string(int(span)) + "m north-south", .48f, .8f, .022f, aspect);
    text(v, renaming ? "Name: " + editName : "Right click explored land: waypoint", -.94f, .82f,
         .024f, aspect);
    text(v,
         renaming ? "Enter saves name / Esc cancels" : "Wheel: zoom / Drag: pan / M or Esc: close",
         -.94f, .9f, .025f, aspect);
  }
};
