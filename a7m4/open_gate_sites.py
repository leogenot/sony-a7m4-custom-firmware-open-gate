"""Open-gate site image: the one source of every address and byte the camera module touches.

Derived at build time from ArcaneLantern/a7iv-open-gate-test's design JSONs and native/open-gate/stored_record.json;
the generated og_sites.bin is never committed. Offline only.

Image format (version 1, little-endian, read by native/open-gate/og_image.h):

    header   magic 'OGSI', u16 version, u16 header_bytes (68), u32 total_bytes, u32 crc32 of everything after the
             header, u16 preset_count, u16 0, then 4 preset slots {u32 offset, u32 bytes, u16 runs, u8 mode, u8 family}
    run      u8 space, u8 flags (0), u16 len, u32 addr, old[len], new[len], zero padding to 4 bytes

Runs hold only changed bytes (gaps of up to _GAP unchanged bytes inside one design entry are kept, old == new). Across
all presets, two runs of one space either cover the same bytes or none in common, so the camera can compare a whole
run with the matching run of another preset.
"""
from __future__ import annotations

import hashlib
import json
import struct
import zlib
from collections.abc import Mapping
from dataclasses import dataclass
from enum import Enum, IntEnum
from io import BytesIO
from pathlib import Path

from .common import require

_ROOT = Path(__file__).resolve().parents[1]
_NATIVE = _ROOT / "native/open-gate"
_LAYOUT = _NATIVE / "layout.json"
_STORED_RECORD = _NATIVE / "stored_record.json"

_CP_IMAGE_VA = 0x108000            # cpapp-b.bin file offset = CP VA - 0x108000
_MAGIC = b"OGSI"
_VERSION = 1
_PRESET_SLOTS = 4
_RUN_MAX = 256                     # og_image.h OG_RUN_MAX: the camera's run buffer
_MAX_PRESET_RUNS = 1024            # og_reconcile.c OG_MAX_PRESET_RUNS: its run index
_MAX_FAMILY_RUNS = 1536            # og_image.h OG_MAX_FAMILY_RUNS: one state byte per run of a family
_MAX_IMAGE_BYTES = 0xffff          # run offsets are 16-bit on the camera
_GAP = 4                           # unchanged bytes kept inside a run rather than starting a new one
_HEADER = struct.Struct("<4sHHIIHH")
_SLOT = struct.Struct("<IIHBB")
_RUN = struct.Struct("<BBHI")
_HEADER_BYTES = _HEADER.size + _PRESET_SLOTS * _SLOT.size
_RECORD_NOTE = "26 bytes of the stored drive-parameter record"
_DMMCFG_MEMBER = "0700_part_image/dev/nflasha8"
_DMMCFG_PATH = "DMMCFG.BIN"


class Mode(IntEnum):
    OFF = 0
    UHD_32 = 1   # 3840x2560
    K57_32 = 2   # 5760x3840, 300 Mbps


class Space(IntEnum):
    CP = 0
    CP_DRIVE_RECORD = 1
    AVCAM = 2
    MPR = 3


class Family(Enum):
    NTSC = "ntsc"
    PAL = "pal"


_FAMILY_INDEX = {Family.NTSC: 0, Family.PAL: 1}   # og_family


@dataclass(frozen=True)
class Run:
    space: Space
    addr: int
    old: bytes
    new: bytes


@dataclass(frozen=True)
class Preset:
    mode: Mode
    family: Family
    runs: tuple[Run, ...]


@dataclass(frozen=True)
class FileEdit:
    """A build-time edit of a file inside a partition (step 1, DmmCfg.bin)."""
    member: str
    path: str
    offset: int
    old: bytes
    new: bytes


