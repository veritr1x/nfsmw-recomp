# Changelog

## Unreleased

- Re-pin the kit to `main` 30fb57d. The merge that landed this game's kit
  work on main had dropped 27 kernel32 import declarations; an import with no
  table entry has an unknown argument count, so every call to one leaked its
  arguments and a thread polling a timer walked the guest stack ten megabytes
  below itself into `.bss`, where it overwrote a callback table with its own
  return addresses and died four subsystems away in SEH. Restored. Recovery
  now arbitrates on evidence rather than on which guess resolved first
  (36,566 to 36,717 functions, against 36,718 for the translator this port
  was built on), `__initterm` is matched by its shape rather than by one
  register allocation, and `RECOMP_NULL_FAULTS` can make the never-mapped
  first 64 KB fault the way Windows does (a build option, off by default,
  because the guest reaches those reads with pointers Windows would have
  filled in). `SystemTimeToFileTime` was declared and empty; implemented.
- 836 CRT static initializers had never run. This executable's `__initterm`
  keeps its cursor on the stack and calls through `EDX`, and the kit knew
  only the `ESI`/`CALL EAX` spelling, so the CRT walked its table, called
  each constructor through the address table, and the address table had never
  heard of them: every one of those globals reached the game with a null
  vtable and null members. One owns a bitset whose base stayed null, so the
  guest set bits at guest `0x138` and read them back from the same place.
- `game.toml` names eight entry points a run proved, where recovery does not
  reach them: `0x006db6c0` is a thread start routine, so the thread it
  belongs to returned immediately and did nothing; `0x007cd5e4` is an SEH
  handler, and without it a fault the game handles itself reached the
  dispatcher as `ExceptionContinueExecution` and stopped the run. The last
  three were each reachable only once the one before it could be delivered. A
  quick-race run now reports no undeliverable calls at all, where every run
  before it named at least one.
- The game renders its front end and career menus, reaches track select and
  drives a race, and one or two runs in five play `smoke/quick-race.script`
  to the end with `guest exit code 0`. The rest hang entering race loading:
  the main thread spins in a translated function rather than deadlocking, so
  the watchdog reports it as the guest no longer calling into the runtime.
  Measured at 0 to 2 in 5 across three kit configurations, none
  distinguishable from another at that sample size, so nothing in this
  re-pin is a regression and five runs cannot settle the question either way.
  Recorded in `docs/analysis.md` with the instruments that move it and the
  one that does not.

- The translation compiles and the game boots as far as its first Direct3D 9
  call. `game.toml` names two CRT helper entry points the Ghidra listing
  lacks; everything else was kit work (see the kit's changelog): MMX/SSE2
  traps, `XADD`, `CMPXCHG`, `LAHF`, the x87 constants and environment ops,
  `INT3` as a block terminator, `GetModuleHandleA` for served modules, 19
  kernel32 shims and stdcall pop counts for the unshimmed imports. Recorded
  in `docs/analysis.md`. The window path works too: `RegisterClassExA`,
  `AdjustWindowRect` and `GlobalMemoryStatusEx` landed in the kit, and the
  guest stack no longer drifts. With the kit's new Direct3D 9 and D3DX 9
  modules the game now boots, creates its device, streams its audio and runs
  its own render loop without crashing. It draws nothing yet: device
  resources and shader translation are the remaining work. With the kit's
  vtable pop counts corrected the game now runs without crashing at all, and
  it draws: seven DrawPrimitiveUP calls inside seven effect passes, with
  every guest call resolved. Nothing is rasterized yet.
- Re-pin the kit to `main` 4574a35, the commit the other game repositories
  pin; `game.toml` gains the `entry_points` key and CI takes the current
  three-platform shape. On this kit the translator clears discovery and
  emits code for all but 40 of the 25,768 functions; the 40 need MMX, SSE2,
  `STMXCSR`, two x87 constants, `FNSTENV` and `LAHF` in the kit's
  translator. Recorded in `docs/analysis.md`.
- New game repository for Need for Speed: Most Wanted (PC Black Edition,
  `speed.exe` SHA-256 `80774c2e…d253c`) in the shape of populous-recomp: the
  kit as the submodule `kit/`, `game.toml` and `globals.toml`, thin
  `tools/*.py` wrappers, config tests and CI.
- `game.toml` carries the measured identity of the executable (image base
  `0x00400000`, entry point `0x007c4040`, guest root, required data
  directories, iOS bundle exclusions). The Populous-shaped hooks and globals
  the kit compiles against are sentinels in the executable's unused section
  padding until the bring-up identifies them; `tests/test_game_config.py`
  enforces that.
- `tools/analyze.py`: listing export with Ghidra's own analyzers, because the
  kit's setup expects a curated annotation set this game does not have.
- First pipeline run recorded in `docs/analysis.md`: Ghidra exports 25,768
  functions; the kit's translator parses them all and stops at its discovery
  gates (638 dispatch targets outside any listing, mostly fall-throughs after
  calls Ghidra marks non-returning). No translation compiles yet.
- `docs/analysis.md`: the executable's import surface, graphics path and the
  kit work each needs. The blocking item is shader-model Direct3D 9 through
  D3DX effects, outside the kit's supported envelope today.
