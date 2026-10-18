#pragma once
#include "world.hpp"
#include <array>
// A session-wide clock: six exact 180-second phases, independent of biome.
enum class WeatherState { Rain, ThunderRain, ThunderSnow, Snow, RainAgain, Clear };
struct WeatherSample {
  WeatherState state;
  bool rain, snow, thunder;
  float cloud;
};
inline WeatherSample weatherAt(double seconds) {
  double cycle = std::fmod(std::max(0., seconds), 1080.);
  int phase = int(cycle / 180.);
  return {WeatherState(phase), phase == 0 || phase == 1 || phase == 4, phase == 2 || phase == 3,
          phase == 1 || phase == 2,
          phase == 5                 ? 0.f
          : phase == 1 || phase == 2 ? 1.f
                                     : .7f};
}
class Weather {
  double time = 0, nextStrike = 192, thunderDue = -1;
  float flash = 0, visibleBolt = 0;
  Vec strike{};
  uint32_t serial = 0;

public:
  WeatherSample sample = weatherAt(0);
  void start(double seconds) {
    time = std::max(0., seconds);
    sample = weatherAt(time);
    nextStrike = time + 3;
  }
  bool tick(double elapsed, Vec eye) {
    double previous = time;
    time += std::max(0., elapsed);
    sample = weatherAt(time);
    flash = std::max(0.f, flash - float(elapsed) * 4.f);
    visibleBolt = std::max(0.f, visibleBolt - float(elapsed));
    bool thunder = false;
    if (thunderDue >= 0 && time >= thunderDue) {
      thunder = true;
      thunderDue = -1;
    }
    if (sample.thunder && time >= nextStrike) {
      ++serial;
      float angle = float(hash(int(serial), 91, 7) & 65535) * 6.2831853f / 65535;
      float distance = 45 + hash(int(serial), 17, 3) % 70;
      strike = {eye.x + std::sin(angle) * distance, 0, eye.z + std::cos(angle) * distance};
      strike.y = std::max(seaLevel, surfaceHeight(strike.x, strike.z));
      flash = 1;
      visibleBolt = .22f;
      thunderDue = time + distance / 343.;
      nextStrike = time + 10 + hash(int(serial), 38, 1) % 13;
    } else if (!sample.thunder && weatherAt(previous).thunder)
      nextStrike = time + 3;
    return thunder;
  }
  void apply(Camera &camera) const {
    camera.environment[1] = sample.cloud;
    camera.environment[2] = flash;
    camera.environment[3] = sample.state == WeatherState::Clear ? 0.f : 1.f;
  }
  void draw(std::vector<Vertex> &vertices, const Camera &camera, const Player &view) const {
    Vec eye{camera.eye[0], camera.eye[1], camera.eye[2]};
    Vec right{std::cos(view.yaw), 0, std::sin(view.yaw)};
    Vec up = normalized(cross(right, view.look()));
    if (view.barrelFlight && view.flying) {
      right = view.flightRight;
      up = view.flightUp;
    }
    auto ribbon = [&](Vec a, Vec b, Vec side, int material, Vec color) {
      Vec points[] = {a - side, b - side, b + side, a + side};
      quad(vertices, points, material, 1, color);
    };
    if (sample.rain || sample.snow) {
      int bx = int(std::floor(eye.x / 7)), bz = int(std::floor(eye.z / 7));
      float base = std::floor(eye.y / 16) * 16 - 16;
      for (int x = -5; x <= 5; x++)
        for (int z = -5; z <= 5; z++)
          for (int layer = 0; layer < (sample.snow ? 5 : 3); layer++) {
            uint32_t h = hash(bx + x, layer, bz + z, 8473);
            float wx = (bx + x) * 7 + float(h & 255) / 36.5f,
                  wz = (bz + z) * 7 + float((h >> 8) & 255) / 36.5f;
            double progress = std::fmod(time * (sample.rain ? 23. : 1.6) +
                                            float((h >> 16) & 65535) / 65535 * 48 + layer * 16,
                                        48.);
            Vec position{wx, base + 48 - float(progress), wz};
            if (sample.snow) {
              position.x += std::sin(float(time) * .8f + float(h & 31)) * .6f;
              position.z += std::cos(float(time) * .6f + float(h & 31)) * .6f;
            }
            if (position.y < std::max(surfaceHeight(position.x, position.z), seaLevel) + .1f)
              continue;
            if (sample.rain)
              ribbon(position, position + Vec{-.15f, -1.5f, 0}, right * .022f, 202, {1, 1, 1});
            else {
              Vec r = right * .12f, u = up * .12f;
              Vec points[] = {position - r - u, position + r - u, position + r + u,
                              position - r + u};
              quad(vertices, points, 201, 1);
            }
          }
    }
    if (visibleBolt > 0) {
      Vec last = strike + Vec{0, 55, 0};
      for (int i = 1; i <= 8; i++) {
        Vec next = strike + Vec{float(int(hash(i, int(serial), 0) % 7) - 3), 55 * (1 - i / 8.f),
                                float(int(hash(i, int(serial), 1) % 7) - 3)};
        ribbon(last, next, right * .18f, 203, {1, 1, 1});
        if (i == 3 || i == 5)
          ribbon(next, next + Vec{7, -9, 3}, right * .1f, 203, {1, 1, 1});
        last = next;
      }
    }
  }
};
