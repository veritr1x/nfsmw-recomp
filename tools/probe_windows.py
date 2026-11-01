#!/usr/bin/env python3
"""Run the native Windows smoke host in a new, isolated profile."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def read_evidence(log):
    """Script delivery cannot hide a rejected core mod or a guest failure."""
    frames = re.search(r"presented frames:\s+(\d+)", log)
    steps = re.search(r"script:\s+(\d+) of (\d+) steps", log)
    return {
        "frames": int(frames[1]) if frames else None,
        "script_completed": bool(steps and steps[1] == steps[2]),
        "mods_loaded": re.findall(r"mods: loaded (\S+) [^\n]+", log),
        "mods_rejected": re.findall(r"mods: rejected [^\n]+", log),
        "unknown": re.findall(r"[^\n]*(?:unknown (?:call|jump|target)|(?:call|jump) to unknown|no translated function|no block entry)[^\n]*", log),
        "missing": re.findall(r"[^\n]*(?:unimplemented import|unsupported required CRT import)[^\n]*", log),
        "guest_faults": re.findall(r"[^\n]*raising an access violation[^\n]*", log),
    }


def passed(record):
    return (record["exit"] == 0 and not record["timed_out"] and record["script_completed"]
            and "core.nfsmw" in record["mods_loaded"]
            and not any(record[key] for key in ("mods_rejected", "unknown", "missing", "guest_faults")))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--script", type=Path, default=ROOT / "smoke/quick-race.script")
    parser.add_argument("--seconds", type=int, default=300)
    parser.add_argument("--toolchain", type=Path, help="Optional LLVM-MinGW directory for its runtime DLLs")
    parser.add_argument("--seed", type=Path, help="Copy one of this probe's own profiles")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("This probe runs the native Windows host")
    if args.seconds <= 0:
        parser.error("--seconds must be positive")
    binary = ROOT / "build/recomp/pop_smoke.exe"
    script = args.script.resolve()
    if not binary.is_file() or not script.is_file():
        parser.error("Build the real smoke host and provide an existing script first")
    sys.path.insert(0, str(ROOT / "kit/tools"))
    import game_config
    cfg = game_config.load(ROOT)
    executable = cfg["developer_exe_path"]
    if not executable.is_file() or hashlib.sha256(executable.read_bytes()).hexdigest() != cfg["game"]["sha256"]:
        parser.error("Prepare the exact pinned game installation with tools/setup.py first")
    runs = ROOT / "build/windows-runs"
    runs.mkdir(parents=True, exist_ok=True)
    seed = args.seed.resolve() if args.seed else None
    if seed and (not seed.is_relative_to(runs.resolve()) or seed.name != "profile" or not seed.is_dir()):
        parser.error("--seed must name a profile owned by this probe under build/windows-runs")
    case = Path(tempfile.mkdtemp(prefix="entry-", dir=runs))
    profile = case / "profile"
    profile.mkdir()
    if seed:
        shutil.copytree(seed, profile, dirs_exist_ok=True)
    (profile / "mod-settings.json").write_bytes((json.dumps({
        "core.nfsmw/width": 1280, "core.nfsmw/height": 720}) + "\n").encode())
    env = {key: value for key, value in os.environ.items() if not key.startswith("RECOMP_")}
    if args.toolchain:
        env["PATH"] = str(args.toolchain.resolve() / "bin") + os.pathsep + env.get("PATH", "")
    env.update(RECOMP_EXE=str(executable), RECOMP_PROFILE_DIR=str(profile),
               RECOMP_REGISTRY=str(profile / "registry.json"), RECOMP_SMOKE_SECONDS=str(args.seconds),
               RECOMP_SMOKE_DRAWABLE="1280x720", RECOMP_LOG="1", RECOMP_IMPORT_STATS="1",
               RECOMP_RUN_RECORD=str(case / "run.json"))
    timed_out = False
    try:
        run = subprocess.run([str(binary), "--script", str(script)], cwd=case, env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=args.seconds + 35)
        output, code = run.stdout, run.returncode
    except subprocess.TimeoutExpired as error:
        output, code, timed_out = error.stdout or b"", None, True
    (case / "host.log").write_bytes(output)
    record = dict(read_evidence(output.decode("utf-8", errors="replace")),
                  case=str(case), script=str(script), binary=str(binary), exit=code, timed_out=timed_out)
    record["passed"] = passed(record)
    (case / "result.json").write_bytes((json.dumps(record, indent=2) + "\n").encode())
    print(json.dumps(record), flush=True)
    return int(not record["passed"])


if __name__ == "__main__":
    raise SystemExit(main())
