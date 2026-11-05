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

## Optional streaming work

[recomp-kit issue #48](https://github.com/veritr1x/recomp-kit/issues/48)
proposes fetching game data from the hosting server in ranges, keeping
saves in OPFS. That is separate from this import-and-play workflow. Do not
mix another fork's player or asset manifest into this build: its browser
loader, native filesystem mount and range server must agree.

The proposal was welcomed upstream, but no linked implementation is merged
as of 2026-11-05. An implementation in our kit needs three connected parts:

1. An opt-in launcher flag that stages the configured installation and a
   file manifest, honoring bundle exclusions and prepared auxiliary modules.
   The server must support HEAD, valid byte ranges and isolation headers.
2. A player that selects the hosted manifest when present and retains the
   existing import flow when it is absent. The executable path must agree
   with the selected filesystem; the profile must still point to OPFS.
3. A read-only WasmFS fetch mount on the game worker before loading the
   executable, with correct file sizes, directory enumeration and mixed-case
   path resolution. Memory caching needs an explicit bound for long sessions.

There is a verified SDK blocker before enabling that mount. Emscripten
6.0.11's `libwasmfs_fetch.js` loses bytes when a whole-file download is later
read across a backend chunk boundary. A synthetic 12-byte file with 8-byte
chunks reproduced it locally: the second read reported eight bytes but the
last four were zero. The proposed early return for an already downloaded
whole file preserved all twelve bytes in the same test. This was a JavaScript
backend probe, not a compiled streaming game test.

The [upstream fix](https://github.com/emscripten-core/emscripten/pull/28071)
is still open at this review. With the proposal's 4 MiB chunks, our
6,029,312-byte `speed.exe` falls in the affected whole-file size range. Range
support alone therefore does not remove the need for a corrected backend.
Use a verified fixed SDK or a narrowly maintained replacement with its MIT
notice before testing the mount. Leave the installed SDK unmodified.

Acceptance checks should cover the executable's full hash, reads straddling
chunk boundaries, a server that returns a whole file despite Range, missing
and truncated responses, read-only game data, save/reload in OPFS, a real
quick race and fallback to an already imported installation. Persistent
download caching is a separate follow-up to the proposed memory-only cache.
