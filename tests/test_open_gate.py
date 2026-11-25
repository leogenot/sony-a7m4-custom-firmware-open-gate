"""Open gate: the site image against the kit's designs and v0.1.3's files, and the camera module on the host.

Needs ArcaneLantern/a7iv-open-gate-test's designs and an unpacked v0.1.3 tree (fwtool); they are not in the repo.
Set OG_DESIGNS and OG_V013_DEV, or keep the workspace layout (../a7iv-open-gate-test, ../unpacked from
scripts/unpack.sh).
"""
import argparse
import hashlib
import json
import os
import runpy
import shutil
import struct
import subprocess
from pathlib import Path

import pytest

from a7m4.common import ToolError
from a7m4.open_gate import (Module, _config_header, _edit_bank, _edit_banks, _linker_script, _uxc_records, _uxc_texts,
                            build, compile_module, compose_ap)
from a7m4.open_gate_sites import (_CP_IMAGE_VA, _NATIVE, PRESETS, _ROOT, STEP1, _STORED_RECORD, Family, Mode, Run, SiteImage,
                                  Space, check_stock, compile_image, dmmcfg_edits, _layout, load_design, runs_of)

WORKSPACE = _ROOT.parent
DESIGNS = Path(os.environ.get("OG_DESIGNS", WORKSPACE / "a7iv-open-gate-test/windows/tools/modding/designs"))
DEV = Path(os.environ.get("OG_V013_DEV",
                         WORKSPACE / "unpacked/BODYDATA-custom-v0.1.3/firmware.tar_unpacked/0700_part_image/dev"))
KIT_APPLY = DESIGNS.parents[1] / "camera_tests/CT6_memory_read_check/open_gate_apply.py"
FILES = {"cpapp-b.bin": ("nflasha8_unpacked/cpapp-b.bin", "nflasha8:CPAPP-B.BIN"),
         "av-cam.bin": ("nflasha3_unpacked/av-cam.bin", "nflasha3:av-cam.bin"),
         "libmpr.so": ("nflasha15_unpacked/lib/libmpr.so", "nflasha15:lib/libmpr.so"),
         "DmmCfg.bin": ("nflasha8_unpacked/DmmCfg.bin", "nflasha8:DMMCFG.BIN")}
RECORD = json.loads(_STORED_RECORD.read_text("utf-8"))
OK, BUSY, NOT_READY, WRONG_DIAL, FOREIGN, IO, UNDO_FAILED, NOT_SAVED = range(8)

needs_designs = pytest.mark.skipif(not DESIGNS.is_dir(), reason="kit designs not available")
needs_files = pytest.mark.skipif(not all((DEV / p).is_file() for p, _ in FILES.values()), reason="no v0.1.3 tree")


@pytest.fixture(scope="module")
def image():
    if not DESIGNS.is_dir():
        pytest.skip("kit designs not available")
    return compile_image(DESIGNS, _STORED_RECORD)


@pytest.fixture(scope="module")
def files():
    if not all((DEV / p).is_file() for p, _ in FILES.values()):
        pytest.skip("no unpacked v0.1.3 tree")
    data = {k: (DEV / p).read_bytes() for k, (p, _) in FILES.items()}
    for key, (_, pin) in FILES.items():
        assert hashlib.sha256(data[key]).hexdigest() == _layout()["inputs"][pin], key
    return data


def expected_bytes(mode, family, changed_only=True):
    """{(space, addr): (old, new)} for every changed byte (or every byte) of a preset's design entries and stored
    record patch, read straight from the JSONs."""
    result = {}

    def add(space, addr, old, new):
        for i, (o, n) in enumerate(zip(bytes.fromhex(old), bytes.fromhex(new))):
            if o != n or not changed_only:
                assert result.setdefault((space, addr + i), (o, n)) == (o, n)

    for name in PRESETS[(mode, family)]:
        design = json.loads((DESIGNS / f"{name}.json").read_text())
        for patch in design.get("patches", []):
            space = {"av-cam.bin": Space.AVCAM, "libmpr.so": Space.MPR}.get(patch.get("binary"), Space.CP)
            add(space, int(patch["file_off"] if "binary" in patch else patch["addr"], 16), patch["old"], patch["new"])
        for site in design.get("sites", []):
            add(Space.CP, int(site["ram_address"], 16), site["old"], site["new"])
        if design["summary"].get("plus", "").startswith("26 bytes of the stored drive-parameter record"):
            fam = RECORD["families"][family.value]
            for i, off in enumerate(fam["offsets_in_record"]):
                add(Space.CP_DRIVE_RECORD, off, fam["old"][2 * i:2 * i + 2], fam["new"][2 * i:2 * i + 2])
    return result


def image_bytes(preset):
    result = {}
    for run in preset.runs:
        for i in range(len(run.old)):
            assert (run.space, run.addr + i) not in result
            result[(run.space, run.addr + i)] = (run.old[i], run.new[i])
    return result


@needs_designs
def test_presets_are_the_kit_states_without_step1_and_the_c3_cycle():
    kit = runpy.run_path(str(KIT_APPLY))
    for state, mode in (("default", Mode.UHD_32), ("real57", Mode.K57_32)):
        steps = [s[1] for s in kit["STATES"][state]["steps"] if s[1] != STEP1 and not s[1].startswith("lcdcycle")]
        assert PRESETS[(mode, Family.NTSC)] == tuple(steps)
        pal = [kit["PAL"][s][1] if s in kit["PAL"] else s for s in steps]
        assert PRESETS[(mode, Family.PAL)] == tuple(pal)


def test_site_image_round_trip(image):
    data = image.encode()
    assert SiteImage.decode(data) == image and image.encode() == data
    assert [(p.mode, p.family) for p in image.presets] == list(PRESETS)
    broken = bytearray(data)
    broken[-1] ^= 1
    with pytest.raises(ToolError):
        SiteImage.decode(bytes(broken))
    small = SiteImage(image.presets[:1])
    assert SiteImage.decode(small.encode()) == small


