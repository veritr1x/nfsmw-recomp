# Build and play in a browser

The browser version builds from this repository and its `kit/` submodule.
No other game or runtime fork is needed. The original game files remain
private local inputs. The normal launcher imports them into this browser's
origin-private file system (OPFS); saves live there separately.

## Prepare the tools and game

Use a recursive checkout, Python 3.9 or later, Emscripten 6.0.11, and the
Ghidra/Java prerequisites in [CONTRIBUTING.md](../CONTRIBUTING.md). The
following PowerShell commands run from the repository root. Adjust the SDK,
game and Ghidra paths for your installation.

```powershell
git submodule update --init --recursive
python -m venv .venv
& .venv/Scripts/python.exe -m pip install --require-hashes --only-binary=:all: -r kit/requirements.lock

& .venv/Scripts/python.exe tools/setup.py --install 'D:\Games\Need For Speed Most Wanted Black Edition' --link-only
& .venv/Scripts/python.exe tools/analyze.py --ghidra-home 'C:\Tools\ghidra_12.1.3_PUBLIC' --java-home 'C:\Tools\jdk-21'

& C:\Tools\emsdk\emsdk_env.ps1
$env:PATH = "$PWD\.venv\Scripts;$env:PATH"
& .venv/Scripts/python.exe tools/build.py --target web --regenerate --jobs 4
```

Install and activate version `6.0.11` with the SDK's `emsdk install` and
`emsdk activate` commands first if needed. On macOS/Linux, activate with
`source /path/to/emsdk/emsdk_env.sh` and use `.venv/bin/python` instead.
The same setup, analysis and build wrappers work there.

Analysis and the first compilation can take several minutes. Later builds
can omit `--regenerate` when the translator and translation configuration
have not changed. Rebuild after changing sources; the existence of a Wasm
file alone does not establish that it matches the current checkout.

## Serve and launch

```powershell
& .venv/Scripts/python.exe kit/tools/web_launcher.py --game-dir "$PWD" --out build/web-site --web-build nfsmw=build/web/recomp --serve 2026
```

Keep that terminal running and open `http://localhost:2026/` in a browser
with WebGPU hardware acceleration. Choose **Import folder** and select the
installation containing `speed.exe`, then choose **Play** after it reports
**Ready**. The supplied executable must match the hash in `game.toml`.
The import takes about 2.8 GiB for the tested installation. The browser
also needs storage headroom; the launcher checks before copying. You do
not need to remove or change the installation's ASI loader or scripts.

The server supplies COOP/COEP headers and the Wasm MIME type. Opening the
HTML as a local file or using a plain static server without isolation
headers cannot run the shared-memory engine. Keep the hostname and port
the same to reuse imported data and saves: `localhost` and `127.0.0.1`
are separate browser origins. Use **Manage → Export saves** before
clearing site data.

The server binds to loopback. A container or remote deployment additionally
needs an explicitly configured network listener and HTTPS outside localhost.
The Docker entrypoint in NFS issue #3 uses another fork's `--asset-dir`
option; that is not part of this launcher's import workflow. Its separate
`server.py` is not invoked by that entrypoint.

## Optional streaming

The kit now implements the optional hosted installation described in
[recomp-kit issue #48](https://github.com/veritr1x/recomp-kit/issues/48).
After the same setup and web build above, stage and serve it with:

```powershell
& .venv/Scripts/python.exe kit/tools/web_launcher.py --game-dir "$PWD" --out build/web-stream-site --web-build nfsmw=build/web/recomp --stream nfsmw --serve 2027
```

Open `http://localhost:2027/` and choose **Stream game**. This copies the
configured installation into the private local site, checks the executable
hash and writes `nfsmw/data/manifest.txt`. It honors the bundle exclusions
and prepared auxiliary modules. The source installation is unchanged.
Staging uses about 2.8 GiB of server disk for the tested installation; the
browser downloads requested files or chunks instead of importing that data.
Keep this generated site private, like the source game files.

The native worker mounts the manifest's files read-only at `/data`, before
loading and verifying `speed.exe`. File sizes come from the manifest so
directory scans do not download every asset. Fetches use 4 MiB chunks and a
64 MiB memory cache with eviction/refetch. The linked kit backend corrects
Emscripten 6.0.11's whole-file offset bug across chunk boundaries, including
our 6,029,312-byte executable. Its MIT notice ships with the player; the
installed SDK is unchanged. See the
[upstream fix](https://github.com/emscripten-core/emscripten/pull/28071).

**Import folder**, ZIP import and **Play** remain available. Play selects
the imported installation; Stream game explicitly selects hosting even if
an import exists. A direct player URL prefers a valid imported installation,
then the configured stream. Both use the same OPFS profile at a given
origin, so switching modes preserves saves. `?source=import` and
`?source=stream` also select a mode explicitly. Without streaming metadata,
the existing import-only launcher and player continue to work.

Omitting `--stream` copies no installation files and removes streaming
metadata. It does not delete previously staged data: use a separate output
directory for an import-only distribution. The standard build's
`build/web-site` remains import-only.

The supplied local server handles HEAD, single byte ranges, Content-Length,
Content-Range and COOP/COEP. A different host must preserve these semantics
and use the same origin for the player and assets. Files above 64 MiB require
range support; smaller whole-file responses are supported with exact size
checks. Missing, truncated and malformed responses fail instead of supplying
corrupt bytes. Missing or invalid manifests offer a return to the launcher
to retry or import a copy. Streaming has no persistent offline download
cache; OPFS saves persist independently.

Windows/Chromium checks cover a streamed quick race, driving and pause/resume,
profile reload, normal folder import with zero asset GETs, switching an
imported profile to streaming, and import-only metadata. These are bounded
bring-up checks, not a completed race, campaign or performance benchmark.
See [analysis.md](analysis.md) for measurements and remaining limits.

## Shared kit refresh

Refreshed recomp-kit to `1cbd81ca8e6e1ef2cf33f1c27a06f69a00e64b82`,
including upstream main, optional explicit full-installation streaming sources,
and the WebGPU fixes found while testing other kit games. The full real web
build passes with Emscripten 6.0.11. A fresh Chromium 145 hardware-WebGPU context
streamed the 1,395-file installation, saved a new NetMat alias, entered a quick
race, accelerated, steered and paused/resumed. No JavaScript exception, failed
asset request or WebGPU validation error was observed; the OPFS game directory
remained empty. Seven portable project tests pass.

The earlier folder-import, source-switch and profile-reload evidence above
is retained; this refresh separately reran streaming and alias creation, not
the full import matrix. These checks do not establish a completed race,
prolonged stability, audible output, other browsers or resolution of every
historical intermittent loading fault. Private evidence is under the kit's
ignored `build/web-matrix` directory.

For a separate full installation when analysis uses a minimal copy, use
`--stream "nfsmw=/path/to/full installation"`. Staging still validates the
pinned executable and leaves the configured analysis source unchanged.
