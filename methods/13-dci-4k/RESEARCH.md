# DCI 4K (XAVC S-I DCI 4K, 4096x2160): research (2026-11-25, offline)

**Goal:** XAVC S-I DCI 4K in **MENU > Shooting > Image Quality > File Format**, with Rec Frame Rate 24p / 30p
(NTSC) and 25p (PAL).

**Status:** camera runs on 2026-11-25. `alpha-dci-0.1`: **DCI not in File Format** (the rows' per-product flag,
below). `alpha-dci-0.2` set it: **DCI listed but greyed out**. `alpha-dci-0.3` also set the DCI Rec Frame Rate
item's flag: **still greyed out**. `alpha-dci-0.4` (2026-11-26): **boot loop**, withdrawn; the camera no longer
starts and needs Fastboot recovery. `alpha-dci-0.5` fixes the cause and is built, but is not released until that
camera is recovered. Every recording layer is present in the A7 IV's v0.1.3 files.

## Present in the firmware

| Layer | Evidence |
| --- | --- |
| Recorder settings (`appFw.so`) | `get_movie_record_setting(file_format, …, xavc_s_i_dci_4k)`, `convert2MovieMainParams` with DCI 24/25/30/50/60/24.00p record settings, `get_movie_rec_frame_rate_xavc_s_i_dci_4k`, encoder profiles `AVC_4096_2160_H422IP@L51` and `@L52`, `reboot_for_dci4k_cross_24_00p` (true 24.00p reboots the camera) |
| File format value | **12**. The switch in `get_movie_record_setting` (`0x2e344f8`, jump table `0x4aef238`) maps 5–10 to its x1–x6 parameters (HS 8K, HS 4K, S 4K, S HD, S-I 4K, S-I HD), 11 to the constant `0x1e`, and 12 to x7 (the DCI record setting). The generated label list (`VAL_…_FILE_FOMRAT_XAVC_SI_DCI_4K` 11th) skips value 11. |
| Menu text | English bank `string_english_horizontal.uxc` holds `0x200a46c` "XAVC S-I DCI 4K", next to the other formats (`0x200a467`–`0x200a46d`) |
| File Format option row | Rows `0x4007570`–`0x4007b70` (0x80 bytes; aliases 130–142; `+0x34` value = the file format). DCI: row `0x4007af0`, alias **141**, text `0x200a46c`, value `0xc`, row id `0x12006d`. A second run (`0x40520f0`–`0x40523f0`, aliases 2430–2436, values 0–6) lists the same formats. |
| Option map | Alias → row relocations `0x50dd3b8`–`0x50dd468` include 141 |
| File Format leaf | `0x594df48`: title `0x200a466`, `+0x54` 13 rows, `+0x58` rows `0x4007570`, getter `0x1c34968`, setter `0x1ca0f58`, commit `0x1cd2f18`, next page `0x1cdeaf0` (`0x2ed` / `0x425`), `+0xa8` `0x1c83604` |
| Coprocessor (`cpapp-b.bin`) | Size-class table `0xbc5d6c` (CP VA): 1920x1080 = 5, 3840x2160 = 8, **4096x2160 = 10**, 7680x4320 = 11 (shared with other bodies) |
| Encoder (`av-cam.bin`) | 60 u16 and 35 u32 occurrences of 4096x2160 (the multi-model descriptor table) |
| MP4 writer (`libmpr.so`) | 4096x2160 size rows at `0x2d33dc`, `0x2e91d4` |

