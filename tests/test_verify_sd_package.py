"""tools/verify_sd_package.py on the published v0.1.3 package, whose backup manifest also lists MainDest.txt.

Needs the v0.1.3 BODYDATA.DAT (not in the repo): set A7M4_V013_DAT, or keep the workspace layout (../firmware/).
"""
import importlib.util
import os
from pathlib import Path

import pytest

from a7m4.firmware import decode

ROOT = Path(__file__).resolve().parents[1]
DAT = Path(os.environ.get("A7M4_V013_DAT", ROOT.parent / "firmware/BODYDATA-custom-v0.1.3.DAT"))


def tool():
    spec = importlib.util.spec_from_file_location("verify_sd_package", ROOT / "tools/verify_sd_package.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.mark.skipif(not DAT.is_file(), reason="no v0.1.3 BODYDATA.DAT")
def test_v013_package_passes_with_its_extra_backup_row(tmp_path):
    plain = tmp_path / "plain.bin"
    decode(DAT, output=plain)
    public = (ROOT / "keys/published/A7M4_updater_public.pem").read_bytes()
    report = tool().verify(DAT, plain, public, b"WBI4")
    rows = report["backup_templates_crc_verified"]
    assert len(rows) == 33 and sum(name.endswith(".bin") for name in rows) == 32
    assert "0110_backup/MainDest.txt" in rows