def test_every_run_equals_its_design_entries_and_the_stored_record(image):
    for preset in image.presets:
        want, got = expected_bytes(preset.mode, preset.family), image_bytes(preset)
        covered = expected_bytes(preset.mode, preset.family, changed_only=False)
        assert {k: v for k, v in got.items() if v[0] != v[1]} == want, (preset.mode, preset.family)
        for key, value in got.items():   # unchanged bytes kept inside a run are unchanged bytes of a design entry
            assert covered[key] == value


def test_runs_of_one_space_coincide_or_are_disjoint(image):
    seen = {}
    for preset in image.presets:
        for run in preset.runs:
            key = (run.space, preset.family if run.space is Space.CP_DRIVE_RECORD else None)
            seen.setdefault(key, {}).setdefault(run.addr, len(run.old))
            assert seen[key][run.addr] == len(run.old)
    for runs in seen.values():
        ordered = sorted(runs.items())
        assert all(a + n <= b for (a, n), (b, _) in zip(ordered, ordered[1:]))


def test_check_stock_against_v013(image, files):
    check_stock(image, files)
    run = next(r for r in image.presets[0].runs if r.space is Space.CP and r.addr - _CP_IMAGE_VA < len(files["cpapp-b.bin"]))
    tampered = bytearray(files["cpapp-b.bin"])
    tampered[run.addr - _CP_IMAGE_VA] ^= 0xff
    with pytest.raises(ToolError, match="not stock"):
        check_stock(image, files | {"cpapp-b.bin": bytes(tampered)})


@needs_designs
def test_dmmcfg_edits_are_dmmwb4688_v1(files):
    design = json.loads((DESIGNS / f"{STEP1}.json").read_text())
    edits = dmmcfg_edits(DESIGNS)
    assert [(e.offset, e.old, e.new) for e in edits] == [
        (int(s["file_offset_of_the_same_field"], 16), bytes.fromhex(s["old"]), bytes.fromhex(s["new"]))
        for s in design["sites"]]
    assert sum(len(e.new) for e in edits) == 16
    for e in edits:
        assert files["DmmCfg.bin"][e.offset:e.offset + len(e.old)] == e.old


@needs_designs
def test_load_design_refuses_a_changed_file(tmp_path):
    for name in _layout()["designs"]:
        shutil.copy(DESIGNS / f"{name}.json", tmp_path)
    assert load_design(tmp_path, "lcdfill_v2")["design"] == "lcdfill_v2"
    (tmp_path / "lcdfill_v2.json").write_text((tmp_path / "lcdfill_v2.json").read_text().replace("03000000", "03000001", 1))
    with pytest.raises(ToolError, match="SHA-256"):
        load_design(tmp_path, "lcdfill_v2")


def test_runs_of_takes_code_sites_in_libmpr_only():
    mpr = {"binary": "libmpr.so", "file_off": "0x11f4e0", "va": "0x11f4e0", "old": "2090801a", "new": "00008052", "code": True}
    assert runs_of({"patches": [mpr]}, None) == [Run(Space.MPR, 0x11f4e0, bytes.fromhex("2090801a"), bytes.fromhex("00008052"))]
    with pytest.raises(ToolError, match="libmpr.so only"):
        runs_of({"patches": [dict(mpr, binary="av-cam.bin")]}, None)


def test_native_sources_are_pinned():
    pins = _layout()["sources"]
    for name, value in pins.items():
        assert hashlib.sha256((_NATIVE / name).read_bytes()).hexdigest() == value, name


def test_generated_config_and_linker_script_follow_layout():
    lay = _layout()
    header = _config_header(lay, RECORD)
    assert "#define OG_RECORD_ROOT 0x12b9cc0u" in header and "#define OG_CP_VA_END 0x10000000u" in header
    assert "#define OG_DRIVE_MODE_NTSC 0x8c0u" in header and "#define OG_DRIVE_MODE_PAL 0x880u" in header
    script = _linker_script(lay)
    assert ".text 0x9063000" in script and ".bss 0x907f000" in script
    assert "hook_first_previous = 0x9012bd8;" in script and "dlsym = 0x17166a0;" in script
    assert "#define OG_MENU_MID 1593u\n" in header and "#define OG_MENU_ALIAS_BASE 5607u\n" in header
    assert "og_menu_previous_disabled = 0x8fb877c;" in script and "native_mode = 0x28ecdf8;" in script
    assert "og_native" not in script and "OG_MENU_TEXT" not in header


def build_args(tmp_path, **overrides):
    keys = _ROOT / "keys/published"
    args = {"base": tmp_path / "base.DAT", "designs": DESIGNS, "zig": tmp_path / "zig", "output": tmp_path / "out",
            "private_key": keys / "A7M4_updater_private.pem", "public_key": keys / "A7M4_updater_public.pem"}
    (tmp_path / "base.DAT").write_bytes(b"not v0.1.3")
    (tmp_path / "zig").write_bytes(b"not zig")
    return argparse.Namespace(**(args | overrides))


def test_build_refuses_an_existing_output_and_wrong_inputs(tmp_path):
    (tmp_path / "taken").mkdir()
    with pytest.raises(ToolError, match="new output directory"):
        build(build_args(tmp_path, output=tmp_path / "taken"))
    with pytest.raises(ToolError, match="v0.1.3"):
        build(build_args(tmp_path))
    assert not (tmp_path / "out").exists()
    base = WORKSPACE / "firmware/BODYDATA-custom-v0.1.3.DAT"
    if base.is_file():
        with pytest.raises(ToolError, match="Zig"):
            build(build_args(tmp_path, base=base))
        assert not (tmp_path / "out").exists()


