#pragma once
#include "world.hpp"
#include "font_metrics.hpp"
#include <string>
inline float textWidth(const std::string &s, float size, float aspect) {
  float width = 0;
  for (unsigned char c : s)
    width += fontAdvance[(c < 32 || c > 126 ? '?' : c) - 32];
  return width * size * 2.15f / aspect;
}
inline void panel(std::vector<Vertex> &v, float x, float y, float w, float h, Vec color) {
  for (int i : {0, 1, 2, 0, 2, 3})
    v.push_back({x + (i == 1 || i == 2 ? w : 0), y + (i >= 2 ? h : 0), 0, .5f / 512, 32.5f / 512,
                 color.x, color.y, color.z, 1});
}
inline void text(std::vector<Vertex> &v, const std::string &s, float x, float y, float size,
                 float aspect, Vec color = {1, 1, 1}) {
  size *= 2.15f;
  for (unsigned char c : s) {
    if (c < 32 || c > 126)
      c = '?';
    int tile = c - 32;
    float u0 = (tile % 16) * 8.f / 512, u1 = u0 + 8.f / 512, v0 = (64 + (tile / 16) * 8.f) / 512,
          v1 = v0 + 8.f / 512;
    float w = size / aspect;
    float px[4] = {x, x + w, x + w, x}, py[4] = {y, y, y + size, y + size};
    float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
    for (int pass = 0; pass < 2; pass++)
      for (int i : {0, 1, 2, 0, 2, 3}) {
        Vec tint = pass ? color : Vec{.01f, .016f, .02f};
        float shadow = pass ? 0 : .003f;
        v.push_back({px[i] + shadow / aspect, py[i] + shadow, 0, uv[i][0], uv[i][1], tint.x, tint.y,
                     tint.z, 1});
      }
    x += size * fontAdvance[tile] / aspect;
  }
}
struct ButtonRect {
  float x, y, w, h;
  bool contains(float mx, float my) const { return mx >= x && mx < x + w && my >= y && my < y + h; }
};
inline ButtonRect equipmentButton(float aspect) {
  return {-.4f / aspect, -.35f, .72f / aspect, .105f};
}
inline ButtonRect pauseButton(int i, float aspect) {
  return {-.65f / aspect, -.46f + i * .15f, 1.3f / aspect, .14f};
}
inline void button(std::vector<Vertex> &v, ButtonRect r, const std::string &label, float aspect,
                   float mx, float my, bool enabled = true) {
  int state = !enabled ? 2 : r.contains(mx, my) ? 1 : 0;
  // Use the upstream nine-slice metadata: 200 x 20 texture, 3-pixel border.
  float borderY = r.h * 3 / 20, borderX = borderY / aspect;
  float px[] = {r.x, r.x + borderX, r.x + r.w - borderX, r.x + r.w};
  float py[] = {r.y, r.y + borderY, r.y + r.h - borderY, r.y + r.h};
  float uvx[] = {.5f / 512, 3.f / 512, 197.f / 512, 199.5f / 512};
  float base = 384 + state * 24;
  float uvy[] = {(base + .5f) / 512, (base + 3) / 512, (base + 17) / 512, (base + 19.5f) / 512};
  for (int y = 0; y < 3; y++)
    for (int x = 0; x < 3; x++) {
      int ix[] = {x, x + 1, x + 1, x}, iy[] = {y, y, y + 1, y + 1};
      for (int i : {0, 1, 2, 0, 2, 3})
        v.push_back({px[ix[i]], py[iy[i]], 0, uvx[ix[i]], uvy[iy[i]], 1, 1, 1, 1.25f});
    }
  float size = .04f;
  text(v, label, r.x + (r.w - textWidth(label, size, aspect)) * .5f,
       r.y + (r.h - size * 2.15f) * .5f - .005f, size, aspect,
       enabled ? Vec{1, 1, 1} : Vec{.65f, .65f, .65f});
}
inline ButtonRect controlsBackButton(float aspect) {
  return {-.55f / aspect, .64f, 1.1f / aspect, .15f};
}
inline void drawControlsPopup(std::vector<Vertex> &v, float aspect, float mx, float my) {
  panel(v, -.98f / aspect, -.88f, 1.96f / aspect, 1.77f, {.045f, .065f, .083f});
  text(v, "CONTROLS", -textWidth("CONTROLS", .06f, aspect) * .5f, -.79f, .06f, aspect);
  const char *lines[] = {"WASD: move   Mouse: look",
                         "Ctrl: sprint   Shift: crouch",
                         "Space: jump / swim upward",
                         "G: equip / exit   Space: launch flight",
                         "R / RMB: sustained firework boost",
                         "H: third-person free look   Enter: camera",
                         "M: exploration map / waypoints",
                         "Flight: mouse X roll / mouse Y pitch",
                         "E / Tab / I: inventory   Esc: pause",
                         "Free look keeps player moving",
                         "Right click split   Drag distribute",
                         "1-9: swap   F: offhand   Double click: collect",
                         "RMB: rocket   F5: save   [ / ]: view"};
  for (int i = 0; i < 13; i++)
    text(v, lines[i], -.9f / aspect, -.58f + i * .095f, .028f, aspect);
  button(v, controlsBackButton(aspect), "Back", aspect, mx, my);
}
inline void sprite(std::vector<Vertex> &v, int tile, float x, float y, float size, float aspect,
                   Vec tint = {1, 1, 1}) {
  if (tile < 0)
    return;
  float px[4] = {x, x + size / aspect, x + size / aspect, x}, py[4] = {y, y, y + size, y + size};
  float uv[4][2] = {{.001f, .001f}, {.999f, .001f}, {.999f, .999f}, {.001f, .999f}};
  for (int i : {0, 1, 2, 0, 2, 3})
    v.push_back({px[i], py[i], 0, uv[i][0], uv[i][1], tint.x, tint.y, tint.z, float(tile + 2)});
}
struct InventoryLayout {
  float aspect, pixel = 1.42f / 166, w, h, x, top = -.71f;
  explicit InventoryLayout(float ratio)
      : aspect(ratio), w(18 * pixel / ratio), h(18 * pixel), x(-80 * pixel / ratio) {}
  float left() const { return -88 * pixel / aspect; }
  float slotX(int i) const {
    float px = 8;
    if (i < 36)
      px = 8 + (i % 9) * 18;
    else if (i == 40)
      px = 77;
    else if (i >= 41 && i <= 44)
      px = 98 + ((i - 41) % 2) * 18;
    else if (i == 45)
      px = 154;
    return left() + px * pixel / aspect;
  }
  float slotY(int i) const {
    float py = 8;
    if (i < 9)
      py = 142;
    else if (i < 36)
      py = 84 + ((i - 9) / 9) * 18;
    else if (i == 36)
      py = 26;
    else if (i == 38)
      py = 44;
    else if (i == 39 || i == 40)
      py = 62;
    else if (i >= 41 && i <= 44)
      py = 26 + ((i - 41) / 2) * 18;
    else if (i == 45)
      py = 26;
    return top + py * pixel;
  }
  int hit(float mx, float my) const {
    for (int i = 0; i <= 45; i++)
      if (mx >= slotX(i) && mx < slotX(i) + w && my >= slotY(i) && my < slotY(i) + h)
        return i;
    return -1;
  }
};
inline void drawSlot(std::vector<Vertex> &v, const Stack &s, int number, float x, float y, float w,
                     float h, float aspect, bool selected) {
  panel(v, x, y, w - .005f, h - .007f, selected ? Vec{.74f, .89f, .64f} : Vec{.21f, .29f, .34f});
  panel(v, x + .004f, y + .006f, w - .013f, h - .019f, {.085f, .125f, .15f});
  if (!s.empty()) {
    sprite(v, itemTile(s.item), x + .026f / aspect, y + .025f, .12f, aspect);
    if (s.count > 1)
      text(v, std::to_string(s.count),
           x + w - .008f - textWidth(std::to_string(s.count), .032f, aspect), y + h - .073f, .032f,
           aspect);
  }
  if (number >= 0)
    text(v, std::to_string(number + 1), x + .008f / aspect, y - .005f, .024f, aspect,
         {.62f, .71f, .75f});
}
inline void drawInventory(std::vector<Vertex> &v, const Player &p, float aspect, float mx, float my,
                          const std::string &notice = "") {
  InventoryLayout l(aspect);
  float x = l.left(), y = l.top, w = 176 * l.pixel / aspect, h = 166 * l.pixel;
  float px[] = {x, x + w, x + w, x}, py[] = {y, y, y + h, y + h};
  float uvx[] = {256.f / 512, 432.f / 512, 432.f / 512, 256.f / 512};
  float uvy[] = {128.f / 512, 128.f / 512, 294.f / 512, 294.f / 512};
  for (int i : {0, 1, 2, 0, 2, 3})
    v.push_back({px[i], py[i], 0, uvx[i], uvy[i], 1, 1, 1, 1.25f});
  text(v, "Crafting", l.left() + 97 * l.pixel / aspect, l.top + 14 * l.pixel, .032f, aspect,
       {.25f, .25f, .25f});
  for (int i = 0; i <= 45; i++) {
    Stack result = i == 45 ? p.inventory.output() : *p.inventory.slot(i);
    float sx = l.slotX(i), sy = l.slotY(i);
    if (!result.empty()) {
      sprite(v, itemTile(result.item), sx + l.pixel / aspect, sy + l.pixel, 16 * l.pixel, aspect);
      if (result.count > 1)
        text(v, std::to_string(result.count),
             sx + l.w - textWidth(std::to_string(result.count), .025f, aspect), sy + 9 * l.pixel,
             .025f, aspect);
      float maximum = result.item == Item::Helmet       ? 165
                      : result.item == Item::Chestplate ? 240
                      : result.item == Item::Leggings   ? 225
                      : result.item == Item::Boots      ? 195
                                                        : 432;
      if (stackLimit(result.item) == 1 && result.durability < maximum) {
        panel(v, sx + 2 * l.pixel / aspect, sy + 14 * l.pixel, 13 * l.pixel / aspect, 2 * l.pixel,
              {.05f, .05f, .05f});
        panel(v, sx + 2 * l.pixel / aspect, sy + 14 * l.pixel,
              13 * l.pixel / aspect * std::clamp(result.durability / maximum, 0.f, 1.f), l.pixel,
              {.25f, .8f, .15f});
      }
    }
  }
  // Lightweight player silhouette in the authentic preview region.
  panel(v, l.left() + 42 * l.pixel / aspect, l.top + 17 * l.pixel, 12 * l.pixel / aspect,
        12 * l.pixel, {.63f, .43f, .28f});
  panel(v, l.left() + 38 * l.pixel / aspect, l.top + 29 * l.pixel, 20 * l.pixel / aspect,
        22 * l.pixel, {.15f, .61f, .66f});
  panel(v, l.left() + 38 * l.pixel / aspect, l.top + 51 * l.pixel, 8 * l.pixel / aspect,
        20 * l.pixel, {.22f, .22f, .55f});
  panel(v, l.left() + 50 * l.pixel / aspect, l.top + 51 * l.pixel, 8 * l.pixel / aspect,
        20 * l.pixel, {.22f, .22f, .55f});
  int hover = l.hit(mx, my);
  if (hover >= 0 && p.inventory.cursor.empty()) {
    Stack s = hover == 45 ? p.inventory.output() : *p.inventory.slot(hover);
    if (!s.empty()) {
      std::string label(itemName(s.item));
      float tw = textWidth(label, .032f, aspect);
      float tx = std::min(mx + .025f / aspect, .97f - tw), ty = std::min(my + .025f, .84f);
      panel(v, tx - .01f, ty - .01f, tw + .025f, .085f, {.055f, .015f, .075f});
      text(v, label, tx, ty, .032f, aspect);
    }
  }
  if (!notice.empty())
    text(v, notice, l.left(), .77f, .03f, aspect, {1, .85f, .5f});
  if (!p.inventory.cursor.empty()) {
    sprite(v, itemTile(p.inventory.cursor.item), mx - 8 * l.pixel / aspect, my - 8 * l.pixel,
           16 * l.pixel, aspect);
    if (p.inventory.cursor.count > 1)
      text(v, std::to_string(p.inventory.cursor.count), mx, my, .03f, aspect);
  }
}
inline void drawSurvivalHud(std::vector<Vertex> &v, const Player &p, float aspect) {
  InventoryLayout l(aspect);
  float y = .79f;
  for (int i = 0; i < 9; i++)
    drawSlot(v, p.inventory.slots[i], -1, l.slotX(i), y, l.w, l.h, aspect,
             i == p.inventory.selected);
  float size = .055f, gap = .061f / aspect;
  for (int i = 0; i < 10; i++) {
    sprite(v, 15, l.x + i * gap, .66f, size, aspect);
    if (p.health > i * 2)
      sprite(v, 14, l.x + i * gap, .66f, size, aspect);
    float x = l.x + 9 * l.w - 10 * gap + i * gap;
    sprite(v, 17, x, .66f, size, aspect);
    if (p.hunger > i * 2)
      sprite(v, 16, x, .66f, size, aspect);
  }
  if (p.eatTime > 0) {
    panel(v, l.x, .61f, 9 * l.w, .015f, {.18f, .22f, .26f});
    panel(v, l.x, .61f, 9 * l.w * std::min(1.f, p.eatTime / 1.6f), .015f, {.89f, .64f, .25f});
  }
  if (p.rocketTime > 0) {
    panel(v, l.x, .59f, 9 * l.w * p.rocketTime / 1.5f, .012f, {.63f, .86f, 1});
  }
}
