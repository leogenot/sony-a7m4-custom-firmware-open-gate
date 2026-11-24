# Open gate: 3:2 full-sensor video, applied at camera start

[Module](../../native/open-gate/README.md) · [Inputs and layout](../../native/open-gate/layout.json) · [Stored record](../../native/open-gate/stored_record.json)

Builds ArcaneLantern/a7iv-open-gate-test's open-gate state into custom firmware v0.1.3, so it no longer needs a USB
session and a cold boot each time. A native module in `appFw.so` brings camera memory to the mode saved in
`/setting/open_gate.dat` whenever the shooting screen is created, including after a short power-off. The modes are
the kit's two states, NTSC and PAL:

| Mode | File | Kit steps |
| --- | --- | --- |
| 4K 3:2 | 3840x2560, XAVC HS 4K / XAVC S 4K | `ogsq_v3` (PAL `ogsq880_v1`), `outsz2560_v1`, `ogfile2560_hs_s_v2`, `lcdfill_v2` |
| 5.7K 3:2 | 5760x3840, XAVC HS 4K / XAVC S 4K, 300 Mbps on the 100M settings, I and P pictures only | `og57n_v1` (PAL `og57n880_v1`), `og57cp_v3`, `ogfile57r300m1_v4`, `lcdfill_v2` |

