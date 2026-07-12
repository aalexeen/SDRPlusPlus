# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

SDR++ — a cross-platform Software Defined Radio application (C++17). Upstream is
`https://github.com/AlexandreRouma/SDRPlusPlus.git` (branch `master`). This checkout
lives at `SDRPP_project/SDRPlusPlus/`.

## Build (Linux)

```sh
mkdir build && cd build
cmake ..                # add -DOPT_BUILD_<MODULE>=ON/OFF to select modules
make -j$(nproc)
```

- System deps: `cmake fftw3 glfw libvolk zstd` (plus per-module deps — see the module
  table in `readme.md` and the `OPT_BUILD_*` options in the top-level `CMakeLists.txt`).
- A `build/` directory already exists (configured). Incremental rebuild is just `make -j` in it.
- Which modules compile is controlled entirely by `OPT_BUILD_*` cmake options; each is
  `add_subdirectory`'d conditionally from the top-level `CMakeLists.txt`. Many hardware
  source modules default OFF because they need vendor SDKs.

## Run for development

The app needs a **root directory** (config + resources + compiled module list). Never
install to run locally — use `root_dev`:

```sh
sh ./create_root.sh          # once: copies root/ -> root_dev/
cd build && ./sdrpp -r ../root_dev/   # generates root_dev/config.json on first run
```

Then edit `root_dev/config.json` so `"modules"` points at the `.so` files you built.
Generate the list from inside `build/`:

```sh
find . | grep '\.so' | sed 's/^/"/' | sed 's/$/",/' | sed '/sdrpp_core.so/d'
```

Also set `modulesDirectory` / `resourcesDirectory` in that config to `./root_dev/modules`
and `./root_dev/res`. Useful flags (`core/src/command_args.cpp`): `-s` server mode,
`-a`/`-p` server addr/port, `-c` show console (Windows), `--autostart`.

## Formatting

`clang-format` with the repo `.clang-format` is the standard. Check without modifying:
`sh ./check_clang_format.sh` (runs `clang-format -n -Werror`; skips vendored trees like
`core/src/imgui`, `libcorrect`, `json.hpp`, `discord-rpc`, `libsddc`). New code should
match surrounding style — 4-space indent, braces on same line.

## Architecture

Everything is built around a **core shared library + dynamically-loaded plugin modules**.

### Core (`core/`) → `libsdrpp_core.so`
`src/main.cpp` is a trivial `main()` that calls `sdrpp_main()` in `core/src/core.cpp`.
The core owns global singletons (namespace `core`): `configManager`, `moduleManager`,
`modComManager`, `args`. It loads modules listed in config, wires up the signal path,
and runs the GUI (or headless server in `-s` mode).

Key core subsystems (`core/src/`):
- `signal_path/` — the DSP graph. Global singletons in namespace `sigpath` (`signal_path.h`):
  `iqFrontEnd` (decimation, DC blocking, IQ correction, FFT/waterfall feed, VFO fan-out),
  `vfoManager`, `sourceManager`, `sinkManager`. Source module → IQ frontend → splitter →
  per-VFO channels → decoder/sink modules.
- `dsp/` — the DSP block library. Blocks derive from `processor.h`/`hier_block.h`, are
  connected by typed `dsp::stream<T>` (`dsp/stream.h`, 1 MSample ring buffers backed by
  VOLK-aligned buffers). Subdirs group by function (`demod`, `filter`, `channel`,
  `multirate`, `clock_recovery`, `convert`, `correction`, `noise_reduction`, `routing`, …).
- `gui/` — Dear ImGui frontend. `main_window.cpp`, the waterfall/FFT display, menus,
  themes (`theme_manager`), and `smgui` (a wrapper that also renders remotely for server mode).
- `module.{h,cpp}` — the plugin ABI and `ModuleManager`. `config.{h,cpp}` — JSON config
  (`json.hpp` = nlohmann/json). `backend.h` — GLFW vs Android windowing abstraction.

### Modules (`source_modules/`, `sink_modules/`, `decoder_modules/`, `misc_modules/`)
Each module is a standalone shared library that links `sdrpp_core` and installs to
`lib/sdrpp/plugins`. Categories:
- **source** — IQ input (RTL-SDR, HackRF, Airspy, network, file, sdrpp_server, …).
- **sink** — audio output (audio_sink via RtAudio, network_sink, portaudio).
- **decoder** — demod/decode a VFO (`radio` = AM/FM/SSB/etc; meteor, pager, M17, DAB, …).
- **misc** — everything else (recorder, scanner, frequency_manager, rigctl_server/client,
  scheduler, iq_exporter, discord_integration).

`misc_modules/demo_module/src/main.cpp` is the canonical minimal example — copy it to start
a new module.

### The module ABI (every module implements this)
```cpp
SDRPP_MOD_INFO { name, description, author, verMajor, verMinor, verBuild, maxInstances };

class MyMod : public ModuleManager::Instance {
    // ctor(std::string name), dtor, postInit(), enable(), disable(), isEnabled()
};

MOD_EXPORT void _INIT_();                                          // once at load
MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name);
MOD_EXPORT void _DELETE_INSTANCE_(void* instance);
MOD_EXPORT void _END_();                                          // once at unload
```
Modules register GUI via `gui::menu.registerEntry(...)`, tap the signal path through the
`sigpath::*` singletons, and communicate with each other via `core::modComManager`.
See `core/src/module.h` for the full interface.

### Adding a module to the build
1. Create `<category>_modules/<name>/{CMakeLists.txt,src/main.cpp}`. The CMakeLists is
   three lines: `project(...)`, `file(GLOB SRC "src/*.cpp")`, `include(${SDRPP_MODULE_CMAKE})`
   (see `misc_modules/demo_module/CMakeLists.txt`). `sdrpp_module.cmake` does the shared-lib
   + link-core + install boilerplate.
2. Add an `OPT_BUILD_<NAME>` option and a conditional `add_subdirectory` in the top-level
   `CMakeLists.txt`.
3. Rebuild, then add the resulting `.so` to `root_dev/config.json`'s `"modules"` list.

## Vendored code — do not reformat or "fix"
`core/src/imgui/` (Dear ImGui), `core/src/json.hpp` (nlohmann/json), `core/libcorrect/`,
`core/std_replacement/`, `misc_modules/discord_integration/discord-rpc/`,
`source_modules/sddc_source/src/libsddc/`. These are excluded from the clang-format check
for a reason.