The generated availability rules allow DCI for system architecture IPSX, which the A7 IV is
([feature inventory](https://github.com/leogenot/sony-a7iv-feature-research/blob/main/docs/FEATURE_INVENTORY.md)).

## What hides it

**The rows' per-product flag** (found after the `alpha-dci-0.1` camera run). The leaf lists all 13 rows, but the
row-list builder `0x1c0e52c` (vtable slot `0x2e0`) keeps a row only if the per-row check `0x1c0f12c` (slot `0x1a8`)
passes, and that check runs three tests:

1. `0x1c20420(row)`: the halfword at row `+0x44 + 2 x index` must be 1. The index comes from `0x1c16460` →
   `0x1c0bff4`, which maps the product's `menu_item` (`0x183ee0c`) minus 1 through a 26-byte table and falls back to
   19. The A7 IV is `LS`, feature index 26, `menu_item` 0, so **index 19, `+0x6a`**. At index 19, File Format's flags
   give exactly the A7 IV's list: XAVC HS 4K, XAVC S 4K, XAVC S HD, XAVC S-I 4K, XAVC S-I HD (and "-", hidden by
   test 2). **DCI (141) is 0, and so are all ten DCI Rec Frame Rate rows (200–209).**
2. The function the table at `0x58f9db0` holds for the row's `+0x30` (entry 0 = always). DCI's is `0x15` →
   `0x1c35f0c`: setting `0x2530` (getter `0x2922784`) must be nonzero. The DCI frame rates have their own entries
   (`0x50`–`0x58`), the usual NTSC/PAL rules.
3. A virtual check per alias (slot `0x700`).

The option check `0x1c08bc0` (slot `0x210`; it takes an option alias, not an item id) runs test 1 too.

**Items have the same flag**, in their leaf: `0x1c2006c` reads the halfword at leaf `+0x2c + 2 x index` and wants 1
(`+0x52` on the A7 IV); the item check `0x1c0f3ac` (slot `0x208`) reaches it. It is on for File Format's leaf and
off for DCI's Rec Frame Rate leaf (item 42, `0x597ba50`, flags `01000010000000000000`, the same pattern as the DCI
row). With item 42 hidden, DCI has no frame rate to record with, which is the likely reason `alpha-dci-0.2` showed
DCI greyed out. Running
`0x1c20420` in an emulator on the v0.1.3 rows, with the A7 IV's feature index, gives the same visible set as the
camera, and DCI hidden.

**The lookups** (what `alpha-dci-0.1` changed) come after that, and weren't enough on their own. Option ids go through
`0x1c18004`, which Erlla's custom firmware already hooks (`B 0x8fa1000`: its own ids 5607–5630 return themselves). The
stock path calls `0x1c11044`, a lookup in a run-time map (a red-black tree, key at `+0x20`, candidate at `+0x24`) that,
for each candidate of the id, calls a virtual check (`vtable +0x210`) and returns the first that passes, else `0x15d8`.
Which candidates id 141 has, and why they fail on the A7 IV, is decided by tables registered at start-up and isn't
readable from the files.

## The Rec Frame Rate items

Movie Settings' Rec Frame Rate is seven items, one per format family, all listed on that page; the camera shows
the one for the current file format (Sony's rule
`setting_menu_shooting_mode_setting_movie_rec_frame_rate_xavc_s_i_dci_4k_setting_file_format`). Item map entries
`0x50d7190`.. hold item 34 (File Format) → leaf `0x594df48` and items 36–42 → the seven Rec Frame Rate leaves.
**Item 42** is DCI's: leaf `0x597ba50`, options 200–209 = 60p, 50p, 30p, 25p, 24p, 59.94p, 29.97p, 23.98p, 24.00p,
"-" (values 0, 1, 2, 3, 4, 0, 2, 4, 8, 7).

## Menu lookups and Erlla's chains

Erlla's custom firmware redirects three stock lookups to make its own pages and options appear, each answering for
its own ids and passing everything else on:

| Stock function | What it does | Erlla's answer for its ids |
| --- | --- | --- |
| `0x1c08bc0(object, item)` | "is this menu item supported?" (called through a vtable) | 1 for items 1592–1599 |
| `0x1c1116c(…, item)` | maps an item id through a start-up table (a leaf, writes x0–x4 only) | the id itself |
| `0x1c18004(option)` | resolves an option id (`0x1c11044`: per-candidate virtual check, else `0x15d8`) | the id itself, for 5607–5630 |

## The test builds

### `alpha-dci-0.1` (camera: DCI not shown)

Beta 0.1 plus three hooks that go before Erlla's chains at those sites and answer for DCI the same way: item 42 is
supported and maps to itself; options 141 (File Format DCI) and 202, 203, 204, 206, 207 (DCI 30p, 25p, 24p and the
29.97p, 23.98p labels) resolve to themselves. 60p, 50p, 59.94p and 24.00p are left to Sony's answer, so they stay
hidden; which of the rest NTSC or PAL shows is Sony's own rule. The item map hook is plain assembly that writes only
x0 and the flags, since its caller may keep values in scratch registers (GCC 6.4, `-fipa-ra`).

