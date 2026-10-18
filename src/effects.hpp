#pragma once
#include "world.hpp"
struct Particle {
  Vec pos, vel;
  float life;
  Vec color;
  bool rocket = false;
};
class Effects {
  std::vector<Particle> particles;
  uint32_t serial = 0;

public:
  void spawn(Vec pos, Vec vel, float life, Vec color, bool rocket = false) {
    if (particles.size() < 384)
      particles.push_back({pos, vel, life, color, rocket});
  }
  void launch(Vec pos, Vec direction) {
    spawn(pos, direction * 5 + Vec{0, 10, 0}, 1.5f, {1, 1, 1}, true);
  }
  bool tick(float dt, const Player &p) {
    std::vector<Vec> explosions;
    for (auto &v : particles) {
      v.life -= dt;
      v.pos = v.pos + v.vel * dt;
      if (v.rocket) {
        v.vel.y += dt * 4;
        if (v.life <= 0)
          explosions.push_back(v.pos);
      } else
        v.vel.y -= dt * 2;
    }
    std::erase_if(particles, [](const Particle &v) { return v.life <= 0; });
    for (Vec pos : explosions)
      for (int i = 0; i < 48; i++) {
        serial++;
        Vec d{noise(float(serial) * 7.3f, 4), noise(float(serial) * 2.4f, 81),
              noise(float(serial) * 8.1f, 3)};
        spawn(pos, normalized(d) * 8, 1.f, {1, .64f, .21f});
      }
    if (p.rocketTime > 0) {
      serial++;
      float spread = noise(float(serial), 3) * .18f;
      spawn(p.pos + Vec{spread, 1, spread}, p.look() * -3, .35f, {1, .9f, .49f});
    }
    return !explosions.empty();
  }
  void draw(std::vector<Vertex> &v, const Player &p) const {
    Vec right{std::cos(p.yaw), 0, std::sin(p.yaw)}, up = normalized(cross(right, p.look()));
    for (const auto &s : particles) {
      float size = s.rocket ? .14f : .045f;
      Vec r = right * size, u = up * size;
      Vec points[] = {s.pos - r - u, s.pos + r - u, s.pos + r + u, s.pos - r + u};
      quad(v, points, s.rocket ? 11 : 9, 1, s.color);
    }
  }
};
