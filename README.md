# Z3

A compact Windows game built with C++26 and Vulkan: seeded triangle terrain,
branching caves, oceans and deserts, mouse-controlled elytra flight, weather,
and a persistent exploration map.

## Play

Double-click `Z3.exe`. The game embeds its textures, font and shaders; no scripts
or companion runtime files are required. A Vulkan-capable graphics driver is
required. Worlds, exploration data and imported skins are saved beside the
executable. Keep the executable in a writable folder.

The executable is under 10 MB. Generated builds and personal saves are excluded
from this repository. [README.txt](README.txt) contains controls and detailed
feature descriptions.

## Build on Windows

Install MSYS2 UCRT64 and, from its UCRT64 terminal, install:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-glfw mingw-w64-ucrt-x86_64-python
```

Install the LunarG Vulkan SDK (including `glslc`). Open PowerShell with the
UCRT64 `bin` directory on `PATH` (normally `C:\msys64\ucrt64\bin`), and set
`VULKAN_SDK` to your SDK directory if the SDK installer has not already set it.
GCC 14 or newer is required; the project uses experimental `-std=c++26` support.

```powershell
./build.ps1
```

The build compiles shaders, embeds resources, runs simulation tests, verifies
standalone executable imports and checks the entire workspace size. Output:
`dist/Z3.exe`. Build tools and scripts are developer tools only.

## Verification

```powershell
ctest --test-dir build --output-on-failure
./dist/Z3.exe --smoke --frames=120 --validate
```

Smoke runs are muted. Vulkan validation requires the SDK validation layer.
See [PERFORMANCE_NOTES.txt](PERFORMANCE_NOTES.txt) for measured results and
limitations. Native resolution is the default; the game targets 60 FPS but
currently runs below that in demanding cave views. This is an experimental
sandbox, not full Minecraft gameplay or generation parity.

## Repository layout

- `src/`: simulation, world generation, Vulkan renderer and UI.
- `shaders/`: GLSL sources compiled during the build.
- `assets/`, `fonts/`: embedded resources.
- `tools/`: resource generation and packaging checks.
- `licenses/`: third-party notices.

## Credits and licensing

This is an independent project and is not affiliated with Mojang or Microsoft.
Minecraft-derived textures, UI, font and skin resources retain their respective
owners' rights. Their presence here does not grant a license to reuse them.
See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). No blanket open-source
license has been selected for Z3's original code; do not assume MIT or another
license applies.

## Publish this checkout

Create an **empty** GitHub repository named `Z3` (without an initial README,
license or gitignore), then use its actual URL:

```powershell
git remote add origin https://github.com/YOUR-USERNAME/Z3.git
git push -u origin main
```

Push `main` only. Old asset-repository branches remain local for recovery;
`upstream` records asset provenance and has pushing disabled. Do not use
`--mirror` or `--all`. No GitHub repository has been created or pushed by this
preparation step.
