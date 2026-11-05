# NFS Most Wanted Recomp

[Build & contribute](CONTRIBUTING.md) · [Port analysis](docs/analysis.md) ·
[Testing](docs/testing.md) · [Changelog](CHANGELOG.md)

A recompilation of **Need for Speed: Most Wanted** (the 2005 PC Black
Edition) for macOS, iPad, Linux, Windows and the browser, in progress.
Original game instructions are translated to C ahead of time and compiled
with the native host, the way
[populous-recomp](https://github.com/veritr1x/populous-recomp) does it.

The runtime, translator, hosts and mod foundation are
[recomp-kit](https://github.com/veritr1x/recomp-kit), pulled in as the git
submodule `kit/`. This repository holds what is Most Wanted's: `game.toml`
and `globals.toml` (identity, addresses, curated symbols), `tests/` (the
config's contract with the kit), `tools/analyze.py` (this game's listing
export) and docs.

**You need your own copy of the game.** Game executables, artwork, sound,
tracks, cinematics, generated game code and replacement packs are prepared
locally and are not included. See [NOTICE](NOTICE) for ownership and
dependency credits.

## Which executable

The 2005 PC Black Edition ships one game executable, **`speed.exe`** (linked
by Microsoft 7.10, Visual C++ .NET 2003, timestamp 2005-12-01). It renders
through **Direct3D 9** with its own effect files, plays sound through
**DirectSound** from `.abk` banks whose heap routines are x86 code the kit
runs in a small interpreter, and reads its tracks and cars from the game
folder. The exact image is pinned by SHA-256 in `game.toml`; no other build
is accepted.

## Status: plays through the menus; a scripted race completes intermittently

Recorded macOS runs booted, rendered the menus and drove a race, but only one
or two of five scripted runs completed; the others stopped during race loading.
Fresh Windows checks on 2026-11-01 entered and drove two races and exited cleanly,
including mapped-pad/mouse controls. Those bounded results do not establish
that the older timing-dependent loading failure is eliminated. The measurements
and platform-specific evidence are in [docs/analysis.md](docs/analysis.md).

## Platform status

The rows retain their recorded platform-specific scope. Windows was checked
on 2026-11-01 with the diagnostic-host mod correction, and browser playback
on Windows was checked on 2026-11-05. Other platform observations below are
historical. See the [run log](docs/analysis.md).

| Platform | Verified status | Build command | Known issues and remaining checks |
| --- | --- | --- | --- |
| macOS 14+ | Boots, renders the front end and career menus on Metal, reaches track select and a race. One to two runs in five play `smoke/quick-race.script` to the end with `guest exit code 0`. | `.venv/bin/python tools/build.py` (app); `--target smoke` | The intermittent hang entering race loading is open and is the blocker. Performance is unmeasured on this pin: the 4K figures below predate it and cannot be re-measured until a run reaches gameplay reliably. |
| iPadOS 17+ | Played by touch on an earlier kit pin, with the core mods compiled into the app, which a stock device needs because it loads no plugins. | `.venv/bin/python tools/build.py --target ios --console` | Not re-checked since the kit gained restored kernel32 imports, new recovery arbitration and 836 static initializers. |
| Linux | Rendered the race on an earlier pin, including under software Vulkan (lavapipe). | `.venv/bin/python tools/build.py --regenerate` | Not re-checked on the current pin. |
| Windows | Native Windows x64 LLVM-MinGW/Vulkan: fresh menu and two quick-race probes pass at 1280x720 with core.nfsmw loaded. Captures show acceleration, steering, mouse targeting, brake input and pause/resume; both runs exit zero. | `tools/build.py --target smoke`; `tools/probe_windows.py --toolchain /path/to/llvm-mingw` | Bounded checks, not a full campaign or performance measurement. Two passes do not establish that the historical intermittent loading failure is eliminated. |
| The browser | Windows/Chromium WebGPU checked on 2026-11-05: imports the installation, creates and reloads a profile, enters a quick race and accepts driving/pause controls. | `tools/build.py --target web`; [browser setup](docs/web.md) | Bounded gameplay checks, not a full race/campaign or performance benchmark. Render-target readback remains unsupported. Safari was not re-checked. |

### Historical issues and remaining checks

- **The race-loading hang.** A scripted race completes one or two runs in
  five. The main thread spins in a translated function rather than deadlocking,
  so the watchdog reports it as the guest no longer calling into the runtime.
  Every instrument tried so far moves it; `sample` on the hung process is the
  one measurement that does not.
- **Shutdown.** Older smoke runs reported a guest worker outliving the run
  (exit code 4). The fresh Windows menu and race checks exit zero; other
  platform runtime checks have not been repeated for this checkpoint.
- **Earlier per-platform figures.** The 4K and browser numbers quoted above
  were measured before the current kit pin and are kept as history, not as
  claims about it.

Direct3D 9 is translated through one shader generator and renders on Metal
(macOS, iPad), Vulkan (Linux, Windows, and macOS through MoltenVK) and WebGPU
(the browser). The simulation runs at 120 Hz and the widescreen fix (FOV, HUD,
minimap) is ported, both in the `core.nfsmw` mod. Online play (`ws2_32`,
`tapi32`, `netapi32`, the bundled `server.dll`) is out of scope and stubbed to
fail cleanly, and the Windows-only extras in the game folder (the ASI loader
and the widescreen fix) are not part of the port; their fixes are native host
behaviour instead.

## Build on macOS

The steps are the kit's. The last of them regenerates the translation and
builds the app.

```sh
git clone --recurse-submodules https://github.com/veritr1x/nfsmw-recomp.git
cd nfsmw-recomp
python3 -m venv .venv
.venv/bin/python -m pip install --require-hashes --only-binary=:all: -r kit/requirements.lock
.venv/bin/python tools/setup.py --install "/path/to/Need For Speed Most Wanted Black Edition" --link-only
.venv/bin/python tools/analyze.py --ghidra-home /path/to/ghidra_12.1.3_PUBLIC
.venv/bin/python tools/build.py --regenerate
```

`tools/setup.py`, `tools/build.py`, `tools/test.py` and `tools/ios_logs.py`
are four-line wrappers around the kit's tools; every option is the kit's
(`--help` lists them). `tools/analyze.py` is this game's own: the kit's setup
exports listings from a curated annotation set, and none exists for
`speed.exe`, so this script runs Ghidra's analyzers instead. Outputs (the
translation, the apps, the logs) live under ignored `build/`; your
installation is linked at ignored `original/retail` and the Ghidra listings
live in ignored `analysis/`.

## Build for the other platforms

Linux and Windows builds are the kit's `--target app` (Windows cross-compiles
with llvm-mingw), and `tools/build.py --target web` writes a servable site for
the browser. The kit's [README](kit/README.md) has the prerequisites and the
serving headers the web build needs.

For a complete Windows/macOS/Linux browser workflow, including local serving,
folder import and the optional streaming proposal, see [Build and play in a
browser](docs/web.md).

## Play on an iPad

`tools/build.py --target ios --console` builds, signs and installs the app and
streams its console, staging the game directory into the app minus
`[bundle].exclude` in `game.toml` (the uninstaller, the Windows-only DLLs).
The core mods are compiled in, which a stock device needs because it loads no
plugins.

## Check a change

```sh
.venv/bin/python tools/test.py              # the kit's portable suites
.venv/bin/python -m pytest -q tests         # this game's config
.venv/bin/python tools/build.py --stub      # the kit configures against this config, no game code
```

Changes to the runtime, hosts or tools belong in the kit's repository; bump
the submodule here once they land.
