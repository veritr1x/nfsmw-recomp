"""A successful script must not conceal a missing game mod or guest fault."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("probe_windows", ROOT / "tools/probe_windows.py")
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


def test_completed_script_rejects_missing_core_plugin():
    evidence = probe.read_evidence("mods: rejected core.nfsmw: dlopen failed: Windows error 126\n"
                                   "script: 10 of 10 steps\npresented frames: 4404\n")
    assert evidence["script_completed"]
    assert not probe.passed(dict(evidence, exit=0, timed_out=False))


def test_loaded_mod_and_completed_script_pass():
    evidence = probe.read_evidence("mods: loaded core.nfsmw 1.0.0\n"
                                   "script: 10 of 10 steps\npresented frames: 4000\n")
    assert probe.passed(dict(evidence, exit=0, timed_out=False))


def test_guest_fault_or_incomplete_script_cannot_pass():
    log = "mods: loaded core.nfsmw 1.0.0\nscript: 10 of 10 steps\npresented frames: 4000\n"
    evidence = probe.read_evidence(log + "call to 00000000: raising an access violation\n")
    assert not probe.passed(dict(evidence, exit=0, timed_out=False))
    evidence = probe.read_evidence(log.replace("10 of 10", "9 of 10"))
    assert not probe.passed(dict(evidence, exit=0, timed_out=False))