def test_compose_ap_refuses_another_appfw():
    module = Module(b"\0" * 16, 0x9063000, 0x907f000, 0, {})
    with pytest.raises(ToolError, match="v0.1.3"):
        compose_ap(b"\x7fELF" + bytes(64), module, _layout())


# --- og_reconcile.c and og.c on the host, against a fake og_mem seeded with v0.1.3's files --------------------------

class Camera:
    def __init__(self, binary, files, family, image):
        fam = RECORD["families"]
        offsets = ",".join(map(str, fam["ntsc"]["offsets_in_record"]))
        assert fam["ntsc"]["offsets_in_record"] == fam["pal"]["offsets_in_record"]
        self.p = subprocess.Popen([str(binary), str(DEV / FILES["cpapp-b.bin"][0]), str(DEV / FILES["av-cam.bin"][0]),
                                   str(DEV / FILES["libmpr.so"][0]), family.value, offsets, fam["ntsc"]["old"],
                                   fam["pal"]["old"]], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        self.family = family
        for preset in image.presets:   # CP RAM beyond the loaded image holds its stock bytes
            for run in preset.runs:
                if run.space is Space.CP and run.addr - _CP_IMAGE_VA >= len(files["cpapp-b.bin"]):
                    assert self.send(f"seed 0 {run.addr} {run.old.hex()}") == ["ok"]

    def send(self, line):
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()
        return self.p.stdout.readline().split()

    def status(self, line):
        words = self.send(line)
        return int(words[1]), int(words[3]), int(words[5])

    def diff(self):
        self.p.stdin.write("diff\n")
        self.p.stdin.flush()
        result = {}
        for line in iter(self.p.stdout.readline, "end\n"):
            kind, a, b, c = line.split()
            result[(Space.CP_DRIVE_RECORD, int(b), int(a)) if kind == "r" else (Space(int(a)), int(b))] = int(c)
        return result

    def close(self):
        self.p.stdin.close()
        assert self.p.wait(timeout=60) == 0


def state_of(mode, family):
    """The diff the fake memory must show with a mode applied: every changed design byte at its new value."""
    if mode is Mode.OFF:
        return {}
    index = 0 if family is Family.NTSC else 1
    return {((s, a, index) if s is Space.CP_DRIVE_RECORD else (s, a)): new
            for (s, a), (_, new) in expected_bytes(mode, family).items()}


@pytest.fixture(scope="module")
def host(image, files, tmp_path_factory):
    clang = shutil.which("clang") or shutil.which("cc")
    if not clang:
        pytest.skip("no host C compiler")
    tmp = tmp_path_factory.mktemp("og_host")
    (tmp / "setting").mkdir()
    blob = image.encode()
    (tmp / "og_sites_host.c").write_text(
        "#include <stdint.h>\n_Alignas(8) const uint8_t og_sites[] = {" + ",".join(map(str, blob)) + "};\n")
    (tmp / "og_config.h").write_text(_config_header(_layout(), RECORD))
    binary = tmp / "og_host"
    subprocess.run([clang, "-std=c11", "-D_DEFAULT_SOURCE", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fno-strict-aliasing",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-DOG_HOST",
                    f"-DOG_SETTING_DIR=\"{tmp / 'setting'}\"", "-I", str(_NATIVE), "-I", str(tmp),
                    str(_ROOT / "tests/native/og_host.c"), str(_NATIVE / "og_reconcile.c"), str(_NATIVE / "og.c"),
                    str(tmp / "og_sites_host.c"), "-o", str(binary)], check=True)
    return binary


@pytest.fixture
def setting(host):
    """/setting for og.c in the harness, empty at the start of each test."""
    directory = host.parent / "setting"
    for item in directory.iterdir():
        item.unlink()
    return directory


@pytest.fixture
def camera(host, image, files, setting, request):
    cam = Camera(host, files, getattr(request, "param", Family.NTSC), image)
    yield cam
    cam.close()


both = pytest.mark.parametrize("camera", [Family.NTSC, Family.PAL], indirect=True, ids=["ntsc", "pal"])


@both
def test_reconcile_reaches_each_mode_and_back_to_stock(camera):
    fam = camera.family
    assert camera.status("reconcile 0")[::2] == (OK, 0)
    assert camera.diff() == {}
    assert camera.status("reconcile 1")[0] == OK
    assert camera.diff() == state_of(Mode.UHD_32, fam)
    status, reads, writes = camera.status("reconcile 1")
    assert (status, writes) == (OK, 0) and reads
    assert camera.status("reconcile 2")[0] == OK
    assert camera.diff() == state_of(Mode.K57_32, fam)
    assert camera.status("reconcile 2")[::2] == (OK, 0)
    assert camera.status("reconcile 1")[0] == OK
    assert camera.diff() == state_of(Mode.UHD_32, fam)
    assert camera.status("reconcile 0")[0] == OK
    assert camera.diff() == {}


@both
def test_foreign_bytes_are_refused_with_nothing_written(camera, image):
    preset = next(p for p in image.presets if p.family is camera.family and p.mode is Mode.K57_32)
    run = next(r for r in preset.runs if r.space is Space.AVCAM)
    foreign = bytes(b ^ 0x5a for b in run.old)
    assert foreign not in (run.old, run.new)
    assert camera.send(f"corrupt 2 {run.addr} {foreign.hex()}") == ["ok"]
    before = camera.diff()
    assert camera.status("reconcile 2")[::2] == (FOREIGN, 0)
    assert camera.status("reconcile 0")[::2] == (FOREIGN, 0)
    assert camera.diff() == before


@both
def test_foreign_stored_record_is_refused(camera, image):
    fam = RECORD["families"][camera.family.value]
    index = 0 if camera.family is Family.NTSC else 1
    camera.send(f"corrupt 1 {fam['offsets_in_record'][0]} ff")   # the record of the camera's own family
    assert camera.status("reconcile 1")[::2] == (FOREIGN, 0)
    assert camera.diff() == {(Space.CP_DRIVE_RECORD, fam["offsets_in_record"][0], index): 0xff}


@pytest.mark.parametrize("how", ["before", "after"])
@pytest.mark.parametrize("nth", [1, 2, 40, 300, 640])
def test_a_failed_write_puts_every_site_back(camera, nth, how):
    assert camera.status("reconcile 1")[0] == OK
    start = camera.diff()
    camera.send(f"fail {nth} {how}")
    status, _, writes = camera.status("reconcile 2")
    if writes < nth:   # the transition needs fewer writes: nothing failed
        assert status == OK
        return
    assert status == IO
    assert camera.diff() == start
    camera.send(f"fail {nth} {how}")
    status, _, writes = camera.status("reconcile 0")
    assert (status, camera.diff()) == ((IO, start) if writes >= nth else (OK, {}))


def test_a_failed_first_apply_leaves_stock(camera):
    camera.send("fail 10 after")
    assert camera.status("reconcile 2")[0] == IO
    assert camera.diff() == {}


def test_a_failed_put_back_reports_undo_failed(camera):
    camera.send("fail 10 all")
    assert camera.status("reconcile 1")[0] == UNDO_FAILED


def test_not_ready_writes_nothing(camera):
    camera.send("ready 0")
    assert camera.status("reconcile 1")[::2] == (NOT_READY, 0)
    camera.send("ready 1")
    assert camera.status("reconcile 1")[0] == OK


def test_select_persists_and_power_on_restores(host, image, files, setting):
    first = Camera(host, files, Family.NTSC, image)
    assert first.send("selected") == ["mode", "0"]
    assert first.send("power_on") == ["reads", "0", "writes", "0"]   # Off and nothing applied: no memory access
    assert first.send("dial 3") == ["ok"]
    assert first.status("select 1")[0] == WRONG_DIAL
    assert first.send("dial 0") == ["ok"]
    assert first.status("select 1")[0] == OK
    assert first.send("selected") == ["mode", "1"]
    assert first.diff() == state_of(Mode.UHD_32, Family.NTSC)
    record = (setting / "open_gate.dat").read_bytes()
    assert record == struct.pack("<4sIII", b"OGT1", 1, 1, 0xfffffffe)
    assert not (setting / "open_gate.tmp").exists()
    first.close()

    second = Camera(host, files, Family.NTSC, image)
    assert second.send("dial 3") == ["ok"]
    assert second.send("power_on")[3] == "0"        # movie: nothing written
    assert second.diff() == {}
    assert second.send("dial 0") == ["ok"]
    second.send("power_on")
    assert second.diff() == state_of(Mode.UHD_32, Family.NTSC)
    assert second.send("power_on")[3] == "0"        # already in place: reads only
    assert second.status("select 0")[0] == OK
    assert second.diff() == {} and second.send("load") == ["mode", "0"]
    second.close()


def test_bad_record_files_mean_off(host, image, files, setting):
    for content in (b"", b"OGT1" + bytes(12), struct.pack("<4sIII", b"OGT1", 1, 1, 1),
                    struct.pack("<4sIII", b"OGT1", 2, 1, 0xfffffffe), struct.pack("<4sIII", b"OGT1", 1, 7, ~7 & 0xffffffff),
                    struct.pack("<4sIII", b"OGT1", 1, 2, 0xfffffffd) + b"x"):
        (setting / "open_gate.dat").write_bytes(content)
        cam = Camera(host, files, Family.NTSC, image)
        assert cam.send("load") == ["mode", "0"], content
        cam.close()
    (setting / "open_gate.dat").write_bytes(struct.pack("<4sIII", b"OGT1", 1, 2, 0xfffffffd))
    cam = Camera(host, files, Family.NTSC, image)
    assert cam.send("load") == ["mode", "2"]
    cam.close()


# --- og_mem.c on the host, unchanged, against fake DMA, /dev/mem and /proc/self/maps --------------------------------

def test_og_mem_on_the_host(image, tmp_path):
    clang = shutil.which("clang") or shutil.which("cc")
    if not clang:
        pytest.skip("no host C compiler")
    blob = image.encode()
    (tmp_path / "og_sites_host.c").write_text(
        "#include <stdint.h>\n_Alignas(8) const uint8_t og_sites[] = {" + ",".join(map(str, blob)) + "};\n")
    (tmp_path / "og_config.h").write_text(_config_header(_layout(), RECORD))
    fam = RECORD["families"]
    assert fam["ntsc"]["offsets_in_record"] == fam["pal"]["offsets_in_record"]
    c_bytes = lambda h: ",".join(str(b) for b in bytes.fromhex(h))
    (tmp_path / "og_mem_seed.h").write_text(
        "static const unsigned seed_off[] = {" + ",".join(map(str, fam["ntsc"]["offsets_in_record"])) + "};\n"
        f"static const uint8_t seed_ntsc_old[] = {{{c_bytes(fam['ntsc']['old'])}}};\n"
        f"static const uint8_t seed_pal_old[] = {{{c_bytes(fam['pal']['old'])}}};\n")
    binary = tmp_path / "og_mem_host"
    subprocess.run([clang, "-std=c11", "-D_DEFAULT_SOURCE", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                    "-fno-strict-aliasing", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    "-I", str(_NATIVE), "-I", str(tmp_path), str(_ROOT / "tests/native/og_mem_host.c"),
                    str(tmp_path / "og_sites_host.c"), "-o", str(binary)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "ok read above the cached page" in result.stdout
    assert "ok the NTSC camera's records: NTSC, the record of 0x8c0 at index 10" in result.stdout


# --- PR 2: the Open Gate page (the custom firmware's Custom Shutter Sound page, taken over) ------------------------

APP = DEV / "nflasha15_unpacked/share/app"
APPFW = DEV / "nflasha15_unpacked/lib/appFw.so"
STOCK_APP = Path(os.environ.get("OG_602_DEV", WORKSPACE / "unpacked/BODYDATA-6.02/firmware.tar_unpacked/0700_part_image/dev")) \
    / "nflasha15_unpacked/share/app"
ZIG = Path(os.environ.get("OG_ZIG", WORKSPACE / "tools/zig-aarch64-macos-0.15.2/zig"))
MENU = _layout()["menu"]
MID, PAGE_MID, ALIAS = MENU["mid"], MENU["page_mid"], MENU["alias_base"]
TEXT = {k: int(v, 16) for k, v in MENU["text"].items()}
OPTION_TEXT = (TEXT["off"], TEXT["uhd_32"], TEXT["k57_32"])
NATIVE_TRUE = 0x1c9efbc                   # stock "mov w0, #1; ret"
MEDIA, SILENT = 0x50fc298, 0x50fd3d8      # MENU > Shooting > Media, MENU > Shooting > Shutter/Silent
GROUP_BYTES = 0x2e0                       # 16-byte header {id, title, count, 0} + 30 entries {u64 mid, leaf*, page*}
PAGE, LIVE_PAGE, FOCUS_PAGE = 0x8fc0400, 0x8ff8000, 0x8fc0710
LEAF, ROWS, PROFILE = 0x8fc0490, 0x8fc0610, 0x8fc3d28
SLOT = MEDIA + 16 + 4 * 24                # Media's first free entry

needs_appfw = pytest.mark.skipif(not APPFW.is_file(), reason="no v0.1.3 tree")


class Image:
    """v0.1.3's appFw.so (or a composed one) by VA, with its relocation table."""

    def __init__(self, data):
        import io
        import numpy as np
        from elftools.elf.elffile import ELFFile
        self.data = data
        self.elf = ELFFile(io.BytesIO(data))
        self.loads = [s for s in self.elf.iter_segments() if s["p_type"] == "PT_LOAD"]
        rela = self.elf.get_section_by_name(".rela.dyn")
        self.rela = np.frombuffer(data, dtype="<u8", count=rela["sh_size"] // 8, offset=rela["sh_offset"]).reshape(-1, 3)

    def offset(self, va):
        s = next(s for s in self.loads if s["p_vaddr"] <= va < s["p_vaddr"] + s["p_filesz"])
        return s["p_offset"] + va - s["p_vaddr"]

    def bytes(self, va, n):
        return self.data[self.offset(va):self.offset(va) + n]

    def u32(self, va):
        return struct.unpack("<I", self.bytes(va, 4))[0]

    def relatives(self, va):
        """Addends of every R_AARCH64_RELATIVE at va (the loader writes each in turn)."""
        return [int(r[2]) for r in self.rela[(self.rela[:, 0] == va) & (self.rela[:, 1] == 1027)]]

    def relative(self, va):
        found = set(self.relatives(va))
        assert len(found) <= 1, f"{va:#x} has differing relocations"
        return found.pop() if found else None

    def branch(self, va):
        word = self.u32(va)
        imm = word & 0x3ffffff
        return {0x94000000: "bl", 0x14000000: "b"}.get(word & 0xfc000000), va + 4 * (imm - ((imm & 0x2000000) << 1))

    def table(self, base, entries, stride):
        return [self.u32(base + stride * i) for i in range(entries)]


@pytest.fixture(scope="module")
def v013():
    if not APPFW.is_file():
        pytest.skip("no v0.1.3 tree")
    data = APPFW.read_bytes()
    assert hashlib.sha256(data).hexdigest() == _layout()["inputs"]["nflasha15:lib/appFw.so"]
    return Image(data)


def group_entries(img, group):
    """A menu group's live entries as (mid, leaf, page), pointers as their relocations give them."""
    return [(img.u32(group + 16 + 24 * i), img.relative(group + 24 + 24 * i) or 0, img.relative(group + 32 + 24 * i) or 0)
            for i in range(img.u32(group + 8))]


def page_children(img, page):
    return [(img.u32(page + 0x70 + 16 * i), img.relative(page + 0x78 + 16 * i)) for i in range(img.u32(page + 0x64))]


@pytest.fixture(scope="module")
def menu_host(tmp_path_factory):
    clang = shutil.which("clang") or shutil.which("cc")
    if not clang:
        pytest.skip("no host C compiler")
    tmp = tmp_path_factory.mktemp("og_menu_host")
    (tmp / "og_config.h").write_text(_config_header(_layout(), RECORD))
    binary = tmp / "og_menu_host"
    subprocess.run([clang, "-std=c11", "-D_DEFAULT_SOURCE", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fno-strict-aliasing",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-I", str(_NATIVE), "-I", str(tmp),
                    str(_ROOT / "tests/native/og_menu_host.c"), str(_NATIVE / "og_menu.c"), "-o", str(binary)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout.splitlines()


def test_og_menu_callbacks_on_the_host(menu_host):
    lines = menu_host
    assert not [line for line in lines if line.startswith("FAIL")]
    for check in ("getter reflects og_selected", "setter stages the browsed value", "commit selects the option's mode",
                  "commit reports any other status as failure", "a failed commit drops the staged value",
                  "commit refuses an alias below the item's", "commit refuses an alias above the item's",
                  "disabled is 1 exactly in movie", "disabled is 0 without a dial",
                  "the item never reaches the previous check", "other mids chain to the previous check"):
        assert f"ok {check}" in lines, check
    assert lines.count("ok commit reports any other status as failure") == 7   # OG_BUSY .. OG_NOT_SAVED


@needs_appfw
def test_v013_sound_page_is_a_complete_custom_item(v013):
    """What the takeover relies on, in v0.1.3: the page and switch are registered in every native map, accepted by the
    custom firmware's filters, and listed last on Shutter/Silent; the switch's profile +0x50 is the stock 'return 1'.
    0x1c14e6c answers "disabled": its caller 0x1ba807c opens the item when it returns 0 (b.eq to 0x1ba80a0) and
    otherwise calls 0x1c14efc (the "currently disabled" caution) and returns; on the camera the item with 1 in stills
    was refused there and opened in movie."""
    leaves = dict(zip(v013.table(0x8fc0a20, 4, 16), (v013.relative(0x8fc0a28 + 16 * i) for i in range(4))))
    assert leaves == {MID: LEAF, MID + 1: 0x8fc0550, MID + 3: 0x8fc07a0, MID + 4: 0x8fc0860}
    assert dict(zip(v013.table(0x8fc0a60, 2, 16), (v013.relative(0x8fc0a68 + 16 * i) for i in range(2)))) == \
        {PAGE_MID: LIVE_PAGE, PAGE_MID + 3: 0x8ff86e0}
    options = dict(zip(v013.table(0x8fc0a80, 4, 16), (v013.relative(0x8fc0a88 + 16 * i) for i in range(4))))
    assert options == {ALIAS: ROWS, ALIAS + 1: ROWS + 0x80, ALIAS + 2: 0x8fc0920, ALIAS + 3: 0x8fc09a0}
    assert v013.table(0x8fc3fa8, 6, 16) == list(range(PAGE_MID, PAGE_MID + 6)) and v013.relative(0x8fc3fb8 + 8) == PROFILE
    assert v013.u32(PROFILE) == MID and v013.relative(PROFILE + 0x48) == v013.relative(PROFILE + 0x50) == NATIVE_TRUE
    assert v013.u32(NATIVE_TRUE) == 0x52800020 and v013.u32(NATIVE_TRUE + 4) == 0xd65f03c0
    sub_w16_w1 = lambda imm: 0x51000000 | (imm << 10) | (1 << 5) | 16
    assert v013.u32(0x8f900a0) == v013.u32(0x8f900b8) == sub_w16_w1(PAGE_MID)       # supported / mapped: mids 1592..
    assert v013.u32(0x8f900a4) == v013.u32(0x8f900bc) == 0x7100161f                  # cmp w16, #5  (.. 1597)
    assert v013.branch(0x1c08bc0)[1] == 0x8fb90c8 and v013.u32(0x8fb90dc) >> 26 == 5  # chain reaches 0x8f900a0
    assert v013.u32(0x8fa1000) == 0x52800010 | (ALIAS << 5) and v013.u32(0x8fa1008) == 0x71005e1f   # ids 5607..5630
    assert v013.branch(0x1c18004) == ("b", 0x8fa1000)
    assert v013.u32(0x1c1ea74) == 0xf9402ac0 and v013.u32(0x1c1ea7c) == 0x39000a80   # ldr x0, [x22, #0x50]; strb w0, [x20, #2]
    assert v013.branch(0x1ba807c) == ("bl", 0x1c14e6c) and v013.u32(0x1ba8084) & 0xff00001f == 0x54000000   # b.eq: open
    assert (v013.u32(0x1ba8084) >> 5) & 0x7ffff == (0x1ba80a0 - 0x1ba8084) // 4                         # ... at 0x1ba80a0
    assert v013.branch(0x1ba808c) == ("bl", 0x1c14efc)                                                  # nonzero: caution
    assert group_entries(v013, SILENT)[-2:] == [(PAGE_MID, 0, PAGE), (PAGE_MID + 3, 0, FOCUS_PAGE)]


@pytest.fixture(scope="module")
def composed(image, v013, tmp_path_factory):
    if not ZIG.is_file():
        pytest.skip("pinned Zig not available")
    module = compile_module(ZIG, image, tmp_path_factory.mktemp("og_module") / "module")
    return module, Image(compose_ap(v013.data, module, _layout()))


def takeover_places(v013):
    """File (offset, bytes) of every place the takeover may change, from layout.json."""
    rela = v013.elf.get_section_by_name(".rela.dyn")
    places = [(v013.offset(MEDIA + 8), 4), (v013.offset(SLOT), 24), (v013.offset(SILENT + 8), 4),
              (v013.offset(SILENT + 16 + 11 * 24), 48), (v013.offset(PAGE + 0x64), 4), (v013.offset(LIVE_PAGE + 0x64), 4),
              (v013.offset(LEAF + 0x0c), 4), (v013.offset(LEAF + 0x54), 4), (v013.offset(ROWS), 3 * 0x80),
              (v013.offset(int(MENU["parent_pair"]["va"], 16)), 8)]
    for r in MENU["relocations"]["retarget"]:
        index = int((v013.rela[:, 0] == int(r["r_offset"], 16)).nonzero()[0][0])
        places += [(rela["sh_offset"] + 24 * index, 24), (v013.offset(int(r["to"], 16)), 8)]
    return places


def test_composed_appfw_changes_only_the_listed_places(composed, v013):
    import numpy as np
    module, out = composed
    differ = np.flatnonzero(np.frombuffer(v013.data, np.uint8) != np.frombuffer(out.data, np.uint8))
    hooks = [v013.offset(int(h["va"], 16)) for h in _layout()["hooks"]]
    allowed = [(0x38, 0x3a), (0x5e73000, 0x5e73000 + len(module.text)), (0x5e80000, 0x5e80000 + 17 * 56)]
    allowed += [(at, at + 4) for at in hooks] + [(at, at + n) for at, n in takeover_places(v013)]
    assert all(any(lo <= int(i) < hi for lo, hi in allowed) for i in differ)
    assert set(hooks) <= {int(i) & ~3 for i in differ}
    assert len(out.rela) == len(v013.rela)
    tags = lambda img: {t["d_tag"]: t["d_val"] for t in img.elf.get_section_by_name(".dynamic").iter_tags()}
    assert tags(out) == tags(v013)


def test_composed_media_lists_the_open_gate_page_and_shutter_silent_loses_both_sound_pages(composed, v013):
    _, out = composed
    native = group_entries(v013, MEDIA)
    assert [e[0] for e in native] == [188, 189, 193, 194]   # Format, Rec. Media Settings, Recover Image DB, Media Info.
    assert group_entries(out, MEDIA) == native + [(PAGE_MID, 0, PAGE)]
    assert out.relatives(SLOT + 16) == [PAGE] * 4 and out.relative(SLOT + 8) is None
    assert not any(out.bytes(SLOT + 24, MEDIA + GROUP_BYTES - SLOT - 24))
    assert group_entries(out, SILENT) == group_entries(v013, SILENT)[:-2]
    freed = SILENT + 16 + 11 * 24
    assert not any(out.bytes(freed, SILENT + GROUP_BYTES - freed))
    assert not ((out.rela[:, 0] >= freed) & (out.rela[:, 0] < SILENT + GROUP_BYTES)).any()
    # Every other group of the menu tree keeps its bytes.
    tree = 0x50f6380
    size = 8 + v013.u32(tree) * 0x2e10
    before, after = v013.bytes(tree, size), out.bytes(tree, size)
    changed = {tree + i for i in range(size) if before[i] != after[i]}
    assert changed <= set(range(MEDIA, MEDIA + GROUP_BYTES)) | set(range(SILENT, SILENT + GROUP_BYTES))


def test_composed_page_holds_only_the_open_gate_setting(composed, v013):
    module, out = composed
    for page, before in ((PAGE, 2), (LIVE_PAGE, 3)):
        assert out.u32(page) == TEXT["page"] and len(page_children(v013, page)) == before
        assert page_children(out, page) == [(MID, LEAF)]
        assert out.bytes(page, 0x64) == v013.bytes(page, 0x64)   # header kept: title, controller 0x2d7, flags
    assert out.u32(LEAF) == TEXT["title"] and out.u32(LEAF + 0x0c) == 0xffffffff and out.u32(LEAF + 0x54) == 3
    assert out.relative(LEAF + 0x58) == ROWS and out.relative(LEAF + 0x70) == NATIVE_TRUE
    for at, name in ((0x78, "og_menu_getter"), (0x80, "og_menu_setter"), (0x88, "og_menu_commit")):
        assert out.relative(LEAF + at) == module.symbols[name] and struct.unpack("<Q", out.bytes(LEAF + at, 8))[0] == module.symbols[name]
    assert out.relative(LEAF + 0xa8) == 0x8f98430 and out.u32(0x8f98430) == 0x2a1f03e0   # default: mov w0, wzr (Off)
    assert out.bytes(PROFILE, 0x58) == v013.bytes(PROFILE, 0x58)   # the profile stays v0.1.3's
    assert out.relative(PROFILE + 0x48) == out.relative(PROFILE + 0x50) == NATIVE_TRUE


def test_composed_rows_are_off_4k_and_57k(composed, v013):
    _, out = composed
    f = {k: int(v, 16) for k, v in MENU["row_fields"].items() if k != "what"}
    for i in range(3):
        row = ROWS + 0x80 * i
        assert out.u32(row) == out.u32(row + 4) == ALIAS + i
        assert out.u32(row + f["text"]) == out.u32(row + f["help"]) == OPTION_TEXT[i]
        assert out.u32(row + f["images"]) == out.u32(row + f["images"] + 4) == 0xffffffff and out.u32(row + f["value"]) == i
        source = 0x8fc0920 if i == 2 else row   # the third row is the focus switch's first, moved
        kept = [k for k in range(0, 0x80, 4) if k not in (f["text"], f["help"], f["images"], f["images"] + 4, f["value"], f["row_id"])]
        assert [out.u32(row + k) for k in kept] == [v013.u32(source + k) for k in kept]
        assert out.u32(row + f["row_id"]) == 0x001203ee + i
        assert not ((out.rela[:, 0] >= row) & (out.rela[:, 0] < row + 0x80)).any()
    options = dict(zip(out.table(0x8fc0a80, 4, 16), (out.relative(0x8fc0a88 + 16 * i) for i in range(4))))
    assert options == {ALIAS: ROWS, ALIAS + 1: ROWS + 0x80, ALIAS + 2: ROWS + 0x100, ALIAS + 3: 0x8fc09a0}
    pairs = [struct.unpack("<II", out.bytes(0x8f91c50 + 8 * i, 8)) for i in range(8)]
    assert pairs[:4] == [(ALIAS + i, ALIAS + i) for i in range(4)]                     # value map: unchanged
    assert pairs[4:] == [(ALIAS, MID), (ALIAS + 1, MID), (ALIAS + 2, MID), (ALIAS + 3, MID + 3)]   # option -> leaf
    # Nothing points at the bytes the third row replaced, except the option map entry that now names it.
    into = (out.rela[:, 2] >= FOCUS_PAGE) & (out.rela[:, 2] < FOCUS_PAGE + 0x80)
    assert [int(x) for x in out.rela[into, 0]] == [0x8fc0aa8]


def test_composed_relocations_change_only_the_listed_entries(composed, v013):
    module, out = composed
    differ = [i for i in range(len(v013.rela)) if (v013.rela[i] != out.rela[i]).any()]
    spec = MENU["relocations"]["retarget"]
    assert len(differ) == sum(r["r_offset"] != r["to"] or r["new"] != r["addend"] for r in spec)
    for r in spec:
        old = int(r["r_offset"], 16)
        index = int((v013.rela[:, 0] == old).nonzero()[0][0])
        value = int(r["new"], 16) if r["new"].startswith("0x") else module.symbols[r["new"]]
        assert tuple(int(x) for x in v013.rela[index]) == (old, 1027, int(r["addend"], 16))
        assert tuple(int(x) for x in out.rela[index]) == (int(r["to"], 16), 1027, value)


def test_takeover_refuses_another_layout(v013):
    from a7m4.open_gate import _take_over_sound_page
    lay = _layout()
    symbols = {"og_menu_getter": 0x9063000, "og_menu_setter": 0x9063010, "og_menu_commit": 0x9063020}
    _take_over_sound_page(bytearray(v013.data), v013.data, v013.elf, v013.loads, symbols, lay)
    for va, message in ((MEDIA + 8, "Group"), (SILENT + 16 + 12 * 24 + 16, "two sound pages"), (PAGE + 0x64, "Sound page"),
                        (LEAF + 0x54, "switch leaf"), (0x8fc0920, "Row"), (ROWS + 0x80 + 8, "Row"),
                        (int(MENU["parent_pair"]["va"], 16) + 4, "Parent pair")):
        tampered = bytearray(v013.data)
        tampered[v013.offset(va)] ^= 1
        with pytest.raises(ToolError, match=message):
            _take_over_sound_page(bytearray(tampered), bytes(tampered), v013.elf, v013.loads, symbols, lay)
    moved = json.loads(json.dumps(lay))
    moved["menu"]["relocations"]["retarget"][4]["addend"] = "0x8f97f64"
    with pytest.raises(ToolError, match="Relocation at 0x8fc0508"):
        _take_over_sound_page(bytearray(v013.data), v013.data, v013.elf, v013.loads, symbols, moved)
    with pytest.raises(ToolError, match="og_menu_commit"):
        _take_over_sound_page(bytearray(v013.data), v013.data, v013.elf, v013.loads,
                              {k: v for k, v in symbols.items() if k != "og_menu_commit"}, lay)


def test_composed_hooks_reach_the_module_and_chain_to_the_previous_heads(composed, v013):
    module, out = composed
    hooks = _layout()["hooks"]
    assert [h["entry"] for h in hooks] == ["og_hook_first", "og_hook_second", "og_menu_disabled"]
    for hook in hooks:
        va = int(hook["va"], 16)
        assert v013.branch(va) == (hook["kind"], int(hook["previous_target"], 16))
        assert out.branch(va) == (hook["kind"], module.symbols[hook["entry"]])
    # og_menu_disabled: cmp w0, #MID; b.ne -> the previous head (custom sounds' Volume filter, then the stock check).
    at = module.symbols["og_menu_disabled"]
    assert out.u32(at) == 0x7100001f | (MID << 10)
    word = out.u32(at + 4)
    imm = (word >> 5) & 0x7ffff
    target = at + 4 + 4 * (imm - ((imm & 0x40000) << 1))
    assert word & 0xff00001f == 0x54000001 and out.branch(target) == ("b", int(hooks[2]["previous_target"], 16))


# --- the 68 string banks -----------------------------------------------------------------------------------------------

@pytest.fixture(scope="module")
def banks():
    if not APP.is_dir():
        pytest.skip("no v0.1.3 tree")
    return _edit_banks(lambda path: (APP.parent.parent / path).read_bytes(), MENU["strings"])


REPLACED = {int(k, 16): v for k, v in MENU["strings"]["replace"].items()}


def test_every_bank_names_the_page_setting_and_options(banks):
    assert len(banks) == 68
    assert REPLACED == {TEXT["page"]: "Open Gate", TEXT["title"]: "Mode", TEXT["uhd_32"]: "4K 3:2",
                        TEXT["k57_32"]: "5.7K 3:2"}
    for path, (old, new, evidence) in banks.items():
        before, after = _uxc_texts(old), _uxc_texts(new)
        assert len(new) == len(old), path
        assert {rid: after[rid] for rid in REPLACED} == REPLACED, path
        assert after[TEXT["off"]] == before[TEXT["off"]]   # Sony's own Off, unchanged
        assert {rid for rid in before.keys() | after.keys() if before.get(rid) != after.get(rid)} == set(REPLACED), path
        assert set(evidence["replaced"]) == {f"{rid:#010x}" for rid in REPLACED}


def test_banks_change_bytes_only_inside_the_replaced_records(banks):
    for path, (old, new, _) in banks.items():
        records = {rid: (start, text) for start, rid, text in _uxc_records(old)}
        assert {start for start, _, _ in _uxc_records(new)} == {start for start, _, _ in _uxc_records(old)}, path
        inside = set()
        for rid in REPLACED:
            start, text = records[rid]
            inside |= set(range(start + 4, start + ((4 + len(text) + 1 + 3) & ~3)))
        assert {i for i in range(len(old)) if old[i] != new[i]} <= inside, path


@pytest.mark.skipif(not STOCK_APP.is_dir(), reason="no stock 6.02 tree")
def test_banks_keep_every_sony_string(banks):
    for path, (old, new, _) in banks.items():
        stock, after = _uxc_texts((STOCK_APP.parent.parent / path).read_bytes()), _uxc_texts(new)
        assert {rid: after.get(rid) for rid in stock} == stock, path
        assert not set(REPLACED) & stock.keys()


def test_edit_bank_refuses_what_it_cannot_do():
    path = APP / "string_english_horizontal.uxc"
    if not path.is_file():
        pytest.skip("no v0.1.3 tree")
    data, strings = path.read_bytes(), MENU["strings"]
    with pytest.raises(ToolError, match="not in the bank"):
        _edit_bank(data, strings | {"replace": {"0x0200ab51": "Open Gate"}})
    with pytest.raises(ToolError, match="does not fit"):
        _edit_bank(data, strings | {"replace": {"0x0200ab5f": "A much longer label"}})