@dataclass(frozen=True)
class SiteImage:
    presets: tuple[Preset, ...]

    def encode(self) -> bytes:
        require(0 < len(self.presets) <= _PRESET_SLOTS, "OG_IMAGE", "1 to 4 presets")
        body, slots = bytearray(), []
        for preset in self.presets:
            require(preset.mode is not Mode.OFF and preset.runs, "OG_IMAGE", "A preset needs a mode and runs")
            start = _HEADER_BYTES + len(body)
            for run in preset.runs:
                require(0 < len(run.old) == len(run.new) <= _RUN_MAX and 0 <= run.addr < 1 << 32,
                        "OG_IMAGE", "Run length or address out of range")
                body += _RUN.pack(Space(run.space), 0, len(run.old), run.addr) + run.old + run.new
                body += bytes(-len(body) % 4)
            slots.append(_SLOT.pack(start, _HEADER_BYTES + len(body) - start, len(preset.runs), preset.mode,
                                    _FAMILY_INDEX[preset.family]))
        slots += [bytes(_SLOT.size)] * (_PRESET_SLOTS - len(slots))
        total = _HEADER_BYTES + len(body)
        family_runs = {f: sum(len(p.runs) for p in self.presets if p.family is f) for f in Family}
        require(total <= _MAX_IMAGE_BYTES and all(len(p.runs) <= _MAX_PRESET_RUNS for p in self.presets)
                and max(family_runs.values()) <= _MAX_FAMILY_RUNS, "OG_IMAGE",
                "Site image exceeds the camera module's limits (og_image.h)")
        header = _HEADER.pack(_MAGIC, _VERSION, _HEADER_BYTES, total, zlib.crc32(body), len(self.presets), 0)
        return header + b"".join(slots) + bytes(body)

    @staticmethod
    def decode(data: bytes) -> SiteImage:
        require(len(data) >= _HEADER_BYTES, "OG_IMAGE", "Truncated site image")
        magic, version, header_bytes, total, crc, count, reserved = _HEADER.unpack_from(data)
        require(magic == _MAGIC and version == _VERSION and header_bytes == _HEADER_BYTES and reserved == 0,
                "OG_IMAGE", "Not a version-1 site image")
        require(total == len(data) and total % 4 == 0 and zlib.crc32(data[header_bytes:]) == crc,
                "OG_IMAGE", "Site image length or CRC mismatch")
        require(0 < count <= _PRESET_SLOTS, "OG_IMAGE", "Preset count out of range")
        families = {v: k for k, v in _FAMILY_INDEX.items()}
        presets, cursor = [], header_bytes
        for slot in range(_PRESET_SLOTS):
            offset, size, runs, mode, family = _SLOT.unpack_from(data, _HEADER.size + slot * _SLOT.size)
            if slot >= count:
                require(not any((offset, size, runs, mode, family)), "OG_IMAGE", "Unused preset slot is not zero")
                continue
            require(offset == cursor and offset + size <= total and runs and family in families
                    and mode in (Mode.UHD_32, Mode.K57_32), "OG_IMAGE", "Preset slot out of range")
            parsed = []
            for _ in range(runs):
                require(cursor + _RUN.size <= offset + size, "OG_IMAGE", "Run outside its preset")
                space, flags, length, addr = _RUN.unpack_from(data, cursor)
                end = cursor + _RUN.size + 2 * length
                padded = end + (-end % 4)
                require(space in Space._value2member_map_ and flags == 0 and 0 < length <= _RUN_MAX
                        and padded <= offset + size and not any(data[end:padded]), "OG_IMAGE", "Malformed run")
                old = bytes(data[cursor + _RUN.size:cursor + _RUN.size + length])
                parsed.append(Run(Space(space), addr, old, bytes(data[cursor + _RUN.size + length:end])))
                cursor = padded
            require(cursor == offset + size, "OG_IMAGE", "Preset size does not match its runs")
            presets.append(Preset(Mode(mode), families[family], tuple(parsed)))
        require(cursor == total, "OG_IMAGE", "Trailing bytes after the last preset")
        return SiteImage(tuple(presets))


# Designs per preset, in the kit's apply order (open_gate_apply.py STATES), without step 1 (baked into
# DmmCfg.bin) and without the C3 cycle.
PRESETS: dict[tuple[Mode, Family], tuple[str, ...]] = {
    (Mode.UHD_32, Family.NTSC): ("ogsq_v3", "outsz2560_v1", "ogfile2560_hs_s_v2", "lcdfill_v2"),
    (Mode.UHD_32, Family.PAL): ("ogsq880_v1", "outsz2560_v1", "ogfile2560_hs_s_v2", "lcdfill_v2"),
    (Mode.K57_32, Family.NTSC): ("og57n_v1", "og57cp_v3", "ogfile57r300m1_v4", "lcdfill_v2"),
    (Mode.K57_32, Family.PAL): ("og57n880_v1", "og57cp_v3", "ogfile57r300m1_v4", "lcdfill_v2"),
}
STEP1 = "dmmwb4688_v1"


def _layout() -> dict:
    return json.loads(_LAYOUT.read_text("utf-8"))


def load_design(designs: Path, name: str) -> dict:
    """Read one design JSON in place, refusing any file whose SHA-256 is not the one pinned in layout.json."""
    pins = _layout()["designs"]
    require(name in pins, "OG_DESIGN", f"Design {name} is not pinned in layout.json")
    data = (Path(designs) / f"{name}.json").read_bytes()
    actual = hashlib.sha256(data).hexdigest()
    require(actual == pins[name], "OG_DESIGN", f"{name}.json: SHA-256 {actual} is not the pinned {pins[name]}")
    design = json.loads(data)
    require(design.get("design") == name, "OG_DESIGN", f"{name}.json names another design")
    return design


