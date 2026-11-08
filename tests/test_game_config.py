"""Need for Speed: Most Wanted's game.toml renders the values the kit's hooks expect."""
import importlib.util
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
KIT = ROOT / "kit"

# The zero-filled section padding after .rsrc in the pinned speed.exe: mapped,
# never referenced by the game. Every unidentified hook and global lives here.
SENTINEL_LOW, SENTINEL_HIGH = 0x00A37700, 0x00A38000
# Hooks whose addresses were verified in the listings (game.toml says where).
VERIFIED_HOOKS = {"free_run"}


def load_module(name):
    spec = importlib.util.spec_from_file_location(name, KIT / "tools" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


game_config = load_module("game_config")
gen_game_config = load_module("gen_game_config")


class NfsmwConfigTests(unittest.TestCase):
    def setUp(self):
        self.cfg = game_config.load(ROOT)
        self.header = gen_game_config.render_header(self.cfg)

    def test_identity(self):
        self.assertEqual(self.cfg["game"]["id"], "nfsmw")
        self.assertEqual(self.cfg["game"]["executable"], "speed.exe")
        self.assertEqual(self.cfg["game"]["sha256"],
                         "80774c2e5d619b4f120b48d4462896fd504c263399d203a238769cffde1d253c")
        self.assertEqual(self.cfg["game"]["entry_point"], 0x007C4040)
        self.assertEqual(self.cfg["game"]["image_base"], 0x00400000)
        self.assertIn('#define RECOMP_APP_NAME "SpeedRecomp"', self.header)
        self.assertIn('#define RECOMP_GUEST_ROOT "C:\\\\Program Files\\\\EA GAMES\\\\Need for Speed Most Wanted"',
                      self.header)
        self.assertEqual(self.cfg["developer_exe_path"], (ROOT / "original/retail/speed.exe").resolve())
        self.assertEqual(self.cfg["listings_path"], (ROOT / "analysis/decompiled/speed.exe").resolve())

    def test_every_kit_macro_is_rendered(self):
        for macro in ("RECOMP_HOOK_FRAME_CLOCK_BEGIN", "RECOMP_HOOK_FRAME_CLOCK_WAIT",
                      "RECOMP_HOOK_FRAME_CLOCK_WAIT_CLAMP", "RECOMP_HOOK_FRAME_CLOCK_CLAMP_DEADLINE",
                      "RECOMP_HOOK_FRAME_CLOCK_WAIT_DEADLINE", "RECOMP_HOOK_CURSOR_SURFACE_PTRS_COUNT 2",
                      "RECOMP_HOOK_MOUSE_VTABLE", "RECOMP_HOOK_MOUSE_DEVICE_PTR", "RECOMP_HOOK_MOUSE_DEVICE_RIGHT",
                      "RECOMP_HOOK_CAMERA", "RECOMP_GLOBAL_SIMULATION_TURN_ADDR", "RECOMP_GLOBAL_COMMAND_FRAME_ADDR",
                      "RECOMP_GLOBAL_ENTITY_BASE_ADDR", "RECOMP_GLOBAL_ENTITY_BASE_STRIDE",
                      "RECOMP_GLOBAL_ENTITY_BASE_COUNT"):
            self.assertIn("#define " + macro, self.header)

    def test_unidentified_addresses_stay_in_the_sentinel_padding(self):
        """Until a hook is found, it must point where the game never looks."""
        addresses = [self.cfg["translate"]["animation_counter"]]
        for key, value in self.cfg["hooks"].items():
            if key in VERIFIED_HOOKS:
                continue
            addresses += value if isinstance(value, list) else [value]
        addresses += [entry["addr"] for entry in self.cfg["globals"].values()]
        for address in addresses:
            self.assertTrue(SENTINEL_LOW <= address < SENTINEL_HIGH, hex(address))
        self.assertEqual(self.cfg["translate"]["volatile_reads"], [])

    def test_redirected_camera_slots_are_seeded_with_the_replaced_constants(self):
        """With core.nfsmw's widescreen off nothing else writes these slots."""
        def bits(entry):
            if "float" in entry:
                return struct.unpack("<I", struct.pack("<f", entry["float"]))[0]
            return entry["value"] & 0xFFFFFFFF
        seeds = {entry["addr"]: bits(entry) for entry in self.cfg["translate"]["data_seeds"]}
        constants = {0x0089096C: 0x3F800000, 0x008933D4: 0x3F000000, 0x008910C4: 0x3F8CCCCD}
        redirected = [r for r in self.cfg["translate"]["operand_redirects"] if r["from"] in constants]
        self.assertEqual(len(redirected), 6)
        for r in redirected:
            self.assertEqual(seeds.get(r["to"]), constants[r["from"]], hex(r["to"]))

    def test_mod_and_native_slots_stay_in_the_padding_apart_from_the_seeds(self):
        """native/pacing.c and core.nfsmw share these; nothing seeds them."""
        seeded = {entry["addr"] for entry in self.cfg["translate"]["data_seeds"]}
        slots = [0x00A37B60, 0x00A37B70, 0x00A37B74] + [0x00A37C00 + 4 * i for i in range(32 * 3)]
        for slot in slots:
            self.assertTrue(SENTINEL_LOW <= slot < self.cfg["translate"]["animation_counter"], hex(slot))
            self.assertNotIn(slot, seeded, hex(slot))
        for name in ("mods/core/nfsmw/nfsmw.c", "native/pacing.c"):
            text = (ROOT / name).read_text()
            for macro, value in (("SLOT_PACING", "0x00a37b60u"), ("STREAM_RING_HEAD", "0x00a37b70u"),
                                 ("STREAM_RING_TAIL", "0x00a37b74u"), ("STREAM_RING", "0x00a37c00u"),
                                 ("STREAM_RING_SIZE", "32u")):
                self.assertRegex(text, r"#define %s %s\b" % (macro, value), name)

    def test_mapped_pad_reaches_every_driving_action(self):
        """The default PC keys (the key table at 0x008f6d80) for each action."""
        mapped = self.cfg["controls"]["mapped"]
        targets = {value for key, value in mapped.items() if isinstance(value, str)}
        for key in ("Up", "Down", "Space", "LShift", "X", "G", "L", "R", "Escape", "Return"):
            self.assertIn("key:" + key, targets, key)
        self.assertEqual(mapped["left_stick"], "horizontal_arrows")
        # smoke/pad-race.script drives with cross and brakes with square.
        self.assertEqual((mapped["cross"], mapped["square"]), ("key:Up", "key:Down"))

    def test_bundle_exclusions_and_setup(self):
        # What the iPad bundle leaves behind: the uninstaller, the installer
        # helpers, and the Windows-only DLLs a device cannot load anyway.
        for pattern in ("Uninstall", "scripts", "*.dll"):
            self.assertIn(pattern, self.cfg["bundle"]["exclude"])
        # And what it keeps. The cinematics are 856 MB and the temptation is
        # to drop them, but the game decodes its own VP6 and plays them on the
        # device, so excluding them would take the intro and the outro with
        # them. This asserted the opposite until the cinematics worked.
        self.assertNotIn("MOVIES", self.cfg["bundle"]["exclude"])
        self.assertEqual(self.cfg["setup"]["required_dirs"], ["CARS", "FRONTEND", "GLOBAL", "SOUND", "TRACKS"])
        self.assertNotIn("annotations_url", self.cfg["setup"])


if __name__ == "__main__":
    unittest.main()
