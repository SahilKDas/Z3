#pragma once
#include "renderer.hpp"
#include "inventory.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <tuple>
#include <bit>
struct Vec {
  float x = 0, y = 0, z = 0;
  Vec operator+(Vec b) const { return {x + b.x, y + b.y, z + b.z}; }
  Vec operator-(Vec b) const { return {x - b.x, y - b.y, z - b.z}; }
  Vec operator*(float f) const { return {x * f, y * f, z * f}; }
};
inline float dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float length(Vec v) { return std::sqrt(dot(v, v)); }
inline Vec normalized(Vec v) {
  float n = length(v);
  return n > 0 ? v * (1 / n) : Vec{};
}
inline Vec cross(Vec a, Vec b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline thread_local uint32_t worldSeed = 2643;
inline uint32_t hash(int x, int y, int z, uint32_t seed = worldSeed) {
  uint32_t h =
      uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + uint32_t(z) * 2246822519u + seed;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
inline float noise(float x, float y, float z = 0) {
  int ix = int(std::floor(x)), iy = int(std::floor(y)), iz = int(std::floor(z));
  float a = x - ix, b = y - iy, c = z - iz;
  a = a * a * (3 - 2 * a);
  b = b * b * (3 - 2 * b);
  c = c * c * (3 - 2 * c);
  auto v = [&](int dx, int dy, int dz) {
    return float(hash(ix + dx, iy + dy, iz + dz) & 65535) / 32767.5f - 1;
  };
  auto lerp = [](float p, float q, float t) { return p + (q - p) * t; };
  return lerp(lerp(lerp(v(0, 0, 0), v(1, 0, 0), a), lerp(v(0, 1, 0), v(1, 1, 0), a), b),
              lerp(lerp(v(0, 0, 1), v(1, 0, 1), a), lerp(v(0, 1, 1), v(1, 1, 1), a), b), c);
}
inline constexpr float seaLevel = 29.f;
inline float fbm(float x, float z, int octaves = 5) {
  float sum = 0, amplitude = .5f;
  for (int i = 0; i < octaves; i++) {
    sum += noise(x, z) * amplitude;
    x = x * 2.03f + 17.1f;
    z = z * 2.03f - 9.7f;
    amplitude *= .48f;
  }
  return sum;
}
inline float smoothRange(float low, float high, float value) {
  float t = std::clamp((value - low) / (high - low), 0.f, 1.f);
  return t * t * (3 - 2 * t);
}
struct Climate {
  float temperature, moisture, aridity;
};
inline Climate climate(float x, float z) {
  float temperature = noise(x * .0019f + 500, z * .0019f - 130);
  float moisture = noise(x * .0023f - 210, z * .0023f + 73);
  float aridity = smoothRange(.02f, .3f, temperature) * (1 - smoothRange(-.35f, .05f, moisture));
  return {temperature, moisture, aridity};
}
inline float rawTerrain(float x, float z) {
  // Broad continental shelves and climate-controlled relief share smooth borders.
  const float wx = x + noise(x * .004f, z * .004f) * 35;
  const float wz = z + noise(x * .004f + 83, z * .004f - 41) * 35;
  const float continent = fbm(wx * .0018f, wz * .0018f, 4);
  const float land = smoothRange(-.08f, .14f, continent);
  const Climate biome = climate(wx, wz);
  const float mountain = smoothRange(.12f, .65f, noise(wx * .003f + 170, wz * .003f)) * land *
                         (1 - biome.aridity * .85f);
  const float ridge = 1 - std::abs(fbm(wx * .006f, wz * .006f, 4) * 1.4f);
  const float drainage = std::abs(noise(wx * .004f - 250, wz * .004f + 90));
  const float valley = std::exp(-drainage * drainage * 240) * land;
  const float hills = fbm(wx * .012f, wz * .012f, 3) * (9 - biome.aridity * 6);
  const float dunes = std::sin(wx * .07f + noise(wx * .015f, wz * .015f) * 3) *
                      (1.2f + noise(wx * .025f, wz * .025f) * .6f);
  const float river = std::exp(-drainage * drainage * 1400) * land * (1 - mountain);
  const float detail = fbm(wx * .12f, wz * .12f, 4) * (1.1f + mountain * 2.4f);
  const float crags = std::pow(std::abs(noise(wx * .03f, wz * .03f)), 3.f) * mountain * 9;
  return std::clamp(seaLevel - 5 + continent * 72 + land * 11 + hills +
                        ridge * ridge * mountain * 58 - valley * (4 + mountain * 9) +
                        dunes * biome.aridity * land + detail + crags -
                        river * std::max(0.f, continent * 72 + land * 11 - 3),
                    3.f, 205.f);
}
struct Geology {
  bool active = false, cave = false, roof = false;
  float floor = 0, ceiling = 0, depth = 0;
};
struct CaveExit {
  uint32_t seed = 0;
  int bx = INT32_MIN, bz = INT32_MIN;
  float x = 0, z = 0;
  bool valid = false, ravine = false;
  float lip = 0;
};
inline CaveExit caveExit(int bx, int bz) {
  thread_local std::array<CaveExit, 16> cache{};
  auto &exit = cache[(uint32_t(bx) & 3) + 4 * (uint32_t(bz) & 3)];
  if (exit.bx == bx && exit.bz == bz && exit.seed == worldSeed)
    return exit;
  exit = {worldSeed, bx, bz, 0, 0, false, ((bx + bz + int(worldSeed & 1)) & 1) != 0};
  uint32_t rotation = hash(bx, 817, bz) % 16;
  // One dry entrance per four-by-four chamber region, readily found from the surface.
  for (int attempt = 0; attempt < 16; attempt++) {
    int index = (int(rotation) + attempt * 7) % 16;
    int cx = bx * 4 + index % 4, cz = bz * 4 + index / 4;
    float x = cx * 512 + 256, z = cz * 512 + 256;
    if (rawTerrain(x, z) > seaLevel + 6) {
      exit.x = x;
      exit.z = z;
      exit.valid = true;
      exit.lip = rawTerrain(x, z - 202) - 12;
      break;
    }
  }
  return exit;
}
inline float networkFloor(float x, float z) { return -1200 + fbm(x * .0008f, z * .0008f, 3) * 100; }
struct CaveNode {
  float x, z, rx, rz, height;
  bool lake;
};
inline CaveNode caveNodeUncached(int gx, int gz) {
  uint32_t h = hash(gx, 501, gz);
  float x = gx * 512 + 256 + int(h % 121) - 60, z = gz * 512 + 256 + int((h >> 8) % 121) - 60;
  bool large = (h % 7) == 0;
  return {x,
          z,
          large ? 100.f + float((h >> 16) % 51) : 30.f + float((h >> 16) % 31),
          large ? 90.f + float((h >> 20) % 61) : 25.f + float((h >> 20) % 36),
          large ? 70.f + float((h >> 12) % 71) : 22.f + float((h >> 12) % 29),
          large && (h % 3) == 0 && networkFloor(x, z) < -1050};
}
inline CaveNode caveNode(int gx, int gz) {
  struct Entry {
    int gx = INT32_MIN, gz = INT32_MIN;
    uint32_t seed = 0;
    CaveNode value;
  };
  thread_local std::array<Entry, 256> cache{};
  auto &entry = cache[hash(gx, 919, gz) & 255];
  if (entry.gx == gx && entry.gz == gz && entry.seed == worldSeed)
    return entry.value;
  auto node = caveNodeUncached(gx, gz);
  if (node.lake) {
    float level = networkFloor(node.x, node.z);
    for (Vec d : {Vec{50, 0, 0}, Vec{-50, 0, 0}, Vec{0, 0, 50}, Vec{0, 0, -50}})
      node.lake &= std::abs(networkFloor(node.x + d.x, node.z + d.z) - level) < 4;
  }
  entry = {gx, gz, worldSeed, node};
  return node;
}
inline float caveLakeLevel(float x, float z) {
  int gx = int(std::floor(x / 512)), gz = int(std::floor(z / 512));
  auto node = caveNode(gx, gz);
  if (!node.lake || std::hypot(x - node.x, z - node.z) > std::min(50.f, node.rx * .55f))
    return -10000;
  return networkFloor(node.x, node.z) - 3;
}
inline float entranceShape(float x, float z) {
  auto exit = caveExit(int(std::floor(x / 2048)), int(std::floor(z / 2048)));
  if (!exit.valid)
    return 100;
  float ex = x - exit.x, ez = z - exit.z;
  return exit.ravine ? ex * ex / (205 * 205) + ez * ez / (210 * 210)
                     : (ex * ex + ez * ez) / (205 * 205);
}
inline float entranceRim(float x, float z) {
  float shape = entranceShape(x, z);
  return shape >= 1 && shape < 1.5f ? std::sin((shape - 1) / .5f * 3.14159265f) : 0;
}
inline Geology geologyUncached(float x, float z) {
  int gx = int(std::floor(x / 512)), gz = int(std::floor(z / 512));
  auto node = caveNode(gx, gz);
  float wx = x + noise(x * .018f, z * .018f) * 3,
        wz = z + noise(x * .016f + 41, z * .016f - 28) * 3;
  float nx = (wx - node.x) / node.rx, nz = (wz - node.z) / node.rz;
  float n = nx * nx + nz * nz, clearance = node.height;
  auto passage = [&](CaveNode a, CaveNode b, float width, float phase) {
    float vx = b.x - a.x, vz = b.z - a.z, len2 = std::max(.01f, vx * vx + vz * vz);
    float t = std::clamp(((wx - a.x) * vx + (wz - a.z) * vz) / len2, 0.f, 1.f);
    float length = std::sqrt(len2),
          bend = std::sin(t * 3.14159265f) * std::sin(t * 6.2831853f + phase) * 38;
    float px = a.x + vx * t - vz / length * bend, pz = a.z + vz * t + vx / length * bend;
    float radius = width * (1 + .2f * std::sin(t * 12 + phase));
    float q = ((wx - px) * (wx - px) + (wz - pz) * (wz - pz)) / (radius * radius);
    if (q < n) {
      n = q;
      clearance = width * 1.8f;
    }
  };
  // Guaranteed east/west backbone, with selected cross-links and short blind branches.
  for (int ex = gx - 1; ex <= gx; ex++) {
    auto a = caveNode(ex, gz), b = caveNode(ex + 1, gz);
    auto h = hash(ex, 617, gz);
    passage(a, b, h % 9 == 0 ? 40.f : 7.f + float(h % 9), float(h % 31));
  }
  for (int ez = gz - 1; ez <= gz; ez++) {
    int block = int(std::floor(gx / 4.f));
    if (gx == block * 4 + int(hash(block, 211, ez) % 4))
      passage(caveNode(gx, ez), caveNode(gx, ez + 1), 10.f + hash(gx, 619, ez) % 6, float(ez % 13));
  }
  if (hash(gx, 625, gz) % 3 == 0) {
    float angle = float(hash(gx, 627, gz) % 628) * .01f;
    CaveNode end = node;
    end.x += std::cos(angle) * 170;
    end.z += std::sin(angle) * 170;
    passage(node, end, 7.f, 1.5f);
  }
  auto exit = caveExit(int(std::floor(x / 2048)), int(std::floor(z / 2048)));
  // Link the bottom of a winding entrance ramp to the main cave network.
  if (exit.valid && gx == int(std::floor(exit.x / 512)) && gz == int(std::floor(exit.z / 512))) {
    CaveNode entry = node;
    entry.x = exit.x;
    entry.z = exit.z - 225;
    passage(node, entry, 36, 0);
  }
  float floor = networkFloor(x, z) + noise(x * .021f, z * .021f) * 2;
  float lake = caveLakeLevel(x, z);
  if (lake > -9000) {
    float d = std::hypot(x - node.x, z - node.z) / (std::min(50.f, node.rx * .55f));
    floor = floor - 10 * (1 - smoothRange(0.f, 1.f, d));
  }
  float top = rawTerrain(x, z);
  if (n >= 1)
    return {false, true, false, floor, floor, top - floor};
  bool opening = false;
  float ceiling =
      floor + (clearance + noise(x * .037f, z * .037f) * 3) * std::sqrt(std::max(0.f, 1 - n));
  if (!opening)
    ceiling = std::max(floor, std::min(ceiling, top - 5));
  return {true, !(opening && exit.ravine), !opening, floor, ceiling, top - floor};
}
inline Geology geologyBase(float x, float z) {
  struct Entry {
    uint32_t x = 0, z = 0, seed = 0;
    bool valid = false;
    Geology value;
  };
  thread_local std::array<Entry, 8192> cache{};
  uint32_t ux = std::bit_cast<uint32_t>(x), uz = std::bit_cast<uint32_t>(z);
  auto &entry = cache[hash(int(ux), 943, int(uz)) & 8191];
  if (entry.valid && entry.x == ux && entry.z == uz && entry.seed == worldSeed)
    return entry.value;
  entry = {ux, uz, worldSeed, true, geologyUncached(x, z)};
  return entry.value;
}
inline float entranceBowlFloor(float x, float z, const CaveExit &exit) {
  float ex = x - exit.x, ez = z - exit.z;
  float q = exit.ravine ? ex * ex / (205 * 205) + ez * ez / (210 * 210)
                        : (ex * ex + ez * ez) / (205 * 205);
  return exit.lip - 120 * std::max(0.f, 1 - q);
}
inline Geology geologyRamp(float x, float z) {
  auto exit = caveExit(int(std::floor(x / 2048)), int(std::floor(z / 2048)));
  if (!exit.valid)
    return {};
  float dx = x - exit.x, dz = z - exit.z, d = std::hypot(dx, dz);
  if (d < 96 || d > 249)
    return {};
  float theta = std::atan2(dz, dx) + 1.5707963268f;
  if (theta < 0)
    theta += 6.283185307f;
  float best = 100, tBest = 0;
  for (int turn = 0; turn < 3; turn++) {
    float t = (theta + turn * 6.283185307f) / (6 * 3.1415926536f);
    float radius = 120 + 105 * t + 3 * std::sin(theta * 3);
    float n = std::abs(d - radius) / 8;
    if (n < best) {
      best = n;
      tBest = t;
    }
  }
  float start = entranceBowlFloor(exit.x, exit.z - 120, exit);
  float end =
      networkFloor(exit.x, exit.z - 225) + noise(exit.x * .021f, (exit.z - 225) * .021f) * 2;
  float floor = start * (1 - tBest) + end * tBest;
  if (best >= 1)
    return {false, true, false, floor, floor, rawTerrain(x, z) - floor};
  float ceiling = floor + 24 * std::sqrt(std::max(0.f, 1 - best * best));
  bool roof = entranceShape(x, z) >= 1 || ceiling < entranceBowlFloor(x, z, exit) - 2;
  return {true, true, roof, floor, ceiling, rawTerrain(x, z) - floor};
}
inline Geology geologyEntrance(float x, float z) {
  auto exit = caveExit(int(std::floor(x / 2048)), int(std::floor(z / 2048)));
  if (!exit.valid || entranceShape(x, z) >= 1)
    return {};
  float floor = entranceBowlFloor(x, z, exit);
  auto ramp = geologyRamp(x, z);
  if (ramp.active && !ramp.roof)
    floor = std::min(floor, ramp.floor);
  return {true, !exit.ravine, false, floor, rawTerrain(x, z) + 1000, rawTerrain(x, z) - floor};
}
inline Geology geology(float x, float z) {
  auto entrance = geologyEntrance(x, z);
  return entrance.active ? entrance : geologyBase(x, z);
}
inline bool joinedVolumes(Geology a, Geology b) {
  return a.active && b.active && a.ceiling >= b.floor - 2 && b.ceiling >= a.floor - 2;
}
inline Geology geologyLayer(float x, float z, int layer) {
  if (layer == 2)
    return geologyEntrance(x, z);
  auto base = geologyBase(x, z), ramp = geologyRamp(x, z);
  if (joinedVolumes(base, ramp)) {
    if (layer == 1) {
      ramp.active = false;
      ramp.roof = false;
      return ramp;
    }
    base.floor = std::min(base.floor, ramp.floor);
    base.ceiling = std::max(base.ceiling, ramp.ceiling);
    base.roof = base.roof && ramp.roof;
    return base;
  }
  return layer == 0 ? base : ramp;
}
inline Geology geologyAt(float x, float z, float y) {
  auto base = geologyLayer(x, z, 0), ramp = geologyLayer(x, z, 1), entry = geologyEntrance(x, z);
  // Prefer the highest traversable floor below the actor. Separate ramp and
  // backbone volumes preserve their own ceilings at three-dimensional crossings.
  Geology best{};
  float score = -1e20f;
  for (auto g : {base, ramp, entry}) {
    if (!g.active || (g.roof && y > g.ceiling + .1f))
      continue;
    if (g.floor <= y + .6f && g.floor > score) {
      best = g;
      score = g.floor;
    }
  }
  if (best.active)
    return best;
  // When a swept step enters solid rock it must meet the nearest floor above it.
  float nearest = 1e20f;
  for (auto g : {base, ramp, entry})
    if (g.active && g.floor < nearest) {
      best = g;
      nearest = g.floor;
    }
  return best.active ? best : geology(x, z);
}
inline float terrain(float x, float z) {
  auto g = geology(x, z);
  return g.active && !g.roof ? g.floor : rawTerrain(x, z) + entranceRim(x, z) * 18;
}
// Exactly interpolate the same diagonal as the finest rendered triangle grid.
inline float surfaceHeight(float x, float z) {
  const float ix = std::floor(x), iz = std::floor(z), u = x - ix, v = z - iz;
  const float a = terrain(ix, iz), c = terrain(ix + 1, iz + 1);
  if (u >= v) {
    const float b = terrain(ix + 1, iz);
    return a + (b - a) * u + (c - b) * v;
  }
  const float d = terrain(ix, iz + 1);
  return a + (c - d) * u + (d - a) * v;
}
inline float physicalFloor(float x, float z, float y) {
  auto g = geologyAt(x, z, y);
  return g.active && (y < rawTerrain(x, z) - 5 || !g.roof) ? g.floor : surfaceHeight(x, z);
}
inline bool lavaAt(float x, float z) {
  auto g = geologyBase(x, z);
  auto node = caveNode(int(std::floor(x / 512)), int(std::floor(z / 512)));
  bool bank = node.lake && std::hypot(x - node.x, z - node.z) < node.rx * .8f;
  return g.active && !bank && caveLakeLevel(x, z) < -9000 && g.floor < -850 &&
         noise(x * .045f, z * .045f) > .18f;
}
inline int height(int x, int z) { return int(terrain(float(x), float(z))); }
inline bool desert(int x, int z) { return climate(float(x), float(z)).aridity > .55f; }
// IDs: air, grass, dirt, rock, sand, trunk, leaves, water, snow.
inline bool solid(int b) { return b != 0 && b != 7; }
inline int baseBlock(int x, int y, int z, int h, bool dry) {
  if (y < 0)
    return 3;
  if (y >= 256)
    return 0;
  if (y <= h) {
    if (y > 3 && y < h - 5 && std::abs(noise(x * .075f, y * .095f, z * .075f)) < .065f &&
        noise(x * .025f + 50, y * .04f, z * .025f) > .13f)
      return 0;
    if (y == h)
      return h > 57 ? 8 : (dry || h <= 29 ? 4 : 1);
    return y >= h - 3 ? 2 : 3;
  }
  return y <= 28 ? 7 : 0;
}
inline bool tree(int x, int z) {
  int h = height(x, z);
  return h > 30 && h < 56 && !desert(x, z) && (hash(x, 0, z) % 113 == 0);
}
inline int naturalBlock(int x, int y, int z) {
  int h = height(x, z), b = baseBlock(x, y, z, h, desert(x, z));
  if (b)
    return b;
  if (y <= h || y > h + 10)
    return 0;
  for (int a = x - 2; a <= x + 2; a++)
    for (int c = z - 2; c <= z + 2; c++) {
      if (hash(a, 0, c) % 113 != 0 || !tree(a, c))
        continue;
      int ground = height(a, c), top = ground + 5;
      if (a == x && c == z && y > ground && y <= top)
        return 5;
      if (y >= top - 2 && y <= top + 1 &&
          std::abs(x - a) + std::abs(z - c) + (y == top + 1 ? 1 : 0) <= 3)
        return 6;
    }
  return 0;
}
using Edits = std::map<std::tuple<int, int, int>, int>;
inline int block(int x, int y, int z, const Edits &edits) {
  auto i = edits.find({x, y, z});
  return i == edits.end() ? naturalBlock(x, y, z) : i->second;
}
inline void quad(std::vector<Vertex> &out, const Vec *p, int tile, float light,
                 Vec tint = {1, 1, 1}, float width = 1, float height = 1) {
  float uv[4][2] = {{0, height}, {width, height}, {width, 0}, {0, 0}};
  for (int i : {0, 1, 2, 0, 2, 3})
    out.push_back({p[i].x, p[i].y, p[i].z, uv[i][0], uv[i][1], tint.x * light, tint.y * light,
                   tint.z * light, -float(tile + 1)});
}
inline std::vector<Vertex> meshChunk(int cx, int cz, const Edits &edits) {
  constexpr int N = 18, H = 96;
  std::vector<uint8_t> cells(N * N * H);
  auto idx = [](int x, int y, int z) { return (x * 18 + z) * 96 + y; };
  for (int x = 0; x < N; x++)
    for (int z = 0; z < N; z++) {
      int wx = cx * 16 + x - 1, wz = cz * 16 + z - 1, h = height(wx, wz);
      bool dry = desert(wx, wz);
      for (int y = 0; y < H; y++)
        cells[idx(x, y, z)] = uint8_t(baseBlock(wx, y, wz, h, dry));
      for (int y = h + 1; y <= std::min(h + 9, H - 1); y++)
        if (!cells[idx(x, y, z)])
          cells[idx(x, y, z)] = uint8_t(naturalBlock(wx, y, wz));
    }
  for (auto &[key, value] : edits) {
    auto [x, y, z] = key;
    x -= cx * 16 - 1;
    z -= cz * 16 - 1;
    if (x >= 0 && x < N && z >= 0 && z < N && y >= 0 && y < H)
      cells[idx(x, y, z)] = uint8_t(value);
  }
  auto get = [&](int x, int y, int z) { return y < 0 ? 3 : y >= H ? 0 : int(cells[idx(x, y, z)]); };
  std::vector<Vertex> out;
  out.reserve(16000);
  // Greedy rectangles merge coplanar faces of the same material. The shader
  // repeats the 16-pixel tile within each rectangle without atlas bleeding.
  for (int face = 0; face < 6; face++) {
    int axis = face / 2, sign = (face % 2 == 0) ? 1 : -1;
    // X planes use Z/Y; Y planes use X/Z; Z planes use X/Y.
    int ua = axis == 0 ? 2 : 0, va = axis == 1 ? 2 : 1;
    int dims[3] = {16, 96, 16};
    int U = dims[ua], V = dims[va];
    std::vector<int> mask(U * V);
    for (int slice = 0; slice < dims[axis]; slice++) {
      for (int v = 0; v < V; v++)
        for (int u = 0; u < U; u++) {
          int c[3] = {1, 0, 1};
          c[axis] += slice;
          c[ua] += u;
          c[va] += v;
          int b = get(c[0], c[1], c[2]);
          c[axis] += sign;
          int neighbor = get(c[0], c[1], c[2]);
          mask[v * U + u] = b && (!neighbor || (neighbor == 7 && b != 7)) ? b : 0;
        }
      for (int v = 0; v < V; v++)
        for (int u = 0; u < U;) {
          int b = mask[v * U + u];
          if (!b) {
            u++;
            continue;
          }
          int w = 1, h = 1;
          while (u + w < U && mask[v * U + u + w] == b)
            w++;
          bool same = true;
          while (v + h < V && same) {
            for (int j = 0; j < w; j++)
              if (mask[(v + h) * U + u + j] != b) {
                same = false;
                break;
              }
            if (same)
              h++;
          }
          int tile = b == 1    ? (face == 2   ? 0
                                  : face == 3 ? 2
                                              : 1)
                     : b == 2  ? 2
                     : b == 3  ? 3
                     : b == 4  ? 4
                     : b == 5  ? (axis == 1 ? 6 : 5)
                     : b == 6  ? 7
                     : b == 7  ? 8
                     : b == 9  ? 22
                     : b == 10 ? 24
                               : 9;
          Vec tint = b == 1 && face == 2 ? Vec{.52f, .84f, .32f}
                     : b == 6            ? Vec{.46f, .74f, .32f}
                     : b == 7            ? Vec{.3f, .58f, .92f}
                                         : Vec{1, 1, 1};
          float light = face == 2 ? 1 : face == 3 ? .48f : face < 2 ? .78f : .88f;
          float c[3] = {float(cx * 16), 0, float(cz * 16)};
          c[axis] += slice + (sign > 0 ? 1 : 0);
          c[ua] += u;
          c[va] += v;
          Vec p[4];
          for (int i = 0; i < 4; i++) {
            float q[3] = {c[0], c[1], c[2]};
            q[ua] += (i == 1 || i == 2) ? w : 0;
            q[va] += (i >= 2) ? h : 0;
            p[i] = {q[0], q[1], q[2]};
          }
          quad(out, p, tile, light, tint, float(w), float(h));
          for (int a = 0; a < h; a++)
            for (int j = 0; j < w; j++)
              mask[(v + a) * U + u + j] = 0;
          u += w;
        }
    }
  }
  return out;
}
struct Player {
  uint32_t seed = 2643;
  float roll = 0;
  bool barrelFlight = false;
  Vec flightForward{0, 0, -1}, flightRight{1, 0, 0}, flightUp{0, 1, 0};
  Inventory inventory = Inventory::starter();
  Vec pos{8, 70, 8}, vel{};
  float yaw = 0, pitch = 0, hunger = 20, health = 20, exhaustion = 0, saturation = 5, regen = 0,
        fallStart = 70;
  bool grounded = false, flying = false, wet = false;
  bool smoothTerrain = false, unrestrictedFlight = false;
  int noJumpDelay = 0;
  bool crouching = false, swimming = false;
  float bodyHeight = 1.8f;
  float rocketTime = 0, wingWear = 0, eatTime = 0;
  int eatSlot = -1;
  bool equipped() const {
    return inventory.chest.item == Item::Elytra && !inventory.chest.empty() &&
           (unrestrictedFlight || inventory.chest.durability > 1);
  }
  void flightControl(float dt, float mouseYawRate) {
    // Mouse turn rate controls bank; stopping the mouse levels the wings.
    // Heading never drifts from keyboard input or banking alone.
    const float target = !flying              ? 0.f
                         : unrestrictedFlight ? std::remainder(mouseYawRate * .55f, 6.2831853f)
                                              : std::clamp(mouseYawRate * .55f, -1.15f, 1.15f);
    roll += std::remainder(target - roll, 6.2831853f) * (-std::expm1(-8.f * std::max(dt, 0.f)));
  }
  void mouseLook(float yawDelta, float pitchDelta, float dt) {
    if (flying && barrelFlight) {
      auto rotate = [](Vec v, Vec axis, float angle) {
        return v * std::cos(angle) + cross(axis, v) * std::sin(angle) +
               axis * (dot(axis, v) * (1 - std::cos(angle)));
      };
      flightRight = normalized(rotate(flightRight, flightForward, yawDelta));
      flightUp = normalized(cross(flightRight, flightForward));
      flightForward = normalized(rotate(flightForward, flightRight, pitchDelta));
      flightUp = normalized(cross(flightRight, flightForward));
      yaw = std::atan2(flightForward.x, -flightForward.z);
      pitch = std::asin(std::clamp(flightForward.y, -1.f, 1.f));
      Vec baseRight{std::cos(yaw), 0, std::sin(yaw)}, baseUp = cross(baseRight, flightForward);
      roll = std::atan2(-dot(flightRight, baseUp), dot(flightRight, baseRight));
      return;
    }
    const float gain = flying && unrestrictedFlight ? 2.f : 1.f;
    yaw = std::remainder(yaw + yawDelta * gain, 6.2831853f);
    pitch = flying && unrestrictedFlight ? std::remainder(pitch + pitchDelta * gain, 6.2831853f)
                                         : std::clamp(pitch + pitchDelta, -1.48f, 1.48f);
    flightControl(dt, yawDelta * gain / std::max(dt, .001f));
  }
  bool deploy() {
    if (!equipped() || (!unrestrictedFlight && (grounded || wet)))
      return false;
    flightForward = look();
    flightRight = {std::cos(yaw), 0, std::sin(yaw)};
    flightUp = cross(flightRight, flightForward);
    roll = 0;
    flying = true;
    return true;
  }
  bool firework() {
    if (!flying || !equipped())
      return false;
    // Explicit sandbox rule: rockets are unlimited. The boost equation remains vanilla.
    rocketTime = 1.5f;
    return true;
  }
  bool beginEating() {
    if (hunger >= 20 || !isFood(inventory.held().item) || inventory.held().empty())
      return false;
    if (eatSlot != inventory.selected) {
      eatSlot = inventory.selected;
      eatTime = 0;
    }
    return true;
  }
  void eatTick(float dt, bool held) {
    if (!held || eatSlot != inventory.selected || inventory.held().empty() ||
        !isFood(inventory.held().item) || hunger >= 20) {
      eatTime = 0;
      eatSlot = -1;
      return;
    }
    eatTime += dt;
    if (eatTime >= 1.6f) {
      bool steak = inventory.held().item == Item::Steak;
      if (inventory.consume(eatSlot)) {
        hunger = std::min(20.f, hunger + (steak ? 8.f : 4.f));
        saturation = std::min(hunger, saturation + (steak ? 12.8f : 2.4f));
      }
      eatTime = 0;
      eatSlot = -1;
    }
  }
  Vec look() const {
    if (barrelFlight && flying)
      return flightForward;
    return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
  }
  float groundAt(Vec p) const {
    return std::max({physicalFloor(p.x, p.z, p.y), physicalFloor(p.x - .23f, p.z, p.y),
                     physicalFloor(p.x + .23f, p.z, p.y), physicalFloor(p.x, p.z - .23f, p.y),
                     physicalFloor(p.x, p.z + .23f, p.y)});
  }
  bool collides(Vec p, const Edits &e) const {
    if (smoothTerrain) {
      auto g = geologyAt(p.x, p.z, p.y);
      return p.y < groundAt(p) - .0001f ||
             (g.roof && p.y < rawTerrain(p.x, p.z) && p.y + bodyHeight > g.ceiling);
    }
    constexpr float epsilon = 1.e-5f;
    for (int x = int(std::floor(p.x - .3f + epsilon)); x <= int(std::floor(p.x + .3f - epsilon));
         x++)
      for (int y = int(std::floor(p.y + epsilon)); y <= int(std::floor(p.y + bodyHeight - epsilon));
           y++)
        for (int z = int(std::floor(p.z - .3f + epsilon));
             z <= int(std::floor(p.z + .3f - epsilon)); z++)
          if (solid(block(x, y, z, e)))
            return true;
    return false;
  }
  // Swept full-block AABB clipping: Y, then the larger horizontal displacement.
  // Partial voxel shapes and entity colliders are tracked in PHYSICS_RESEARCH.txt.
  Vec clip(Vec origin, Vec motion, const Edits &e) const {
    if (smoothTerrain) {
      Vec at = origin;
      bool clippedX = false, clippedY = false, clippedZ = false;
      int steps = std::clamp(int(std::ceil(length(motion) / .25f)), 1, 512);
      Vec delta = motion * (1.f / steps);
      const float initialFloor = groundAt(origin);
      for (int i = 0; i < steps; i++) {
        Vec next = at + delta;
        const float floor = groundAt(next);
        if (next.y < floor) {
          if (floor - at.y > .6f || (!grounded && delta.y > 0 && floor - initialFloor > .6f)) {
            clippedX |= next.x != at.x;
            clippedZ |= next.z != at.z;
            next.x = at.x;
            next.z = at.z;
          }
          const float supportedY = std::max(next.y, groundAt(next));
          clippedY |= supportedY != next.y;
          next.y = supportedY;
        }
        auto roof = geologyAt(next.x, next.z, at.y);
        if (roof.roof && at.y > roof.ceiling && next.y < rawTerrain(next.x, next.z) - 2) {
          clippedX |= next.x != at.x;
          clippedZ |= next.z != at.z;
          next.x = at.x;
          next.z = at.z;
        }
        if (roof.roof && at.y < roof.ceiling && next.y + bodyHeight > roof.ceiling) {
          if (delta.y > 0) {
            next.y = std::min(next.y, roof.ceiling - bodyHeight);
            clippedY = true;
          } else {
            next.x = at.x;
            next.z = at.z;
            clippedX = clippedZ = true;
          }
        }
        at = next;
      }
      // Preserve requested movement on untouched axes. Subtracting large float
      // positions introduces rounding that must never be interpreted as a hit.
      Vec result = at - origin;
      return {clippedX ? result.x : motion.x, clippedY ? result.y : motion.y,
              clippedZ ? result.z : motion.z};
    }
    const int first = std::abs(motion.x) < std::abs(motion.z) ? 2 : 0;
    Vec result{};
    for (int axis : {1, first, 2 - first}) {
      float desired = axis == 0 ? motion.x : axis == 1 ? motion.y : motion.z;
      if (desired == 0)
        continue;
      Vec at = origin + result;
      float lo[] = {at.x - .3f, at.y, at.z - .3f};
      float hi[] = {at.x + .3f, at.y + bodyHeight, at.z + .3f};
      float scanLo[3], scanHi[3];
      for (int k = 0; k < 3; k++) {
        scanLo[k] = lo[k] + (k == axis ? std::min(0.f, desired) : 0.f);
        scanHi[k] = hi[k] + (k == axis ? std::max(0.f, desired) : 0.f);
      }
      float allowed = desired;
      for (int x = int(std::floor(scanLo[0])); x <= int(std::floor(scanHi[0])); x++)
        for (int y = int(std::floor(scanLo[1])); y <= int(std::floor(scanHi[1])); y++)
          for (int z = int(std::floor(scanLo[2])); z <= int(std::floor(scanHi[2])); z++) {
            if (!solid(block(x, y, z, e)))
              continue;
            const float cell[] = {float(x), float(y), float(z)};
            bool overlap = true;
            for (int k = 0; k < 3; k++)
              if (k != axis && (hi[k] <= cell[k] + 1.e-6f || lo[k] >= cell[k] + 1 - 1.e-6f))
                overlap = false;
            if (!overlap)
              continue;
            if (desired > 0 && hi[axis] <= cell[axis] + 1.e-6f)
              allowed = std::min(allowed, std::max(0.f, cell[axis] - hi[axis]));
            if (desired < 0 && lo[axis] >= cell[axis] + 1 - 1.e-6f)
              allowed = std::max(allowed, std::min(0.f, cell[axis] + 1 - lo[axis]));
          }
      (axis == 0 ? result.x : axis == 1 ? result.y : result.z) = allowed;
    }
    return result;
  }
  void tick(float dt, float forward, float strafe, bool sprint, bool jump, bool crouch,
            bool /*boost*/, const Edits &e) {
    // Called once per Java Edition 20 Hz game tick. Public velocity is metres/second.
    // Equations below use blocks/tick, as in the matching official snapshot.
    constexpr float step = .05f;
    (void)dt;
    Vec v = vel * step;
    if (v.x * v.x + v.z * v.z < 9.e-6f)
      v.x = v.z = 0;
    if (std::abs(v.y) < .003f)
      v.y = 0;
    if (noJumpDelay > 0)
      --noJumpDelay;
    if (!jump)
      noJumpDelay = 0;
    const Vec old = pos;
    float waterDepth = 0;
    bool eyesWet = false;
    if (smoothTerrain) {
      wet = pos.y < seaLevel && groundAt(pos) < seaLevel;
      waterDepth = wet ? seaLevel - pos.y : 0;
      eyesWet = wet && pos.y + (swimming || flying ? .4f : crouching ? 1.27f : 1.62f) < seaLevel;
    } else {
      for (int x = int(std::floor(pos.x - .3f + 1.e-5f));
           x <= int(std::floor(pos.x + .3f - 1.e-5f)); x++)
        for (int z = int(std::floor(pos.z - .3f + 1.e-5f));
             z <= int(std::floor(pos.z + .3f - 1.e-5f)); z++)
          for (int y = int(std::floor(pos.y + .001f));
               y <= int(std::floor(pos.y + bodyHeight - .001f)); y++)
            if (block(x, y, z, e) == 7)
              waterDepth = std::max(waterDepth, y + 1 - pos.y);
      wet = waterDepth > 0;
      eyesWet = block(int(std::floor(pos.x)), int(std::floor(pos.y + 1.62f)),
                      int(std::floor(pos.z)), e) == 7;
    }
    if (smoothTerrain && geologyAt(pos.x, pos.z, pos.y).active &&
        pos.y < geologyAt(pos.x, pos.z, pos.y).ceiling) {
      float level =
          pos.y < geologyBase(pos.x, pos.z).ceiling ? caveLakeLevel(pos.x, pos.z) : -10000;
      waterDepth = level > -9000 ? std::max(0.f, level - pos.y) : 0;
      wet = waterDepth > 0;
      eyesWet = wet && pos.y + (swimming ? .4f : 1.62f) < level;
    }
    if (smoothTerrain && lavaAt(pos.x, pos.z) && pos.y < geologyBase(pos.x, pos.z).floor + .45f)
      health = std::max(0.f, health - step * 4);
    sprint = sprint && hunger > 6 && !crouch && forward > 0 && eatSlot < 0;
    swimming = wet && sprint && (swimming || eyesWet);
    crouching = crouch && !swimming && !flying;
    float desiredHeight = (swimming || flying) ? .6f : crouching ? 1.5f : 1.8f;
    const float previousHeight = bodyHeight;
    bodyHeight = desiredHeight;
    if (collides(pos, e))
      bodyHeight = previousHeight;
    if (!equipped() || (wet && !unrestrictedFlight))
      flying = false;
    if (flying && unrestrictedFlight) {
      // Explicit unrestricted sandbox flight. No collision, wear, ceiling,
      // water cancellation, stall or timed boost; walking keeps normal physics.
      constexpr float multiplier = 3.f;
      const Vec aim = look();
      float speed = std::max(length(vel), (rocketTime > 0 ? 34.f : 20.f) * multiplier);
      speed += std::max(0.f, -aim.y) * 9.81f * step; // Dives can exceed cruise speed.
      const float response = -std::expm1(-18.f * step);
      vel = normalized(vel * (1 - response) + aim * speed * response) * speed;
      pos = pos + vel * step;
      grounded = crouching = swimming = false;
      bodyHeight = .6f;
      wingWear = 0;
      fallStart = pos.y;
      return;
    }
    if (wet)
      fallStart = pos.y;
    if (jump && !flying) {
      if (wet && (!grounded || waterDepth > .4f))
        v.y += .04f;
      else if ((grounded || (wet && waterDepth <= .4f)) && noJumpDelay == 0) {
        v.y = std::max(v.y, .42f);
        if (sprint) {
          v.x += std::sin(yaw) * .2f;
          v.z -= std::cos(yaw) * .2f;
        }
        exhaustion = std::min(40.f, exhaustion + (sprint ? .2f : .05f));
        noJumpDelay = 10;
      }
    }
    if (wet && crouch)
      v.y -= .04f;
    float drag = grounded ? .6f * .91f : .91f;
    const bool wasFlying = flying;
    if (flying) {
      const Vec aim = look();
      const float horizontalLook = std::hypot(aim.x, aim.z);
      const float horizontalSpeed = std::hypot(v.x, v.z);
      const float lift = std::cos(pitch) * std::cos(pitch) * (barrelFlight ? 1.f : std::cos(roll));
      v.y += .08f * (-1 + .75f * lift);
      if (v.y < 0 && horizontalLook > 0) {
        const float converted = -v.y * .1f * lift;
        v.x += aim.x * converted / horizontalLook;
        v.y += converted;
        v.z += aim.z * converted / horizontalLook;
      }
      // Our pitch is positive looking up; Java's XRot uses the opposite sign.
      if (pitch > 0 && horizontalLook > 0) {
        const float converted = horizontalSpeed * std::sin(pitch) * .04f;
        v.x -= aim.x * converted / horizontalLook;
        v.y += converted * 3.2f;
        v.z -= aim.z * converted / horizontalLook;
      }
      if (horizontalLook > 0) {
        v.x += (aim.x * horizontalSpeed / horizontalLook - v.x) * .1f;
        v.z += (aim.z * horizontalSpeed / horizontalLook - v.z) * .1f;
      }
      v = {v.x * .99f, v.y * .98f, v.z * .99f};
      wingWear += step;
      if (wingWear >= 1.f - 1.e-6f) {
        wingWear = std::max(0.f, wingWear - 1);
        if (inventory.chest.durability > 1)
          --inventory.chest.durability;
        else
          flying = false;
      }
    } else {
      float inputX = strafe * .98f, inputZ = forward * .98f;
      if (eatSlot >= 0) {
        inputX *= .2f;
        inputZ *= .2f;
      }
      if (crouching) {
        inputX *= .3f;
        inputZ *= .3f;
      }
      // 26.4 square movement correction, before rotation into world coordinates.
      float n = std::hypot(inputX, inputZ);
      if (n > 0) {
        const float maximum = std::max(std::abs(inputX), std::abs(inputZ));
        const float squareDistance = n / maximum;
        const float scaled = std::min(n * squareDistance, 1.f) / n;
        inputX *= scaled;
        inputZ *= scaled;
      }
      const float acceleration = wet        ? .02f
                                 : grounded ? (sprint ? .13f : .1f)
                                 : sprint   ? .025999999f
                                            : .02f;
      v.x += (std::sin(yaw) * inputZ + std::cos(yaw) * inputX) * acceleration;
      v.z += (-std::cos(yaw) * inputZ + std::sin(yaw) * inputX) * acceleration;
      if (swimming) {
        const float aimY = look().y;
        if (aimY <= 0 || jump || eyesWet)
          v.y += (aimY - v.y) * (aimY < -.2f ? .085f : .06f);
      }
    }
    if (crouching && grounded) {
      // Sneaking trims each horizontal axis in .05-block increments at ledges.
      auto unsupported = [&](float x, float z) { return !collides(pos + Vec{x, -.6f, z}, e); };
      auto trim = [](float n) { return std::abs(n) <= .05f ? 0.f : n - std::copysign(.05f, n); };
      while (v.x != 0 && unsupported(v.x, 0))
        v.x = trim(v.x);
      while (v.z != 0 && unsupported(0, v.z))
        v.z = trim(v.z);
      while (v.x != 0 && v.z != 0 && unsupported(v.x, v.z)) {
        v.x = trim(v.x);
        v.z = trim(v.z);
      }
    }
    const float impactHorizontalSpeed = std::hypot(v.x, v.z);
    Vec resolved = clip(pos, v, e);
    const bool horizontalCollision =
        std::abs(resolved.x - v.x) > 1.e-6f || std::abs(resolved.z - v.z) > 1.e-6f;
    grounded = v.y < 0 && std::abs(resolved.y - v.y) > 1.e-6f;
    pos = pos + resolved;
    if (std::abs(resolved.x - v.x) > 1.e-6f)
      v.x = 0;
    if (std::abs(resolved.z - v.z) > 1.e-6f)
      v.z = 0;
    if (std::abs(resolved.y - v.y) > 1.e-6f)
      v.y = 0;
    if (wasFlying && horizontalCollision) {
      const float lostSpeed = impactHorizontalSpeed - std::hypot(v.x, v.z);
      health -= std::max(0.f, lostSpeed * 10 - 3);
    }
    if (!wasFlying) {
      if (wet) {
        v = {v.x * (sprint ? .9f : .8f), v.y * .8f, v.z * (sprint ? .9f : .8f)};
        if (!sprint) {
          const bool falling = vel.y <= 0;
          v.y = falling && std::abs(v.y - .005) >= .003 && std::abs(v.y - .08 / 16) < .003
                    ? -.003f
                    : v.y - .005f;
        }
        if ((jump || swimming) && horizontalCollision &&
            !collides(pos + Vec{v.x, v.y + .6f - pos.y + old.y, v.z}, e))
          v.y = .3f;
      } else {
        v.y = (v.y - .08f) * .98f;
        v.x *= drag;
        v.z *= drag;
      }
    }
    if (grounded) {
      if (!wet && !wasFlying)
        health -= std::max(0.f, std::floor(fallStart - pos.y + 1.e-6f - 3));
      flying = false;
      rocketTime = 0;
      fallStart = pos.y;
    } else if (resolved.y > 0 || wet || wasFlying)
      fallStart = pos.y;
    // Rocket entities update after player travel. Infinite supply is deliberate.
    if (flying && rocketTime > 0) {
      const Vec aim = look();
      v = v + aim * .1f + (aim * 1.5f - v) * .5f;
      rocketTime = std::max(0.f, rocketTime - step);
      if (rocketTime < 1.e-6f)
        rocketTime = 0;
    }
    vel = v * 20;
    const float distanceCm = std::round(std::hypot(pos.x - old.x, pos.z - old.z) * 100);
    if (!wasFlying && (wet || (grounded && sprint)))
      exhaustion = std::min(40.f, exhaustion + distanceCm * .01f * (wet ? .01f : .1f));
    if (exhaustion > 4) {
      exhaustion -= 4;
      if (saturation > 0)
        saturation = std::max(0.f, saturation - 1);
      else
        hunger = std::max(0.f, hunger - 1);
    }
    const bool fastRegen = hunger >= 20 && saturation > 0 && health < 20;
    const bool slowRegen = hunger >= 18 && health < 20;
    if (fastRegen || slowRegen || hunger <= 0) {
      regen += step;
      if (regen >= (fastRegen ? .5f : 4.f) - 1.e-5f) {
        regen = 0;
        if (fastRegen) {
          float spent = std::min(saturation, 6.f);
          health = std::min(20.f, health + spent / 6);
          exhaustion = std::min(40.f, exhaustion + spent);
        } else if (slowRegen) {
          health = std::min(20.f, health + 1);
          exhaustion = std::min(40.f, exhaustion + 6);
        } else
          health = std::max(1.f, health - 1); // Normal difficulty starvation floor.
      }
    } else
      regen = 0;
    if (pos.y < (smoothTerrain ? -2000.f : -20.f) || health <= 0) {
      pos = {8, smoothTerrain ? surfaceHeight(8, 8) + 8 : float(height(8, 8) + 8), 8};
      vel = {};
      health = hunger = 20;
      saturation = 5;
      exhaustion = regen = 0;
      flying = swimming = false;
      roll = 0;
      rocketTime = eatTime = 0;
      eatSlot = -1;
      fallStart = pos.y;
    }
  }
};
inline float cameraEyeHeight(const Player &p) {
  return p.flying || p.swimming ? .4f : p.crouching ? 1.27f : 1.62f;
}
struct CameraTransition {
  float height = 1.62f;
  void update(const Player &p, float dt) {
    // Time-based easing changes only eye height; mouse aim stays immediate.
    height += (cameraEyeHeight(p) - height) * (-std::expm1(-12.f * std::max(dt, 0.f)));
  }
};
inline Camera cameraFor(const Player &p, float aspect, float range, float eyeHeight = -1) {
  Vec eye = p.pos + Vec{0, eyeHeight < 0 ? cameraEyeHeight(p) : eyeHeight, 0}, f = p.look(),
      r{std::cos(p.yaw), 0, std::sin(p.yaw)}, u = cross(r, f);
  Vec bankedRight = r * std::cos(p.roll) - u * std::sin(p.roll);
  u = u * std::cos(p.roll) + r * std::sin(p.roll);
  r = bankedRight;
  if (p.barrelFlight && p.flying) {
    r = p.flightRight;
    u = p.flightUp;
  }
  float view[16] = {r.x, u.x, -f.x, 0, r.y,          u.y,          -f.y,        0,
                    r.z, u.z, -f.z, 0, -dot(r, eye), -dot(u, eye), dot(f, eye), 1};
  float focal = 1 / std::tan(.62f);
  float near = .06f, far = range + 48;
  float projection[16] = {focal / aspect,
                          0,
                          0,
                          0,
                          0,
                          -focal,
                          0,
                          0,
                          0,
                          0,
                          far / (near - far),
                          -1,
                          0,
                          0,
                          near * far / (near - far),
                          0};
  Camera c{};
  for (int col = 0; col < 4; col++)
    for (int row = 0; row < 4; row++)
      for (int k = 0; k < 4; k++)
        c.vp[col * 4 + row] += projection[k * 4 + row] * view[col * 4 + k];
  c.eye = {eye.x, eye.y, eye.z, range};
  c.environment = {!(geology(eye.x, eye.z).active && eye.y < geology(eye.x, eye.z).ceiling) &&
                           eye.y < seaLevel && surfaceHeight(eye.x, eye.z) < eye.y
                       ? 1.f
                       : 0.f,
                   0, 0, 0};
  c.subsurface[0] = eye.y < rawTerrain(eye.x, eye.z) - 8 ? 1.f : 0.f;
  float caveWater = caveLakeLevel(eye.x, eye.z);
  if (caveWater > -9000 && eye.y < caveWater && geology(eye.x, eye.z).active)
    c.environment[0] = 1;
  struct LavaSource {
    uint32_t seed = 0;
    int x = INT32_MIN, z = INT32_MIN;
    Vec pos;
    float radius = 0;
  };
  thread_local LavaSource source;
  int cellX = int(std::floor(eye.x / 16)), cellZ = int(std::floor(eye.z / 16));
  if (source.seed != worldSeed || source.x != cellX || source.z != cellZ) {
    source = {worldSeed, cellX, cellZ, {}, 0};
    float nearest = 1e20f;
    for (int x = -2; x <= 2; x++)
      for (int z = -2; z <= 2; z++) {
        float wx = cellX * 16 + 8 + x * 24, wz = cellZ * 16 + 8 + z * 24;
        if (!lavaAt(wx, wz))
          continue;
        auto g = geologyBase(wx, wz);
        float d = std::hypot(wx - eye.x, wz - eye.z);
        if (d < nearest) {
          source.pos = {wx, g.floor + .5f, wz};
          source.radius = 100;
          nearest = d;
        }
      }
  }
  c.lavaLight = {source.pos.x, source.pos.y, source.pos.z,
                 std::abs(eye.y - source.pos.y) < 120 ? source.radius : 0};
  return c;
}
