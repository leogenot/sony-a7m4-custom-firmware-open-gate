"""[DEBUG-og2] Read the open-gate diagnostic trace off the camera over USB. Read-only.

    sudo <kit>/macos/.venv/bin/python -I tools/og_read_diag.py --kit <kit> --out <folder>

Uses ArcaneLantern/a7iv-open-gate-test's CT1 USB code (its macos/setup.command must have run once): the camera goes
from Mass Storage to service mode, and the only packets this script lets through are SET_FILE_PATH to the three paths
below and GET_FILE2. Nothing is written to the camera; no library is sent. On a Mac it needs sudo, as the kit does.

Camera: dial on stills, USB Connection Mode: Mass Storage, then "Image Transfer (MSC)" when the camera asks.
"""
import argparse
import os
import struct
import sys
from pathlib import Path

PATHS = (b"../../setting/og_diag.txt", b"../../tmp_wbshd/og_diag.txt", b"../../setting/open_gate.dat",
         b"../../setting/open_gate.try")   # [PROBE-movie] the guard file: there only if a movie apply never returned
SET_FILE_PATH, GET_FILE2 = 0x400F, 0x4011
ST_OK, ST_NO_FILE = 0, 0x10000006


def rules(c):
    """The only two packets let through: SET_FILE_PATH to PATHS, and GET_FILE2."""
    return {(c.PFUNC_ADJUST, 0x1001, SET_FILE_PATH): ("SET_FILE_PATH", lambda p: p[:1] == b"\x00" and p[1:] in PATHS),
            (c.PFUNC_ADJUST, 0x1001, GET_FILE2): ("GET_FILE2", lambda p: p == b"\x00\xff\x00")}


def read_files(session, camera):
    """Each of PATHS: saved into the session folder and printed, or reported missing."""
    for path in PATHS:
        folder, name = path.decode().rsplit("/", 2)[-2:]
        shown = "/" + path.decode().replace("../", "")
        answer = camera._sendAdjustControlPacket(0x1001, SET_FILE_PATH, b"\x00" + path)
        status = struct.unpack_from("<I", answer)[0] if len(answer) >= 4 else None
        if status not in (None, ST_OK):
            session.say(f"{shown}: selecting it gave status {status:#010x}")
            continue
        answer = camera._sendAdjustControlPacket(0x1001, GET_FILE2, b"\x00\xff\x00")
        status = struct.unpack_from("<I", answer)[0] if len(answer) >= 4 else None
        data = answer[4:]
        if status != ST_OK:
            session.say(f"{shown}: " + ("not there" if status == ST_NO_FILE else f"status {status}"))
            continue
        session.save_bytes(f"{folder}_{name}", data)
        session.say(f"{shown}: {len(data)} bytes")
        print(data.decode("latin1") if path.endswith(b".txt") else data.hex(" "), flush=True)


def kit_modules(kit):
    ct1 = Path(kit).resolve() / "windows/tools/camera_tests/CT1_usb_first_contact"
    if not (ct1 / "ct1_stage2_service.py").is_file():
        raise SystemExit(f"{ct1} is not the kit's CT1 folder")
    sys.path.insert(0, str(ct1))
    import ct1_common
    import ct1_stage2_service
    return ct1_common, ct1_stage2_service


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--kit", type=Path, required=True, help="the a7iv-open-gate-test checkout")
    parser.add_argument("--out", type=Path, required=True, help="where to put the session folder (created if needed)")
    args = parser.parse_args()
    c, s2 = kit_modules(args.kit)
    c.use_pmca()
    args.out.mkdir(parents=True, exist_ok=True)
    with c.Session("read", root=args.out.resolve(), test="og_diag") as session:
        inner = s2.connect(session)
        s2.service_session(session, inner, lambda camera: read_files(session, camera), rules(c))
        print("Nothing was written to the camera.")
    if os.environ.get("SUDO_UID"):   # sudo: hand the files back to the user who ran it
        for path in (args.out.resolve(), session.dir, *session.dir.iterdir()):
            os.chown(path, int(os.environ["SUDO_UID"]), int(os.environ["SUDO_GID"]))


if __name__ == "__main__":
    main()
