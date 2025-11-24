# Fates-3GX-SDK (Fire Emblem Fates plugin framework)

## Project status

This is an **early alpha** aimed at experienced modders.
APIs, event contexts, and hook sets may change between versions.
If you build anything on top of this, expect breaking changes

This project is a **CTRPF/3GX plugin** for Fire Emblem Fates (3DS) that turns low-level
game hooks into a small, C++-friendly **engine**:

- A **hook layer** that intercepts key game functions in `code.bin`
- A lightweight **event bus** (`Fates::Engine`) that exposes high-level events like:
  - Map start / end
  - Turn begin / end
  - HP changes & kills
  - RNG calls
  - Level ups & skill learns
  - Item gains
- A set of **example modules** that show how to register handlers and react to events.

The goal is to make it possible to write Fates gameplay mods in **C++** rather than ARM
assembly, while keeping everything small, fast, and understandable.

---

## Supported game / versions

Right now the SDK targets:

- **Fire Emblem Fates – Special Edition (NA), v1.1**


## Building the plugin

1. Install **devkitPro / devkitARM**, **libctru**, and **CTRPluginFramework**.
2. Clone this repository.
3. Copy the example build config:
4. **Clean decrypted** code.bin goes into /original

   ```bash
   cp build_config.example.toml build_config.toml


For setup instructions, see [docs/getting_started.md](docs/getting_started.md).

## Third-party tools

This project uses and bundles the following third-party tools:

- **3gxtool (3DS Game eXtension Tool v1.3)**  
  Used to convert the compiled CTRPluginFramework plugin into a `.3gx` file.
  Original project by Nanquitas / The Pixellizer Group (ThePixellizerOSS).  
  - Upstream: https://gitlab.com/thepixellizeross/3gxtool  
  - GitHub mirror: https://github.com/Nanquitas/3gxtool  
  - Bundled binary: `build/3gxtool/3gxtool.exe`  
  - License: see `third_party/3gxtool-LICENSE.txt`