The step log (`/setting/og_diag.txt`) records once per id what Sony's chain answered and what the module returned:
items 34 and 39–42, options 139–141 and 182–209. If DCI doesn't appear, it shows what a visible option (139, S-I 4K;
182–190, its frame rates) resolves to, which is what the next build would return for DCI.

### `alpha-dci-0.2`

`alpha-dci-0.1` plus a row-edit step in the build (`a7m4/open_gate.py` `_edit_rows`, `layout.json` `row_edits`):

- the A7 IV flag (`+0x6a`) set to 1 for File Format's DCI row (141) and the DCI 30p, 25p, 24p, 29.97p and 23.98p
  rows (202, 203, 204, 206, 207); 60p, 50p, 59.94p, 24.00p stay hidden;
- the DCI row's `+0x30` set from `0x15` to 0 (always available), so setting `0x2530` doesn't hide it. Its real value
  is logged: `dci.option 141 <Sony's answer> 141 <setting 0x2530>`. If it reads 0 and DCI then fails to record, that
  setting is what Sony checks for.

The emulator run on the composed `appFw.so` shows 141 and the five frame rates visible, every other row unchanged.
On the camera: DCI listed in File Format but greyed out.

### `alpha-dci-0.3`

`alpha-dci-0.2` plus the A7 IV flag in the DCI Rec Frame Rate leaf (`+0x52` set to 1). The edit step now takes leaves
(named by title) as well as rows (`layout.json` `menu_edits`, `_edit_menu_records`). It also drops the `0x1c08bc0`
hook: that check takes an option alias, so its answer for "item 42" was about option 42 (Lossless Comp (L), already
shown), and its `dci.item` lines were mislabelled. The emulator on the composed `appFw.so` passes the item check
for item 42 and the row check for DCI and its five frame rates, and changes nothing else.

On the camera: still greyed out. The log:

```text
dci.option 8b 8b 8b 2    # 139 (S-I 4K): Sony resolves it to itself; setting 0x2530 = 2
dci.option 8c 8c 8c 2    # 140 (S-I HD)
dci.option 8d 8d 8d 2    # 141 (DCI): Sony resolves it to itself too
```

So the lookup hook isn't needed, and setting `0x2530` (2, nonzero) never hid DCI. No Rec Frame Rate option was
resolved: the menu never reached DCI's frame rates.

### `alpha-dci-0.4`

The menu object answers per-option questions through its vtable (base `0x58f7a68`, a second copy at `0x58f8618`;
`0x59ea9a8` holds base − 0x10, from which the static helpers at `0x1c14bfc`.. build their objects):

| Slot | Function | What it checks |
| --- | --- | --- |
| `0x1a8` | `0x1c0f12c` | shown: row flag == 1, the `+0x30` availability, slot `0x700` (the row builder asks it per row) |
| `0x1b0` | `0x1c0f1c0` | resolved row flag != 0, then slot `0x6f8` |
| `0x1f0` | `0x1c08c18` | resolved row flag != 0 |
| `0x1f8` | `0x1c08d74` | a `+0xe` flag in a record from another table (id 141 there is Camera Set Memory 1) |
| `0x200` | `0x1c08da8` | the same record, flag != 0 |
| `0x210` | `0x1c08bc0` | row flag == 1 (Erlla chains its first instruction) |
| `0x6f8` | `0x1c07aa4` | slot `0x700` on the slot-`0x750` mapping of the alias |
| `0x700` | `0x1c0ca38` | row bytes `+0x1c`/`+0x1d` and the item leaf's `+0x28`/`+0x29` through slot `0x520` |

`alpha-dci-0.4` points both vtables' entries for `0x1a8`, `0x1b0`, `0x1f0`, `0x210`, `0x6f8` and `0x700` at
`og_dci.c` wrappers (`layout.json` `vtable_hooks`, `_hook_vtables`): yes for 141, Sony's answer for every other id.
The first time the row builder asks about 139 and 141, the log gets `dci.checks <alias> <answers> <vtable>`.

