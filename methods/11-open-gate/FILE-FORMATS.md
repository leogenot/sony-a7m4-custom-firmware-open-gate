# Open gate as File Format entries (2026-11-25)

**Goal:** choose open gate in **MENU > Shooting > Image Quality > File Format**, like any other format.

**Status:** test build `alpha-formats-0.1`, not yet run on a camera.

## How it works

| Piece | What |
| --- | --- |
| Rows | File Format (leaf `0x594df48`, 13 rows from `0x4007570`) gets two entries: its hidden legacy rows 130 and 131 (`0x4007570`, `0x40075f0`), each a copy of the XAVC HS 4K row (`0x4007870`) with its own alias, text (help too) and value (`0x40`, `0x41`, unique in the item); the row ids stay. |
| Texts | "4K 3:2 Open Gate" on `0x200ab5d` and "5.7K 3:2 Open Gate" on `0x200ab63`, written in place in all 68 banks. Both were custom-sound texts (the focus page's title and its restore prompt) that no menu of this build reaches, and both have room in every bank (20 and 32 bytes); no Sony text has room for 17 bytes in every bank. |
| What a row means | A File Format option stores a real Sony format, so the rows stand for the camera's 4K format with open gate on: XAVC HS 4K 24p on NTSC, XAVC S 4K 25p on PAL. |
| Getter (`+0x78`) | One of the rows while open gate is on and the format is HS or S 4K; else Sony's value. |
| Setter (`+0x80`) | Browsing a row passes Sony the matching 4K format (6 or 7). |
| Commit (`+0x88`) | A row: apply open gate (on movie, behind the crash guard), then Sony's File Format commit with option 136 (HS 4K) or 137 (S 4K), then the shared Rec Frame Rate commit (`0x1cd7190`) with 151 (HS 4K 24p) or 161 (S 4K 25p). Any other option: Sony's commit, then open gate Off. |
| Option lookup (`0x1c18004`) | Rows 130 and 131 resolve to themselves (before Erlla's chain); while open gate is on, HS 4K's rates other than 24p / 23.98p and S 4K's other than 25p resolve to "unknown" (`0x15d8`). |

Sony's commits take an option id: `0x1c18368` maps it to its value and the format is applied through event
`0x3e6d` (File Format) or `0x264e0b4` (Rec Frame Rate).

## Why it needs the movie-mode apply

File Format is a movie-mode menu, so choosing a row applies open gate with the switch on movie, which Beta refuses.
This build carries `alpha-movie`'s movie-mode apply and crash guard (`/setting/open_gate.try`), without its trace.
The format change also makes the camera rebuild its movie pipeline right after the apply, which should be the safest
moment for it; the camera test shows whether it is.

## Rows' flags

Rows carry ten u32s at `+0x40`–`+0x64` that read as pairs of 16-bit 0/1 flags: all set on the S 4K row, zero on the
legacy rows, and different on the DCI and 8K rows from the rows the A7 IV shows. They may be per-product
availability. The reused rows take the HS 4K row's, so they read as available wherever HS 4K is.

## Camera test

1. On movie, **File Format**: are "4K 3:2 Open Gate" and "5.7K 3:2 Open Gate" listed?
2. Choose "4K 3:2 Open Gate": live view keeps running; File Format shows it; **Rec Frame Rate** offers only 24p (25p
   on PAL); the Open Gate page shows 4K 3:2. Record 10 s: a 3840x2560 file.
3. Choose "5.7K 3:2 Open Gate": as 2, a 5760x3840 file.
4. Choose XAVC S-I 4K: open gate Off, all frame rates back, a normal 16:9 file.
5. Power off and on with a 3:2 format chosen: still chosen, still applied.

If the camera freezes, pull the battery: the crash guard starts the camera with open gate Off.
