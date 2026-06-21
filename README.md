# Stom-pi JSFX Bridge

JSFX-to-LV2 bridge for the Stom-pi pedalboard platform. Allows JSFX effects
(REAPER's scripted audio plugins) to be loaded by mod-host as native LV2 plugins.

Uses [ysfx](https://github.com/jpcima/ysfx) for JSFX compilation and runtime.

## Components

- **jsfx-wrapper.so** — Shared LV2 plugin that hosts JSFX via ysfx. Symlinked
  into each generated LV2 bundle; reads `wrapper.json` to determine which JSFX
  to load.

- **stom-pi-jsfx-scan** — CLI tool that introspects a `.jsfx` file and outputs
  `wrapper.json` with slider/parameter metadata for LV2 TTL generation.

## Build

```bash
git submodule update --init --recursive --depth 1
make
```

## Install

```bash
sudo make install          # Installs to /usr/local
sudo make install PREFIX=/usr  # Or specify prefix
```

## License

GPLv2+
