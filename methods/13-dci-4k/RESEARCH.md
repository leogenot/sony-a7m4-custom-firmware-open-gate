# DCI 4K (XAVC S-I DCI 4K, 4096x2160): research (2026-11-25, offline)

**Goal:** XAVC S-I DCI 4K in **MENU > Shooting > Image Quality > File Format**, with Rec Frame Rate 24p / 30p
(NTSC) and 25p (PAL).

**Status:** offline research; no build yet. Every recording layer is present in the A7 IV's v0.1.3 files; what
hides it is the menu's option lookup, built at run time.

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

## Two ways forward

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
