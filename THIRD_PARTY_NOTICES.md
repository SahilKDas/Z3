# Third-party notices

Z3's source and resource directories contain material with different ownership
and licensing terms. No project-wide license overrides third-party rights.

- Minecraft-derived resources in `assets/`, plus `fonts/english.png` and
  `fonts/default-skin.png`, retain Mojang/Microsoft or their respective owners'
  rights. The initial asset source was
  https://github.com/InventivetalentDev/minecraft-assets/tree/26.4-snapshot-3 .
  The upstream repository is an asset source, not a license grant for Minecraft
  content. Some local resource copies were adapted for embedding.
- `fonts/Nunito.ttf`: Nunito, distributed under the SIL Open Font License;
  see `fonts/OFL.txt`. The current in-game bitmap font is Minecraft-derived.
- GLFW: see `licenses/GLFW.txt`.
- MinGW runtime: see `licenses/MinGW-runtime.txt`.
- winpthreads: see `licenses/winpthreads.txt`.

The game links the installed Windows/Vulkan graphics runtime. These drivers
and the Vulkan SDK are not included in this repository. Keep applicable
third-party notices with distributions. See `PHYSICS_RESEARCH.txt` for gameplay
research references; Z3 is an independent implementation.
