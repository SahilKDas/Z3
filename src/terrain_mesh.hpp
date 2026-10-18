#pragma once
#include "world.hpp"
#include "skin.hpp"
#include <limits>
struct TerrainChunk {
  std::vector<Vertex> vertices, underground, water, caveWater;
  Vec undergroundMin{}, undergroundMax{};
  uint64_t serial = 0;
  bool hasOpening = false;
  Vec min{}, max{};
  int lod = 1;
};
inline int terrainLod(int distance) {
  return distance <= 5 ? 1 : distance <= 9 ? 2 : distance <= 15 ? 4 : distance <= 20 ? 8 : 16;
}
inline Vec terrainTint(float x, float y, float z) {
  Climate biome = climate(x, z);
  if (y > 95)
    return {.96f, .98f, 1.f};
  if (y < seaLevel + 2 || biome.aridity > .55f)
    return {1.f, .91f, .67f};
  Vec grass{.44f, .82f + biome.moisture * .08f, .3f};
  Vec sand{1.f, .91f, .67f};
  float transition = smoothRange(.15f, .55f, biome.aridity);
  return grass * (1 - transition) + sand * transition;
}
inline bool coralHabitat(float x, float y, float z) {
  float depth = seaLevel - y;
  return depth > 1.8f && depth < 12 && climate(x, z).temperature > .08f &&
         noise(x * .035f + 140, z * .035f - 88) > -.12f;
}
inline void terrainTriangle(std::vector<Vertex> &v, Vec a, Vec b, Vec c, int tile, Vec tint) {
  for (Vec p : {a, b, c})
    v.push_back(
        {p.x, p.y, p.z, p.x * .25f, p.z * .25f, tint.x, tint.y, tint.z, -float(tile + 101)});
}
inline void terrainBox(std::vector<Vertex> &v, Vec center, Vec half, int tile, Vec tint) {
  Vec p[8];
  for (int i = 0; i < 8; i++)
    p[i] = center +
           Vec{(i & 1) ? half.x : -half.x, (i & 2) ? half.y : -half.y, (i & 4) ? half.z : -half.z};
  const int faces[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 4, 6, 2},
                           {1, 3, 7, 5}, {2, 6, 7, 3}, {0, 1, 5, 4}};
  for (auto &f : faces) {
    Vec q[] = {p[f[0]], p[f[1]], p[f[2]], p[f[3]]};
    quad(v, q, tile, .9f, tint);
  }
}
inline TerrainChunk meshTerrain(int cx, int cz, int lod, int size = 16, int edgeStep = 1) {
  TerrainChunk chunk;
  chunk.lod = lod;
  auto &v = chunk.vertices;
  v.reserve(size_t(6 * (size / lod) * (size / lod) + 24 * size / edgeStep));
  float h[129][129];
  // Fine chunks share one-metre edges; large patches share sixteen-metre edges.
  for (int x = 0; x <= size; x++)
    for (int z = 0; z <= size; z++)
      if ((x % lod == 0 && z % lod == 0) ||
          ((x == 0 || z == 0 || x == size || z == size) && x % edgeStep == 0 && z % edgeStep == 0))
        h[x][z] = terrain(float(cx * 16 + x), float(cz * 16 + z));
  auto point = [&](int x, int z) { return Vec{float(cx * 16 + x), h[x][z], float(cz * 16 + z)}; };
  for (int x = 0; x < size; x += lod)
    for (int z = 0; z < size; z += lod) {
      Vec a = point(x, z), b = point(x + lod, z), c = point(x + lod, z + lod),
          d = point(x, z + lod);
      const float average = (a.y + b.y + c.y + d.y) * .25f;
      Vec tint = terrainTint(a.x + lod * .5f, average, a.z + lod * .5f);
      int tile =
          average > 95                                                                         ? 9
          : average < seaLevel + 2 || climate(a.x + lod * .5f, a.z + lod * .5f).aridity > .55f ? 4
                                                                                               : 0;
      auto geo = geology(a.x + lod * .5f, a.z + lod * .5f);
      if ((geo.active && !geo.roof) || entranceRim(a.x + lod * .5f, a.z + lod * .5f) > .01f) {
        tile = 3;
        tint = geo.active && !geo.roof ? Vec{.55f, .55f, .58f} : Vec{.9f, .9f, .94f};
      }
      bool mouthEdge = false;
      bool openFirst = false, caveCorners = false;
      auto firstGeo = geology(a.x, a.z);
      openFirst = firstGeo.active && !firstGeo.roof;
      for (Vec corner : {a, b, c, d}) {
        auto g = geology(corner.x, corner.z);
        caveCorners |= g.active;
        mouthEdge |= (g.active && !g.roof) != openFirst;
      }
      auto checkOpening = [&](float x, float z) {
        auto g = geology(x, z);
        bool open = g.active && !g.roof;
        caveCorners |= g.active;
        mouthEdge |= open != openFirst;
      };
      checkOpening(a.x + lod * .5f, a.z + lod * .5f);
      if (x == 0 || z == 0 || x + lod == size || z + lod == size)
        for (int t = 0; t <= lod; t += edgeStep) {
          checkOpening(a.x + t, a.z);
          checkOpening(a.x + t, a.z + lod);
          checkOpening(a.x, a.z + t);
          checkOpening(a.x + lod, a.z + t);
        }
      mouthEdge &= caveCorners;
      chunk.hasOpening |= openFirst || (geo.active && !geo.roof) || mouthEdge;
      if (mouthEdge) {
        // Clip one-metre triangles instead of discarding a whole coarse cell.
        // Shared world-space samples keep adjacent chunks on the same boundary.
        auto solid = [](Vec p) {
          auto g = geology(p.x, p.z);
          return !g.active || g.roof;
        };
        auto clip = [&](Vec a, Vec b, Vec c) {
          Vec input[] = {a, b, c};
          std::vector<Vec> polygon;
          for (int i = 0; i < 3; ++i) {
            Vec a = input[i], b = input[(i + 1) % 3];
            bool sa = solid(a), sb = solid(b);
            if (sa) polygon.push_back(a);
            if (sa != sb) {
              Vec inside = sa ? a : b, outside = sa ? b : a;
              for (int n = 0; n < 12; ++n) {
                Vec mid = (inside + outside) * .5f;
                if (solid(mid)) inside = mid; else outside = mid;
              }
              polygon.push_back(inside);
            }
          }
          for (auto &p : polygon)
            p.y = rawTerrain(p.x, p.z) + entranceRim(p.x, p.z) * 18;
          for (size_t i = 1; i + 1 < polygon.size(); ++i)
            terrainTriangle(v, polygon[0], polygon[i], polygon[i + 1], 3, {.8f, .81f, .83f});
        };
        for (int u = 0; u < lod; ++u)
          for (int w = 0; w < lod; ++w) {
            Vec p{a.x + u, 0, a.z + w};
            clip(p, p + Vec{1, 0, 0}, p + Vec{1, 0, 1});
            clip(p, p + Vec{1, 0, 1}, p + Vec{0, 0, 1});
          }
      } else if (geo.active && !geo.roof) {
        // Deliberate cave opening; its floor belongs to the underground mesh.
      } else if (lod == 1 || (x != 0 && z != 0 && x + lod != size && z + lod != size)) {
        terrainTriangle(v, a, b, c, tile, tint);
        terrainTriangle(v, a, c, d, tile, tint);
      } else {
        std::vector<Vec> edge;
        for (int t = 0; t < lod; t += (z == 0 ? edgeStep : lod))
          edge.push_back(point(x + t, z));
        for (int t = 0; t < lod; t += (x + lod == size ? edgeStep : lod))
          edge.push_back(point(x + lod, z + t));
        for (int t = 0; t < lod; t += (z + lod == size ? edgeStep : lod))
          edge.push_back(point(x + lod - t, z + lod));
        for (int t = 0; t < lod; t += (x == 0 ? edgeStep : lod))
          edge.push_back(point(x, z + lod - t));
        Vec mid{a.x + lod * .5f, terrain(a.x + lod * .5f, a.z + lod * .5f), a.z + lod * .5f};
        for (size_t i = 0; i < edge.size(); i++)
          terrainTriangle(v, mid, edge[i], edge[(i + 1) % edge.size()], tile, tint);
      }
      if ((!geo.active || geo.roof) && std::min({a.y, b.y, c.y, d.y}) < seaLevel) {
        Vec water[] = {
            {a.x, seaLevel, a.z}, {b.x, seaLevel, b.z}, {c.x, seaLevel, c.z}, {d.x, seaLevel, d.z}};
        quad(chunk.water, water, 8, 1.f, {1.f, 1.f, 1.f}, float(lod), float(lod));
      }
    }
  // Mesh independently stacked volumes, using the same fields as collision.
  for (int layer = 0; layer < 3; layer++) {
    auto geology = [layer](float x, float z) { return geologyLayer(x, z, layer); };
    // Interior floors/ceilings share the terrain chunk grid and streaming budget.
    int caveLod = std::min(lod, 4);
    for (int x = 0; x < size; x += caveLod)
      for (int z = 0; z < size; z += caveLod) {
        Vec points[4] = {{float(cx * 16 + x), 0, float(cz * 16 + z)},
                         {float(cx * 16 + x + caveLod), 0, float(cz * 16 + z)},
                         {float(cx * 16 + x + caveLod), 0, float(cz * 16 + z + caveLod)},
                         {float(cx * 16 + x), 0, float(cz * 16 + z + caveLod)}};
        auto mid = geology(points[0].x + caveLod * .5f, points[0].z + caveLod * .5f);
        bool touchesCave = mid.active && mid.cave;

        for (Vec point : points) {
          auto g = geology(point.x, point.z);
          touchesCave |= g.active && g.cave;
        }
        if (!mid.active && !touchesCave)
          continue;
        float referenceFloor = mid.floor;
        if (!mid.active)
          for (auto p : points) {
            auto g = geology(p.x, p.z);
            if (g.active) {
              referenceFloor = g.floor;
              break;
            }
          }
        Vec floor[4];
        for (int i = 0; i < 4; i++) {
          auto g = geology(points[i].x, points[i].z);
          floor[i] = points[i];
          floor[i].y = g.active ? g.floor : referenceFloor;
        }
        bool hasRoof = mid.active && mid.roof;
        for (Vec point : points) {
          auto g = geology(point.x, point.z);
          hasRoof |= g.active && g.roof;
        }
        terrainTriangle(chunk.underground, floor[0], floor[1], floor[2], 3, {.55f, .56f, .6f});
        terrainTriangle(chunk.underground, floor[0], floor[2], floor[3], 3, {.55f, .56f, .6f});
        if (hasRoof) {
          std::vector<Vec> polygon;
          for (int i = 0; i < 4; i++) {
            Vec a = points[i], b = points[(i + 1) % 4];
            bool insideA = geology(a.x, a.z).roof, insideB = geology(b.x, b.z).roof;
            if (insideA)
              polygon.push_back(a);
            if (insideA != insideB) {
              Vec inside = insideA ? a : b, outside = insideA ? b : a;
              for (int iteration = 0; iteration < 12; iteration++) {
                Vec midpoint = (inside + outside) * .5f;
                if (geology(midpoint.x, midpoint.z).roof)
                  inside = midpoint;
                else
                  outside = midpoint;
              }
              polygon.push_back(inside);
            }
          }
          // Closed tunnels use complete roof cells: zero clearance at the outer
          // rock boundary joins ceiling to floor without clipped polygon cracks.
          bool touchesMouth = mid.active && !mid.roof;
          for (Vec point : points) {
            auto g = geology(point.x, point.z);
            touchesMouth |= g.active && !g.roof;
          }
          if (!touchesMouth)
            polygon.assign(std::begin(points), std::end(points));
          for (auto &point : polygon) {
            auto g = geology(point.x, point.z);
            point.y = g.active ? g.ceiling : referenceFloor;
          }
          for (size_t i = 1; i + 1 < polygon.size(); i++)
            terrainTriangle(chunk.underground, polygon[i + 1], polygon[i], polygon[0], 3,
                            {.48f, .49f, .53f});
          // Close the roof lip at the mouth instead of stretching a surface triangle down it.
          for (size_t i = 0; i < polygon.size(); i++) {
            Vec a = polygon[i], b = polygon[(i + 1) % polygon.size()];
            auto ga = geology(a.x, a.z), gb = geology(b.x, b.z);
            if (ga.ceiling - ga.floor < 4.f && gb.ceiling - gb.floor < 4.f) {
              auto renderedFloor = [&](Vec point) {
                float u = std::clamp((point.x - points[0].x) / caveLod, 0.f, 1.f),
                      w = std::clamp((point.z - points[0].z) / caveLod, 0.f, 1.f);
                point.y = u >= w ? floor[0].y + (floor[1].y - floor[0].y) * u +
                                       (floor[2].y - floor[1].y) * w
                                 : floor[0].y + (floor[2].y - floor[3].y) * u +
                                       (floor[3].y - floor[0].y) * w;
                return point;
              };
              Vec lowA = renderedFloor(a), lowB = renderedFloor(b);
              terrainTriangle(chunk.underground, a, b, lowB, 3, {.4f, .41f, .44f});
              terrainTriangle(chunk.underground, a, lowB, lowA, 3, {.4f, .41f, .44f});
            }
            auto nearMouth = [&](Vec point) {
              for (Vec offset :
                   {Vec{.15f, 0, 0}, Vec{-.15f, 0, 0}, Vec{0, 0, .15f}, Vec{0, 0, -.15f}}) {
                auto g = geology(point.x + offset.x, point.z + offset.z);
                if (g.active && !g.roof)
                  return true;
              }
              return false;
            };
            if (nearMouth(a) && nearMouth(b)) {
              Vec topA = a, topB = b;
              topA.y = rawTerrain(a.x, a.z) + entranceRim(a.x, a.z) * 18;
              topB.y = rawTerrain(b.x, b.z) + entranceRim(b.x, b.z) * 18;
              terrainTriangle(chunk.underground, a, topA, topB, 3, {.45f, .46f, .49f});
              terrainTriangle(chunk.underground, a, topB, b, 3, {.45f, .46f, .49f});
            }
          }
        }
        if (layer == 0 && lavaAt(points[0].x + caveLod * .5f, points[0].z + caveLod * .5f)) {
          for (auto &point : floor)
            point.y += .3f;
          quad(chunk.underground, floor, 204, 1, {1, 1, 1}, float(caveLod), float(caveLod));
        }
        if (layer == 0) {
          float lake = caveLakeLevel(points[0].x + caveLod * .5f, points[0].z + caveLod * .5f);
          if (lake < -9000)
            for (auto p : points) {
              float l = caveLakeLevel(p.x, p.z);
              if (l > -9000) {
                lake = l;
                break;
              }
            }
          if (lake > -9000)
            for (auto triangle : {std::array<int, 3>{0, 1, 2}, std::array<int, 3>{0, 2, 3}}) {
              std::vector<Vec> waterPolygon;
              auto wet = [&](Vec p) {
                return caveLakeLevel(p.x, p.z) > -9000 && geologyLayer(p.x, p.z, 0).floor < lake;
              };
              for (int i = 0; i < 3; i++) {
                Vec a = floor[triangle[i]], b = floor[triangle[(i + 1) % 3]];
                bool insideA = wet(a), insideB = wet(b);
                if (insideA)
                  waterPolygon.push_back(a);
                if (insideA != insideB) {
                  Vec inside = insideA ? a : b, outside = insideA ? b : a;
                  for (int n = 0; n < 10; n++) {
                    auto m = (inside + outside) * .5f;
                    if (wet(m))
                      inside = m;
                    else
                      outside = m;
                  }
                  waterPolygon.push_back(inside);
                }
              }
              for (size_t i = 1; i + 1 < waterPolygon.size(); i++)
                for (auto p : {waterPolygon[0], waterPolygon[i], waterPolygon[i + 1]})
                  chunk.caveWater.push_back({p.x, lake, p.z, p.x * .25f, p.z * .25f, 1, 1, 1, -9});
            }
        }
      }
  }
  // Seal underground LOD edges against the shared one-metre field.
  if (lod > 1)
    for (int layer = 0; layer < 3; layer++) {
      auto closeEdge = [&](Vec a, Vec b) {
        auto ga = geologyLayer(a.x, a.z, layer), gb = geologyLayer(b.x, b.z, layer);
        if (!ga.active && !gb.active)
          return;
        int steps = int(std::round(std::hypot(b.x - a.x, b.z - a.z)));
        for (int i = 0; i < steps; i++) {
          float u = float(i) / steps, w = float(i + 1) / steps;
          Vec pa = a + (b - a) * u, pb = a + (b - a) * w;
          auto gpa = geologyLayer(pa.x, pa.z, layer), gpb = geologyLayer(pb.x, pb.z, layer);
          if (!gpa.active || !gpb.active)
            continue;
          for (int roof = 0; roof < 2; roof++) {
            if (roof && (!ga.roof || !gb.roof || !gpa.roof || !gpb.roof))
              continue;
            float ya = roof ? ga.ceiling : ga.floor, yb = roof ? gb.ceiling : gb.floor;
            Vec coarseA = pa, coarseB = pb;
            coarseA.y = ya * (1 - u) + yb * u;
            coarseB.y = ya * (1 - w) + yb * w;
            pa.y = roof ? gpa.ceiling : gpa.floor;
            pb.y = roof ? gpb.ceiling : gpb.floor;
            terrainTriangle(chunk.underground, pa, pb, coarseB, 3, {.55f, .56f, .6f});
            terrainTriangle(chunk.underground, pa, coarseB, coarseA, 3, {.55f, .56f, .6f});
          }
        }
      };
      int step = std::min(lod, 4);
      for (int t = 0; t < size; t += step) {
        closeEdge({float(cx * 16 + t), 0, float(cz * 16)},
                  {float(cx * 16 + t + step), 0, float(cz * 16)});
        closeEdge({float(cx * 16 + t), 0, float(cz * 16 + size)},
                  {float(cx * 16 + t + step), 0, float(cz * 16 + size)});
        closeEdge({float(cx * 16), 0, float(cz * 16 + t)},
                  {float(cx * 16), 0, float(cz * 16 + t + step)});
        closeEdge({float(cx * 16 + size), 0, float(cz * 16 + t)},
                  {float(cx * 16 + size), 0, float(cz * 16 + t + step)});
      }
    }
  // A local horizon approximation darkens enclosed terrain without per-frame work.
  for (auto &vertex : v) {
    int x = int(std::round((vertex.x - cx * 16) / lod)) * lod,
        z = int(std::round((vertex.z - cz * 16) / lod)) * lod;
    if (x >= lod && z >= lod && x <= size - lod && z <= size - lod) {
      float enclosure = std::max(
          0.f, (h[x - lod][z] + h[x + lod][z] + h[x][z - lod] + h[x][z + lod]) * .25f - h[x][z]);
      float ao = std::clamp(1.f - enclosure / (lod * 4.f), .75f, 1.f);
      vertex.r *= ao;
      vertex.g *= ao;
      vertex.b *= ao;
    }
  }
  // Deterministic triangle vegetation, boulders and scattered ground detail.
  const int vegetationStep = size == 16 ? 4 : (lod >= 64 ? 32 : 16);
  for (int x = 2; x < size; x += vegetationStep)
    for (int z = 2; z < size; z += vegetationStep) {
      int wx = cx * 16 + x, wz = cz * 16 + z;
      uint32_t seed = hash(wx, 0, wz);
      float y = surfaceHeight(float(wx), float(wz));
      if (coralHabitat(float(wx), y, float(wz)) && lod <= 2 && seed % 3 != 0) {
        constexpr Vec colors[] = {
            {1.f, .3f, .48f}, {.64f, .38f, 1.f}, {1.f, .75f, .2f}, {.24f, .8f, 1.f}};
        Vec color = colors[(seed / 7) % 4];
        float tall = .55f + (seed % 11) * .09f;
        terrainBox(v, {float(wx), y + tall * .35f, float(wz)}, {.12f, tall * .35f, .12f}, 9, color);
        for (int branch = 0; branch < 3; ++branch) {
          float angle = branch * 2.0944f + (seed % 20) * .1f;
          Vec tip{wx + std::cos(angle) * .55f, y + tall, wz + std::sin(angle) * .55f};
          terrainBox(v, tip, {.18f, .24f, .18f}, 9, color);
          terrainTriangle(v, {float(wx), y + tall * .3f, float(wz)}, tip,
                          {tip.x + .15f, tip.y - .35f, tip.z}, 9, color);
        }
      }
      if (entranceRim(float(wx), float(wz)) > .01f ||
          (geology(float(wx), float(wz)).active && !geology(float(wx), float(wz)).roof) ||
          y < seaLevel + 1 || y > 95)
        continue;
      if (climate(float(wx), float(wz)).aridity > .55f) {
        if (lod <= 2 && seed % 17 == 0) {
          float tall = 1.2f + (seed % 5) * .3f;
          terrainBox(v, {float(wx), y + tall * .5f, float(wz)}, {.18f, tall * .5f, .18f}, 7,
                     {.5f, .85f, .33f});
          terrainBox(v, {wx + .35f, y + tall * .65f, float(wz)}, {.35f, .13f, .15f}, 7,
                     {.5f, .85f, .33f});
        }
        continue;
      }
      if (seed % 7 < 2 && climate(float(wx), float(wz)).aridity < .3f &&
          noise(wx * .008f + 31, wz * .008f) > -.1f) {
        if (size > 16) {
          int gx = (x / lod) * lod, gz = (z / lod) * lod;
          float u = float(x - gx) / lod, w = float(z - gz) / lod;
          float a = h[gx][gz], b = h[gx + lod][gz];
          float c = h[gx + lod][gz + lod], d = h[gx][gz + lod];
          y = u >= w ? a + (b - a) * u + (c - b) * w
                     : a + (c - d) * u + (d - a) * w;
        }
        const float tall = 4 + (seed % 100) * .03f;
        if (lod <= 2)
          terrainBox(v, {float(wx), y + tall * .35f, float(wz)}, {.17f, tall * .35f, .17f}, 5,
                     {.7f, .55f, .4f});
        for (int layer = 0; layer < (lod <= 2 ? 3 : 1); layer++) {
          float base = y + tall * .35f + layer * tall * .18f, radius = 1.7f - layer * .35f;
          Vec top{float(wx), base + tall * .65f, float(wz)};
          for (int k = 0; k < (lod <= 2 ? 6 : 3); k++) {
            float angle = k * 6.2831853f / (lod <= 2 ? 6 : 3),
                  next = (k + 1) * 6.2831853f / (lod <= 2 ? 6 : 3);
            Vec a{wx + std::cos(angle) * radius, base, wz + std::sin(angle) * radius};
            Vec b{wx + std::cos(next) * radius, base, wz + std::sin(next) * radius};
            terrainTriangle(v, a, top, b, 7, {.3f, .69f + layer * .05f, .27f});
          }
        }
      } else if (lod <= 2 && seed % 9 == 0) {
        float r = .3f + (seed % 19) * .035f;
        Vec top{float(wx), y + r * 1.4f, float(wz)};
        Vec points[] = {{wx - r, y, float(wz)},
                        {float(wx), y, wz + r},
                        {wx + r, y, float(wz)},
                        {float(wx), y, wz - r}};
        for (int k = 0; k < 4; k++)
          terrainTriangle(v, points[k], top, points[(k + 1) % 4], 3, {.66f, .68f, .7f});
      } else if (lod == 1 && seed % 3 == 0) {
        Vec a{wx - .2f, y, float(wz)}, b{wx + .2f, y, float(wz)}, c{float(wx), y + .5f, float(wz)};
        terrainTriangle(v, a, b, c, 7, {.42f, .78f, .23f});
      }
    }
  // Distant patches use a shared 16-metre grid; skirts seal their fine-grid boundary.
  if (size > 16) {
    auto skirt = [&](Vec a, Vec b) {
      Vec midpoint = (a + b) * .5f;
      if (geology(a.x, a.z).active || geology(b.x, b.z).active ||
          geology(midpoint.x, midpoint.z).active || std::abs(a.y - b.y) > 12.f)
        return;
      Vec c = b - Vec{0, 4, 0}, d = a - Vec{0, 4, 0};
      Vec tint = terrainTint(a.x, a.y, a.z);
      terrainTriangle(v, a, b, c, 3, tint);
      terrainTriangle(v, a, c, d, 3, tint);
    };
    for (int t = 0; t < size; t += edgeStep) {
      skirt(point(t, 0), point(t + edgeStep, 0));
      skirt(point(t, size), point(t + edgeStep, size));
      skirt(point(0, t), point(0, t + edgeStep));
      skirt(point(size, t), point(size, t + edgeStep));
    }
  }
  auto bounds = [&](const std::vector<Vertex> &vertices, Vec &low, Vec &high) {
    low = {float(cx * 16), std::numeric_limits<float>::max(), float(cz * 16)};
    high = {float(cx * 16 + size), -std::numeric_limits<float>::max(), float(cz * 16 + size)};
    for (auto &p : vertices) {
      low.y = std::min(low.y, p.y);
      high.y = std::max(high.y, p.y);
    }
  };
  bounds(chunk.vertices, chunk.min, chunk.max);
  bounds(chunk.underground, chunk.undergroundMin, chunk.undergroundMax);
  if (!chunk.water.empty())
    chunk.max.y = std::max(chunk.max.y, seaLevel);
  return chunk;
}
inline bool inFrustum(const Camera &camera, Vec low, Vec high) {
  auto &m = camera.vp;
  for (int plane = 0; plane < 6; plane++) {
    float p[4];
    for (int k = 0; k < 4; k++) {
      float w = m[k * 4 + 3];
      p[k] = plane == 0   ? w + m[k * 4]
             : plane == 1 ? w - m[k * 4]
             : plane == 2 ? w + m[k * 4 + 1]
             : plane == 3 ? w - m[k * 4 + 1]
             : plane == 4 ? m[k * 4 + 2]
                          : w - m[k * 4 + 2];
    }
    Vec support{p[0] >= 0 ? high.x : low.x, p[1] >= 0 ? high.y : low.y, p[2] >= 0 ? high.z : low.z};
    if (dot(support, {p[0], p[1], p[2]}) + p[3] < 0)
      return false;
  }
  return true;
}
inline void drawPlayer(std::vector<Vertex> &v, const Player &p) {
  size_t start = v.size();
  auto skinBox = [&](Vec center, Vec half, int tx, int ty, int width, int depth, int height) {
    Vec corners[8];
    for (int i = 0; i < 8; ++i)
      corners[i] = center + Vec{(i & 1) ? half.x : -half.x, (i & 2) ? half.y : -half.y,
                                (i & 4) ? half.z : -half.z};
    const int faces[6][4] = {{0, 2, 3, 1}, {5, 7, 6, 4}, {4, 6, 2, 0},
                             {1, 3, 7, 5}, {2, 6, 7, 3}, {4, 0, 1, 5}};
    const int rectangles[6][4] = {{tx + 2 * depth + width, ty + depth, width, height},
                                  {tx + depth, ty + depth, width, height},
                                  {tx, ty + depth, depth, height},
                                  {tx + depth + width, ty + depth, depth, height},
                                  {tx + depth, ty, width, depth},
                                  {tx + depth + width, ty, width, depth}};
    for (int face = 0; face < 6; face++) {
      auto &r = rectangles[face];
      float uv[4][2] = {{float(r[0]), float(r[1] + r[3])},
                        {float(r[0]), float(r[1])},
                        {float(r[0] + r[2]), float(r[1])},
                        {float(r[0] + r[2]), float(r[1] + r[3])}};
      for (int index : {0, 1, 2, 0, 2, 3}) {
        Vec point = corners[faces[face][index]];
        v.push_back(
            {point.x, point.y, point.z, uv[index][0] / 64, uv[index][1] / 64, 1, 1, 1, -201});
      }
    }
  };
  skinBox({0, 1.5f, 0}, {.25f, .25f, .25f}, 0, 0, 8, 8, 8);
  skinBox({0, 1.f, 0}, {.25f, .375f, .125f}, 16, 16, 8, 4, 12);
  skinBox({-.375f, 1.f, 0}, {skinSlim ? .09375f : .125f, .375f, .125f}, 40, 16, skinSlim ? 3 : 4, 4,
          12);
  skinBox({.375f, 1.f, 0}, {skinSlim ? .09375f : .125f, .375f, .125f}, 32, 48, skinSlim ? 3 : 4, 4,
          12);
  skinBox({-.125f, .375f, 0}, {.125f, .375f, .125f}, 0, 16, 4, 4, 12);
  skinBox({.125f, .375f, 0}, {.125f, .375f, .125f}, 16, 48, 4, 4, 12);
  skinBox({0, 1.5f, 0}, {.255f, .255f, .255f}, 32, 0, 8, 8, 8);
  skinBox({0, 1.f, 0}, {.255f, .38f, .13f}, 16, 32, 8, 4, 12);
  skinBox({-.375f, 1.f, 0}, {skinSlim ? .099f : .13f, .38f, .13f}, 40, 32, skinSlim ? 3 : 4, 4, 12);
  skinBox({.375f, 1.f, 0}, {skinSlim ? .099f : .13f, .38f, .13f}, 48, 48, skinSlim ? 3 : 4, 4, 12);
  skinBox({-.125f, .375f, 0}, {.13f, .38f, .13f}, 0, 32, 4, 4, 12);
  skinBox({.125f, .375f, 0}, {.13f, .38f, .13f}, 0, 48, 4, 4, 12);
  if (p.equipped()) {
    Vec base = p.flying ? Vec{0, .9f, 0} : Vec{0, 1.15f, -.2f};
    for (float sign : {-1.f, 1.f}) {
      Vec outer = base + (p.flying ? Vec{sign * 1.25f, -.7f, .12f} : Vec{sign * .45f, 0, -.7f});
      terrainTriangle(v, base, outer,
                      base + (p.flying ? Vec{sign * .15f, -1.1f, 0} : Vec{sign * .15f, 0, -.5f}), 9,
                      {.45f, .47f, .62f});
    }
  }
  Vec forward = p.look(), right{std::cos(p.yaw), 0, std::sin(p.yaw)};
  Vec up = cross(right, forward);
  Vec bankedRight = right * std::cos(p.roll) - up * std::sin(p.roll);
  Vec bankedUp = up * std::cos(p.roll) + right * std::sin(p.roll);
  if (p.barrelFlight && p.flying) {
    bankedRight = p.flightRight;
    bankedUp = p.flightUp;
  }
  for (size_t i = start; i < v.size(); ++i) {
    auto &vertex = v[i];
    Vec local{vertex.x, vertex.y, vertex.z};
    Vec position = p.flying ? p.pos + bankedRight * local.x + forward * (local.y - .9f) +
                                  bankedUp * (local.z + .3f)
                            : p.pos + right * local.x + Vec{0, local.y, 0} +
                                  Vec{std::sin(p.yaw), 0, -std::cos(p.yaw)} * local.z;
    vertex.x = position.x;
    vertex.y = position.y;
    vertex.z = position.z;
  }
}