def runs_of(design: dict, record_patch: dict | None) -> list[Run]:
    """The design's sites as runs in its own order, then the stored-record bytes; unchanged bytes dropped."""
    entries: list[tuple[Space, int, bytes, bytes]] = []
    for patch in design.get("patches", []):
        old, new = bytes.fromhex(patch["old"]), bytes.fromhex(patch["new"])
        if "binary" in patch:
            space = {"av-cam.bin": Space.AVCAM, "libmpr.so": Space.MPR}[patch["binary"]]
            addr = int(patch["file_off"], 16)
            require(space is Space.MPR or not patch.get("code"), "OG_DESIGN", "Code sites are libmpr.so only")
            require(space is not Space.MPR or int(patch["va"], 16) == addr, "OG_DESIGN", "libmpr site VA != offset")
        else:
            require(patch.get("where") == "file" and patch.get("kind") in ("S", "R"), "OG_DESIGN",
                    "Unknown patch kind")
            space, addr = Space.CP, int(patch["addr"], 16)
        entries.append((space, addr, old, new))
    for site in design.get("sites", []):
        require("file_offset_of_the_same_field" not in site, "OG_DESIGN", "Step 1 is a FileEdit, not a run")
        entries.append((Space.CP, int(site["ram_address"], 16), bytes.fromhex(site["old"]), bytes.fromhex(site["new"])))
    if record_patch is not None:
        old, new = bytes.fromhex(record_patch["old"]), bytes.fromhex(record_patch["new"])
        offsets = record_patch["offsets_in_record"]
        require(len(offsets) == len(old) == len(new) == len(set(offsets)), "OG_RECORD", "Malformed record patch")
        entries += [(Space.CP_DRIVE_RECORD, o, old[i:i + 1], new[i:i + 1]) for i, o in enumerate(offsets)]
    runs: list[Run] = []
    for space, addr, old, new in entries:
        require(0 < len(old) == len(new), "OG_DESIGN", "Site old/new lengths differ")
        for start, end in _spans(old, new):
            if (runs and runs[-1].space is space and runs[-1].addr + len(runs[-1].old) == addr + start
                    and len(runs[-1].old) + end - start <= _RUN_MAX):
                last = runs.pop()
                runs.append(Run(space, last.addr, last.old + old[start:end], last.new + new[start:end]))
            else:
                for cut in range(start, end, _RUN_MAX):
                    stop = min(end, cut + _RUN_MAX)
                    runs.append(Run(space, addr + cut, old[cut:stop], new[cut:stop]))
    return runs


def _spans(old: bytes, new: bytes) -> list[tuple[int, int]]:
    """Changed byte spans [start, end) of one entry; up to _GAP unchanged bytes stay inside a span."""
    spans: list[list[int]] = []
    for i in (i for i in range(len(old)) if old[i] != new[i]):
        if spans and i - spans[-1][1] <= _GAP:
            spans[-1][1] = i + 1
        else:
            spans.append([i, i + 1])
    return [(s, e) for s, e in spans]


def compile_image(designs: Path, stored_record: Path) -> SiteImage:
    """All four presets; each run split where another preset's run starts or ends, so overlapping runs coincide."""
    record = json.loads(Path(stored_record).read_text("utf-8"))
    raw: dict[tuple[Mode, Family], list[Run]] = {}
    for (mode, family), names in PRESETS.items():
        runs: list[Run] = []
        for name in names:
            design = load_design(designs, name)
            patch = None
            if design.get("summary", {}).get("plus", "").startswith(_RECORD_NOTE):
                patch = record["families"][family.value]
                notes = [p["note"] for p in design["patches"] if p["kind"] == "R"]
                require(notes and all(patch["drive_mode"] in n for n in notes), "OG_RECORD",
                        f"{name}'s records are not those of drive mode {patch['drive_mode']}")
            runs += runs_of(design, patch)
        raw[(mode, family)] = runs
    stock: dict[tuple, int] = {}
    cuts: dict[tuple, set[int]] = {}
    for (_, family), runs in raw.items():
        for (space, addr), (old, _) in _byte_map(runs).items():
            key = (_region(space, family), addr)
            require(stock.setdefault(key, old) == old, "OG_IMAGE", f"Presets disagree on the stock byte at {key}")
        for run in runs:
            cuts.setdefault(_region(run.space, family), set()).update((run.addr, run.addr + len(run.old)))
    presets = []
    for (mode, family), runs in raw.items():
        pieces = []
        for run in runs:
            region = cuts[_region(run.space, family)]
            bounds = sorted(c for c in region if run.addr < c < run.addr + len(run.old))
            edges = [run.addr, *bounds, run.addr + len(run.old)]
            for start, end in zip(edges, edges[1:]):
                old, new = run.old[start - run.addr:end - run.addr], run.new[start - run.addr:end - run.addr]
                if old != new:
                    pieces.append(Run(run.space, start, old, new))
        presets.append(Preset(mode, family, tuple(pieces)))
    image = SiteImage(tuple(presets))
    _check_coincide(image)
    return image


