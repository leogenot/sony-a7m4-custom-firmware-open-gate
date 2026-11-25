# DCI 4K (XAVC S-I DCI 4K, 4096x2160): research (2026-11-25, offline)

**Goal:** XAVC S-I DCI 4K in **MENU > Shooting > Image Quality > File Format**, with Rec Frame Rate 24p / 30p
(NTSC) and 25p (PAL).

**Status:** test build `alpha-dci-0.1`, not yet run on a camera. Every recording layer is present in the A7 IV's
v0.1.3 files; what hides it is the menu's lookups, built at run time.

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

Not the rows and not the leaf: the leaf lists all 13 rows. Option ids go through `0x1c18004`, which Erlla's custom
firmware already hooks (`B 0x8fa1000`: its own ids 5607–5630 return themselves). The stock path calls `0x1c11044`, a
lookup in a run-time map (a red-black tree, key at `+0x20`, candidate at `+0x24`) that, for each candidate of the
id, calls a virtual check (`vtable +0x210`) and returns the first that passes, else `0x15d8`. Which candidates id 141
has, and why they fail on the A7 IV, is decided by tables registered at start-up and isn't readable from the files.

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

## The test build: `alpha-dci-0.1`

Beta 0.1 plus three hooks that go before Erlla's chains at those sites and answer for DCI the same way: item 42 is
supported and maps to itself; options 141 (File Format DCI) and 202, 203, 204, 206, 207 (DCI 30p, 25p, 24p and the
29.97p, 23.98p labels) resolve to themselves. 60p, 50p, 59.94p and 24.00p are left to Sony's answer, so they stay
hidden; which of the rest NTSC or PAL shows is Sony's own rule. The item map hook is plain assembly that writes only
x0 and the flags, since its caller may keep values in scratch registers (GCC 6.4, `-fipa-ra`).

The step log (`/setting/og_diag.txt`) records once per id what Sony's chain answered and what the module returned:
items 34 and 39–42, options 139–141 and 182–209. If DCI doesn't appear, it shows what a visible option (139, S-I 4K;
182–190, its frame rates) resolves to, which is what the next build would return for DCI.

### Camera test

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