### `alpha-dci-0.4` boot-looped the camera (2026-11-26)

The screen lit up and the camera restarted, over and over, then stayed dark; nothing appeared on USB during the
loop. 0.4's probe called the eight Sony functions above through a `static const` table of their addresses. The
module is linked at its `appFw.so` address and nothing relocates it, but `appFw.so` is `ET_DYN` and loads at a
nonzero base, so each entry pointed at an unmapped address (the built module held `0x1c0f12c`, … as is). The row
builder asks about 139 and 141 during startup (the menu registrars run at every boot), so the first probe call
crashed the app at every boot. The host test can't see it (addresses are right on the host), and the build's
"needs relocations" check passed, because a `-no-pie` link resolves such pointers without a relocation.

- **Build check:** the build compiles each source on its own first and refuses any absolute relocation into a
  loaded section, naming each place (`_stored_addresses`). It refuses 0.4's sources with the eight table entries
  and passes 0.3, `alpha-angle-0.4` and `main`.
- **Recovery:** 0.4 and beta-0.2 differ only in `nflasha15`, so writing beta-0.2's `nflasha15` over Fastboot (Multi
  port + RP2040 boot bridge) returns the camera to beta-0.2. The SD recovery tool's updater allowlist matches
  neither v6.02's nor v0.1.3's updater initrd.

### `alpha-dci-0.5`

0.4's test, with the probe calling each function directly, like every other call into `appFw.so`. It no longer
probes `0x1f8` and `0x200`: they look the id up in another table (141 is Camera Set Memory 1 there), and the probe
runs at boot, so it makes no call it doesn't need. Bits 0–5 of `<answers>` are Sony's answers from `0x1a8`,
`0x1b0`, `0x1f0`, `0x210`, `0x6f8` and `0x700`. If 139 and 141 differ in a bit, that check is what greys DCI out.
If they don't, the greying is decided outside this object, and the next suspects are Sony's run-time availability
rules for File Format, such as `PRM_HAITA_setting_file_format_system_architecture` (it names DCI with the A7 IV's
`IPSX` architecture) and `PRM_license_type_dci4k24p_available`.

Built, audit passed, module 48,348 bytes, no stored address. **Install it only with the Fastboot cable working**:
it still runs new code in the menu at every boot.

### Camera test (`alpha-dci-0.5`)

1. In movie mode, open **MENU > Shooting > Image Quality > File Format**: is **XAVC S-I DCI 4K** listed?
2. Choose it. In **Movie Settings > Rec Frame Rate**: 24p and 30p on NTSC, 25p on PAL?
3. Record 10 s at each frame rate: a 4096x2160 XAVC S-I file that plays?
4. Choose XAVC S-I 4K again: back to normal.

Keep open gate **Off** while DCI is selected: its sensor changes are made for 4K and 5.7K, not DCI.

**If the camera freezes or won't start its live view on movie:** the file format is saved in the camera, so a
format its pipeline can't run would come back at every start on movie. Pull the battery, start with the switch on
**stills** (the movie pipeline doesn't run there), and use **MENU > Setup > Reset/Save Settings > Camera Settings
Reset**, which is expected to put the movie File Format back to its default (not checked on this body). Installing
Beta 0.1 again doesn't change saved settings, so reset first.

## Two ways forward (before the build)

1. **Recording first, through Sony's own path.** A probe build calls the File Format leaf's own commit
   (`0x1cd2f18`) with alias 141 from a test row on our Open Gate page, and logs what happens. If the camera then
   records 4096x2160, the whole recording chain works on this body, and the menu row is the remaining work.
2. **Menu first.** Hook `0x1c18004` (the chain Erlla already uses) so alias 141 resolves, find the value the menu
   expects for a visible stock option (by logging what the lookup returns for 139, S-I 4K, on the camera), and do
   the same for the DCI Rec Frame Rate item.

Recommended: 1, then 2: it finds out whether DCI records at all before the menu work.

## Not in scope

- DCI 50/60p: the 7K readout is too slow for 60p (measured FF 4K readout 26.7–27.4 ms).
- True 24.00p: Sony's own code reboots the camera to switch.
- DCI in XAVC HS or S: Sony's DCI is S-I (All-Intra) only.