Both encoder steps also set the MP4 headers (track header, sample description, Sony's profile box) to the file's real
size, so editors open it at 3:2 without a remux; Sony's XML in the file still says 3840x2160. Those are libmpr.so
instructions and table entries, changed in memory like the rest. 5.7K is coded without B pictures, which removed the
kit's duplicate frames, and an HEVC recording's edit list starts at frame 0.

Step 1 (`dmmwb4688_v1`, the taller raw write buffer) is written into `nflasha8/DmmCfg.bin` at build time, so it is
always on and the kit's cold-boot rule goes away. The C3 de-squeeze cycle is not included. Off puts every site back
to stock without a power cycle. The camera's NTSC/PAL setting is detected on the camera from its stored drive-parameter
record; a camera matching neither is left untouched.

## Menu item

**MENU > Shooting > Media > Open Gate > Mode**, with three options: **Off**, **4K 3:2** and **5.7K 3:2**.

- **What it is:** the custom firmware's Custom Shutter Sound page, taken over. Its page, controller and ids are the
  custom firmware's own and already work on the camera, so the setting behaves like the custom sound settings did. The
  page is renamed Open Gate, listed on Media after Display Media Info., and holds only the Mode setting. Both custom
  sound pages leave Shooting > Shutter/Silent; the custom sound code is still in the firmware, but no menu reaches it.
- **Applies on confirm**, through the same reconcile as at start: every site is checked, the chosen preset is
  written and read back, and Off puts every site back to stock without a power cycle. The choice is saved in
  `/setting/open_gate.dat` and re-applied at the next shooting screen.
- **Stills only:** with the dial on movie, Mode shows "This function is currently disabled". Choose the mode on
  stills, then turn the dial and record.
- **On failure** (another apply running, coprocessor not ready, foreign bytes, an I/O error, or the record could not
  be saved) nothing is shown: the native apply method ignores the commit's result (`appFw.so` 0x1c15620), so there
  is no error path to use. Mode keeps showing what is really in place (`og_selected()`). If the bytes were applied
  but the record could not be saved, Mode shows the new mode, and the next power-on goes back to the saved one.

The texts are the sound page's own, rewritten in place in all 68 horizontal and vertical string banks, in English in
every language (Off is Sony's own, translated, string):

| Id | Was | Now |
| --- | --- | --- |
| `0x0200ab5c` | Custom Shutter Sound | Open Gate |
| `0x0200ab5f` | On/Off | Mode |
| `0x0200ab61` | Use this audio for electronic shutter? | 4K 3:2 |
| `0x0200ab62` | Use this audio for focus? | 5.7K 3:2 |

No Sony string changes. The module's [README](../../native/open-gate/README.md) has the records, hooks and
relocations.

## Build

Inputs, all checked against the SHA-256 values in [layout.json](../../native/open-gate/layout.json):

- `BODYDATA.DAT` of A7M4-ToolKit v0.1.3 (custom firmware), not the official 6.02 package;
- the kit's `windows/tools/modding/designs` directory;
- Zig 0.15.2 (official build; the `zig` binary's hash is pinned);
- the published signing keys in `keys/published/` (v0.1.3's updater trusts them).

From the repository root, with Python 3.11+ and `pip install -e ".[firmware,test]"`:

```sh
python -m a7m4.open_gate build --base BODYDATA-custom-v0.1.3.DAT \
    --designs <kit>/windows/tools/modding/designs --zig <zig-0.15.2>/zig \
    --private-key keys/published/A7M4_updater_private.pem \
    --public-key keys/published/A7M4_updater_public.pem --output out/open-gate
```

The output directory must not exist. It gets `BODYDATA.DAT`, `verification.json` (every input and output hash, the
changed members and byte ranges, the module's symbols, the signature check and the package audit) and `work/`.
Only `nflasha8` (`DmmCfg.bin`, 16 bytes), `nflasha15` (`appFw.so`: the module in its zero gap, two program headers,
three hook instructions, the Media and Shutter/Silent groups, the sound page's records and eight of its relocations,
changed in place; and the 68 string banks, same sizes) and `part_image.sum` change. `av-cam.bin`, `libmpr.so`
and `cpapp-b.bin` stay byte-identical: their open-gate changes are made in memory at run time.

Offline checks: `python -m pytest tests/test_open_gate.py`: the site image against the designs and v0.1.3's files;
`og_reconcile.c` and `og.c` on the host against fake memory, and `og_mem.c` against fake DMA, `/dev/mem` and
`/proc/self/maps`; `og_menu.c` against a fake `og.h` and dial; the sound page's records in v0.1.3; the composed `appFw.so` (linked with
the pinned Zig) and all 68 edited banks, decoded. Set `OG_DESIGNS`, `OG_V013_DEV`
(an unpacked v0.1.3 `0700_part_image/dev` tree), `OG_602_DEV` (the same for stock 6.02) and `OG_ZIG` if they are
not at the workspace defaults.

## Limits

- Not yet run on a camera. The memory writes reproduce the kit's own methods and constants, which work on the
  camera, but the module, its start-up trigger and its timing have only been tested offline.
- The planned camera tests of this build are NTSC only. The PAL presets are the kit's PAL designs (`ogsq880_v1`,
  `og57n880_v1`), which the kit ran on a camera set to PAL; this build has not been run on one.
- Applied with the dial on stills only, as the kit does; with the dial on movie the next shooting screen retries,
  and Mode says it is currently disabled.
- This menu has not been run on a camera either (check C8). An earlier build listed a new item of its own directly on
  Media; on the camera it showed no title and could not be selected, because its availability callbacks were inverted
  and nothing in the custom firmware uses that shape. This build reuses the custom sound page instead, which works on
  the camera, with the changes listed in the module README.
- The custom sound settings are gone from the menu, but a custom sound switched on with v0.1.3 stays on (its choice is
  saved in the camera). Switch both custom sounds off in v0.1.3 before installing this build.
- While a mode is on, record only in the formats and frame rates the kit lists (XAVC HS 4K / XAVC S 4K at 24p NTSC,
  XAVC S 4K at 25p PAL). The menu still shows 100M for 5.7K: the card fills about three times faster.
- Set Proxy Recording to Off, as the kit asks. The camera does not play 5.7K files back.

## Later work

- **1080p open gate (1920x1280):** offline research, what it would take and the camera check that decides it:
  [HD-RESEARCH.md](HD-RESEARCH.md).

## Credits and licenses

- Open-gate designs, methods and constants: [ArcaneLantern/a7iv-open-gate-test](https://github.com/ArcaneLantern/a7iv-open-gate-test), GPL-3.0.
- Custom firmware, build tooling and the aspect-ratio hooks this module chains onto:
  [Erlla/sony-a7m4-custom-firmware](https://github.com/Erlla/sony-a7m4-custom-firmware), GPL-3.0.
