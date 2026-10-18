#pragma once
#include "world.hpp"
#include <array>
#include <windows.h>
#include <mmsystem.h>
// Bounded stereo mixer. Inverse-square falloff, head-relative pan, voxel
// occlusion, material footsteps and a local shoreline source. No unbounded
// audio allocations.
class Audio {
  HWAVEOUT output{};
  std::array<std::array<short, 2048>, 4> samples{};
  std::array<WAVEHDR, 4> headers{};
  double time = 0;
  float step = 0, filterL = 0, filterR = 0;
  uint32_t random = 19;
  float rocketSound = 0, burstSound = 0, thunderSound = 0, thunderFilter = 0;

public:
  explicit Audio(bool muted = false) {
    if (muted)
      return;
    WAVEFORMATEX f{};
    f.wFormatTag = WAVE_FORMAT_PCM;
    f.nChannels = 2;
    f.nSamplesPerSec = 22050;
    f.wBitsPerSample = 16;
    f.nBlockAlign = 4;
    f.nAvgBytesPerSec = 88200;
    if (waveOutOpen(&output, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
      output = nullptr;
      return;
    }
    for (int i = 0; i < 4; i++) {
      headers[i].lpData = (char *)samples[i].data();
      headers[i].dwBufferLength = 4096;
      waveOutPrepareHeader(output, &headers[i], sizeof(WAVEHDR));
    }
  }
  ~Audio() {
    if (output) {
      waveOutReset(output);
      for (auto &h : headers)
        waveOutUnprepareHeader(output, &h, sizeof(h));
      waveOutClose(output);
    }
  }
  void footstep() { step = 1; }
  void rocket() { rocketSound = 1; }
  void burst() { burstSound = 1; }
  void thunder() { thunderSound = 1; }
  void update(const Player &p, const Edits &edits, float rain = 0) {
    if (!output)
      return;
    bool bufferReady = false;
    for (const auto &h : headers)
      if ((h.dwFlags & WHDR_INQUEUE) == 0)
        bufferReady = true;
    if (!bufferReady)
      return;
    Vec source = p.pos;
    float distance = 100;
    for (int x = -16; x <= 16; x += 4)
      for (int z = -16; z <= 16; z += 4) {
        int wx = int(p.pos.x) + x, wz = int(p.pos.z) + z;
        if (height(wx, wz) >= 28)
          continue;
        Vec s{float(wx), 29, float(wz)};
        float d = length(s - p.pos);
        if (d < distance) {
          distance = d;
          source = s;
        }
      }
    Vec direction = normalized(source - p.pos);
    float pan = dot(direction, {std::cos(p.yaw), 0, std::sin(p.yaw)}),
          gain = .18f / (1 + distance * distance * .035f);
    int walls = 0;
    for (int i = 1; i < 8; i++) {
      Vec v = p.pos + Vec{0, 1, 0} + direction * (distance * i / 8);
      if (p.smoothTerrain ? v.y < surfaceHeight(v.x, v.z)
                          : solid(block(int(std::floor(v.x)), int(std::floor(v.y)),
                                        int(std::floor(v.z)), edits)))
        walls++;
    }
    gain *= std::pow(.72f, float(walls));
    float wind = std::min(.25f, length(p.vel) * .006f), cut = walls ? .08f : .35f;
    for (int b = 0; b < 4; b++) {
      auto &header = headers[b];
      if ((header.dwFlags & WHDR_INQUEUE) != 0)
        continue;
      for (int i = 0; i < 1024; i++) {
        random = random * 1664525u + 1013904223u;
        float n = float((random >> 8) & 65535) / 32768 - 1;
        float water = n * (.65f + .35f * std::sin(float(time) * 2));
        float material = p.wet ? .8f : .5f;
        float foot = step * material * (n * .5f + std::sin(float(time) * 320) * .5f);
        step *= .9988f;
        float firework =
            rocketSound * (n * .3f + std::sin(float(time) * (800 + rocketSound * 1400)) * .08f) +
            burstSound * n * .5f;
        rocketSound *= .9998f;
        burstSound *= .9995f;
        thunderFilter += (n - thunderFilter) * .012f;
        float weatherNoise = n * rain + thunderFilter * thunderSound * .65f;
        thunderSound *= .999965f;
        float left = water * gain * std::sqrt((1 - pan) * .5f) + n * wind + foot + firework +
                     weatherNoise,
              right = water * gain * std::sqrt((1 + pan) * .5f) + n * wind + foot + firework +
                      weatherNoise;
        filterL += (left - filterL) * cut;
        filterR += (right - filterR) * cut;
        samples[b][i * 2] = short(std::clamp(filterL, -1.f, 1.f) * 18000);
        samples[b][i * 2 + 1] = short(std::clamp(filterR, -1.f, 1.f) * 18000);
        time += 1. / 22050;
      }
      waveOutWrite(output, &header, sizeof(header));
    }
  }
};
