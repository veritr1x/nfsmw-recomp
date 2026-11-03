# Contributing

This repository holds Need for Speed: Most Wanted's configuration, tests
and docs on top of [recomp-kit](https://github.com/veritr1x/recomp-kit),
the submodule at `kit/`. Runtime, translator, host and tooling changes go to
the kit; open an issue there before a large architecture change. Here, the
useful work is the bring-up itself: identifying the hooks and globals that
`game.toml` still marks as sentinels, curating symbols in `globals.toml`, and
keeping [docs/analysis.md](docs/analysis.md) true.

## Prerequisites

- Python 3.9 or later; create `.venv` and install `kit/requirements.lock` with `pip --require-hashes --only-binary=:all:`.
- Native builds on macOS: Apple Silicon, Xcode Command Line Tools and Git.
  CMake and Ninja come from the requirements file. iPad builds need Xcode
  with the iOS SDK and a developer team.
- Listings: [Ghidra 12.1.3](https://github.com/NationalSecurityAgency/ghidra/releases/tag/Ghidra_12.1.3_build)
  and a Java runtime compatible with it (tested with OpenJDK 26.0.1; set
  `JAVA_HOME` to the JDK directory).
- Your own copy of the PC Black Edition of Need for Speed: Most Wanted.

The executable must be `speed.exe` with SHA-256:

```text
80774c2e5d619b4f120b48d4462896fd504c263399d203a238769cffde1d253c
```

The loader refuses other binaries because translated addresses and data
layouts are tied to this image. Do not bypass the hash to add support for
another version; a second version is a second `game.toml`.

## Prepare your game installation

Use a directory containing `speed.exe` and its `CARS`, `FRONTEND`, `GLOBAL`,
`SOUND` and `TRACKS` directories. Paths containing spaces are supported when
quoted.

```sh
.venv/bin/python tools/setup.py --install "/path/to/Need For Speed Most Wanted Black Edition" --link-only
.venv/bin/python tools/analyze.py \
  --ghidra-home "/path/to/ghidra_12.1.3_PUBLIC" \
  --java-home "/path/to/your/jdk/Contents/Home"
```

Setup verifies the executable and links the installation at ignored
`original/retail/` (a copy placed there directly is accepted too). It does
not download the game. `tools/analyze.py` then imports the executable into a
disposable Ghidra project, runs Ghidra's default analyzers and exports
translation inputs into ignored `analysis/decompiled/speed.exe` with the
kit's export script; the log is `build/analyze.log`. The kit's own
`tools/setup.py` without `--link-only` is not used here: it exports with
analysis off and expects a curated annotation set, which this executable
does not have.

## Build and run

```sh
.venv/bin/python tools/build.py --regenerate --jobs 8   # translate, then compile
.venv/bin/python tools/build.py --jobs 8                # afterwards
```

For current translation and bounded runtime results, see
[docs/analysis.md](docs/analysis.md). `--target ios`
builds, signs and installs the iPad app (`RECOMP_IOS_TEAM` or `--team`) once
a macOS build runs. The CMake tree lives in `build/cmake/<preset>`.

## Check your change

```sh
.venv/bin/python tools/test.py             # the kit's portable suites; no game files required
.venv/bin/python -m pytest -q tests        # this repository's config tests
.venv/bin/python tools/build.py --stub     # link-only configure of this config through the kit
.venv/bin/python kit/tools/format.py       # handwritten native code style (kit sources)
```

See [docs/testing.md](docs/testing.md).

## Updating the kit

`git -C kit checkout <commit>` then commit the submodule pointer here, with a
changelog line naming what changed. Keep the pin on a kit tag when one exists.

Keep `.recomp.json` in sync with the README when project status changes.
Its format is defined by [recomp.board](https://recomp.fyi/spec) (CC BY 4.0).
Use only documented status and release evidence; omit unverified fields.
