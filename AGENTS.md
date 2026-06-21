# AGENTS.md

## Project Overview

Rustyfoot JSFX Bridge — provides `jsfx-wrapper.so` (LV2 wrapper) and
`stom-pi-jsfx-scan` (JSFX introspection tool) for the Rustyfoot pedalboard
platform.

## Build

```bash
git submodule update --init --recursive
make              # Build everything
make ysfx         # Build ysfx static library only
make clean        # Clean all
```

## Architecture

The wrapper uses `lv2_lib_descriptor(bundle_path, features)` — the LV2 core
API that receives the bundle path at load time. This lets a single `.so` serve
different plugin URIs by reading a per-bundle `wrapper.json`.

Unlike the VST3 bridge which uses Carla as backend, the JSFX bridge links
ysfx directly — no Carla dependency. ysfx compiles JSFX source at load time
and provides the audio processing runtime.

## Key Files

- `src/wrapper.cpp` — LV2 wrapper plugin (uses ysfx API)
- `src/scanner.cpp` — JSFX scanner CLI (uses ysfx for metadata extraction)
- `ildaeil/` — Git submodule providing ysfx (via Carla's modules)

## Related Projects

- `../stom-pi` — Main Rustyfoot server
- `../stom-pi-vst3` — VST3 bridge (same architecture, uses Carla backend)
- `../stom-pi-builder` — Plugin build system (generates LV2 TTL + modgui)
- `../stom-pi-modhost` — mod-host + JACK packaging