def _region(space: Space, family: Family) -> tuple[Space, Family | None]:
    """The address space a run's addr is in: the stored record is a different record per family."""
    return (space, family if space is Space.CP_DRIVE_RECORD else None)


def _byte_map(runs: list[Run]) -> dict[tuple[Space, int], tuple[int, int]]:
    result: dict[tuple[Space, int], tuple[int, int]] = {}
    for run in runs:
        for i in range(len(run.old)):
            key = (run.space, run.addr + i)
            require(key not in result, "OG_IMAGE", f"Two sites of one preset touch {run.space.name} {key[1]:#x}")
            result[key] = (run.old[i], run.new[i])
    return result


def _check_coincide(image: SiteImage) -> None:
    seen: dict[tuple, dict[int, int]] = {}
    for preset in image.presets:
        for run in preset.runs:
            runs = seen.setdefault(_region(run.space, preset.family), {})
            require(runs.setdefault(run.addr, len(run.old)) == len(run.old), "OG_IMAGE",
                    "Runs at one address differ in length")
    for (space, _), runs in seen.items():
        ordered = sorted(runs.items())
        for (a, n), (b, _) in zip(ordered, ordered[1:]):
            require(a + n <= b, "OG_IMAGE", f"{space.name} runs at {a:#x} and {b:#x} overlap without coinciding")


def dmmcfg_edits(designs: Path) -> list[FileEdit]:
    """Step 1 (dmmwb4688_v1): the four memory-manager words, at their offsets in nflasha8's DmmCfg.bin."""
    design = load_design(designs, STEP1)
    edits = [FileEdit(_DMMCFG_MEMBER, _DMMCFG_PATH, int(s["file_offset_of_the_same_field"], 16),
                      bytes.fromhex(s["old"]), bytes.fromhex(s["new"])) for s in design["sites"]]
    summary = design["summary"]
    require(len(edits) == summary["sites"] and sum(len(e.old) for e in edits) == summary["bytes"]
            and all(len(e.old) == len(e.new) for e in edits), "OG_DESIGN", "Step 1 sites do not match its summary")
    return edits


def check_stock(image: SiteImage, files: Mapping[str, bytes]) -> None:
    """Raise unless every file-backed run's old bytes are in the given input files."""
    cp, avcam, mpr = files["cpapp-b.bin"], files["av-cam.bin"], files["libmpr.so"]
    for preset in image.presets:
        for run in preset.runs:
            n, where = len(run.old), f"{preset.mode.name}/{preset.family.value} {run.space.name} {run.addr:#x}"
            if run.space is Space.CP:
                offset = run.addr - _CP_IMAGE_VA
                require(offset >= 0, "OG_STOCK", f"{where}: below the CP image")
                if offset >= len(cp):
                    continue                                   # CP RAM beyond the loaded image: runtime only
                require(offset + n <= len(cp) and cp[offset:offset + n] == run.old, "OG_STOCK", f"{where}: not stock")
            elif run.space is Space.AVCAM:
                require(avcam[run.addr:run.addr + n] == run.old, "OG_STOCK", f"{where}: not stock")
            elif run.space is Space.MPR:
                require(_mpr_segment_ok(mpr, run.addr, n) and mpr[run.addr:run.addr + n] == run.old,
                        "OG_STOCK", f"{where}: not stock or not at VA == file offset")


def _mpr_segment_ok(elf: bytes, addr: int, length: int) -> bool:
    from elftools.elf.elffile import ELFFile
    for segment in ELFFile(BytesIO(elf)).iter_segments():
        if (segment["p_type"] == "PT_LOAD" and segment["p_offset"] <= addr
                and addr + length <= segment["p_offset"] + segment["p_filesz"]):
            return segment["p_vaddr"] == segment["p_offset"]
    return False
