# Open-gate module (appFw.so)

[Method](../../methods/11-open-gate/README.en.md) · [Layout and pins](layout.json) · [Stored record](stored_record.json)

Freestanding AArch64 C linked at fixed addresses into a new section of v0.1.3's `appFw.so`. Other code calls only
[`og.h`](og.h): `og_power_on()` (shooting-screen hook), `og_select(mode)` and `og_selected()` (the Open Gate
setting, [`og_menu.h`](og_menu.h)).

## Files

| File | Owns |
| --- | --- |
| `og.c` | The selected mode and `/setting/open_gate.dat` (16 bytes `OGT1`, schema 1, mode, ~mode; `.tmp` + fsync + rename) |
| `og_reconcile.c` | The order of work over the site image: check every site, undo, apply, roll back |
| `og_mem.c` | The four memory targets behind `og_mem_read` / `og_mem_write`, and NTSC/PAL detection |
| `og_entry.S` | The two hook trampolines |
| `og_menu.c` | The Open Gate setting's callbacks: getter, setter, commit, and the two "enabled" checks |
| `og_image.h`, `og_sys.h`, `og_util.c` | Site-image accessors; raw Linux system calls; `memset`/`memcpy` for the compiler |
| `og_sites.S` | `.incbin` of the generated `og_sites.bin` ([format](../../a7m4/open_gate_sites.py)); never committed |

`a7m4/open_gate.py` generates `og_config.h` (camera constants from `layout.json` and `stored_record.json`) and the
linker script, links with the pinned Zig, refuses relocations, undefined symbols and any section but `.text`/`.bss`,
and pins every source here by SHA-256 in `layout.json`.

## Layout in appFw.so

| Piece | VA | File offset | Size |
| --- | --- | --- | --- |
| Code, strings, site image (R-X) | `0x9063000` | `0x5e73000` | 46,444 of 0xc000 bytes |
| State (RW, file-backed zeros) | `0x907f000` | `0x5e7f000` | 1,700 of 0x1000 bytes |
| Program headers | 15 -> 17 entries in place at `0x5e80000`; `e_phnum` and `PT_PHDR` sizes updated | | |
| Hooks | `0x1ddab68`, `0x1e56b2c`: `bl` to the aspect wrappers `0x9012bd8` / `0x9012ce4` -> `bl og_hook_first/second` | | |
| Menu hook | `0x1c14e6c`: `b 0x8fb877c` (custom sounds' Volume filter) -> `b og_menu_disabled` | | |
| Menu | the custom sound page's records and two menu groups, and eight relocations changed in place, below | | |

Both sections sit in the file's existing zero gap (`0x5e72500..0x5e80000`); the file keeps its length. Each hook
trampoline calls the aspect feature's wrapper first, unchanged (it runs the native presenter creation and registers
the aspect guide), then `og_power_on` with x0-x18 and NZCV preserved.

## Memory targets

As in the kit's helpers (`libJit_level5_*`, `level16`, `level19_ogfile57r300_v1` and `_m1_v4`, the `lcdcycle_v6` cave):

- **CP**: `dlsym(0, ...)` of `_Z9dmm_allocjPj`, `_Z8dmm_freej`, `sen_dma_transfer`; a 4 KB DMM buffer mapped from
  `/dev/mem` with `O_SYNC`; `sen_dma_transfer(0, cp, phys, n)` reads, `(1, phys, cp, n)` writes, whole 64-byte lines.
  Writes re-read their lines just before the write and read them back after. Reads are served from the last 4 KB
  page read.
- **Stored record**: root `0x12b9cc0` -> `+0x20` -> `+0x684` -> count, mode list, records (348 bytes each); the record
  of drive mode `0x8c0` (NTSC) or `0x880` (PAL). The family is the one whose record holds that family's stock or
  open-gate bytes; exactly one must. The list holds all 30 drive modes, so both records are there; on an NTSC camera
  `0x880`'s record holds `e5` at offset `0xb8` where the kit's PAL stock (read on a camera set to PAL) has `98`, so
  only `0x8c0`'s matches (camera trace, diagnostic build `8ba24f9`).
- **av-cam.bin**: `/dev/mem` (no `O_SYNC`) mapped at physical `0x401381000` (file `0x381000..0x3c0000`); each write is
  read back through the mapping and through `pread` of `/dev/mem`.
- **libmpr.so**: `/proc/self/mem` at the start of its offset-0 mapping in `/proc/self/maps` plus the file offset. Data and
  code sites alike (the encoder steps change instructions there): arm64's `/proc/<pid>/mem` write syncs the
  instruction cache of an executable mapping, which the kit's helpers rely on too.

## Reconcile

`og_reconcile(mode)` first reads every site of the family's presets. Each must hold its stock bytes or the new bytes
of one of the presets at that site (the build splits runs so overlapping sites coincide); otherwise `OG_FOREIGN` and
nothing is written. Then presets are undone last-first (sites already as the target wants them are left), and the
target's runs are applied in the kit's order. A failed read or write puts every touched site back to its starting
bytes (`OG_IO`); if that fails too, `OG_UNDO_FAILED`. When memory already matches, nothing is written. `og_power_on`
does nothing with the dial on movie, and nothing at all while the mode is Off and nothing was applied in this process.

## Menu item

The Open Gate setting is the custom firmware's Custom Shutter Sound page (mid 1592) and its switch (mid 1593), taken
over. Both are registered in every native map by the custom firmware itself, its filters already accept them
(supported and id map `0x8f900a0`/`0x8f900b8` for mids 1592..1597, the apply skip at `0x8f900d0` for the switch's leaf,
option ids 5607..5630 at `0x8fa1000`), and the page's own controller (`0x2d7`) draws it; that is what works on the
camera. `compose_ap` checks every old value below in v0.1.3 before writing (layout.json `menu`).

| Place | VA | v0.1.3 | Composed |
| --- | --- | --- | --- |
| Media group | `0x50fc298` | 4 entries (Format, Rec. Media Settings, Recover Image DB, Display Media Info.) | 5: `{1592, 0, page 0x8fc0400}` |
| Shutter/Silent group | `0x50fd3d8` | 13 entries, the last two the sound pages 1592 and 1595 | 11, the two slots zeroed |
| Page, both copies | `0x8fc0400` (group), `0x8ff8000` (page map) +0x64 | 2 and 3 children (switch, import, volume) | 1: the switch |
| Switch leaf | `0x8fc0490` +0x0c, +0x54 | sound help text, 2 options | none, 3 options |
| Rows (contiguous from leaf +0x58) | `0x8fc0610`, `0x8fc0690`, `0x8fc0710` | On, Off; the listed focus page's header | Off, 4K 3:2, 5.7K 3:2 (ids 5607..5609, values 0..2 = `og_mode`) |
| Option-to-leaf pair | `0x8f91c80` | 5609 -> 1596 (focus switch) | 5609 -> 1593 |

Rows are text only, as native text rows: help = the row's text, no images. The third row is the focus switch's first
row (5609) moved after the other two, with row id `0x1203f0`; the bytes it replaces were only reached through the
Shutter/Silent entry, and the two relocations inside them are moved away.

Relocations, changed in place (`.rela.dyn` keeps its size, `DT_RELASZ` and `DT_RELACOUNT` unchanged):

| r_offset (v0.1.3) | Addend | Now |
| --- | --- | --- |
| `0x50fd500`, `0x50fd518` | the two sound pages, Shutter/Silent entries 11 and 12 | Media entry 4's page field `0x50fc318`, `0x8fc0400` |
| `0x8fc0730`, `0x8fc0788` | inside the third row's bytes | the same (four identical relocations of one field) |
| `0x8fc0508`, `0x8fc0510`, `0x8fc0518` | switch getter, setter, commit | `og_menu_getter`, `og_menu_setter`, `og_menu_commit` |
| `0x8fc0aa8` | option map entry 5609 -> `0x8fc0920` | `0x8fc0710`, the third row |

The leaf's default (+0xa8, `0x8f98430`: return 0) is Off already and stays.

`0x1c14e6c` answers "disabled": its callers open an item only when it returns 0, and otherwise show "This function is
currently disabled" (`0x1ba807c`: nonzero -> `0x1c14efc`, return), as the custom firmware's own filters do for the
items they lock (`0x8f983f0` returns 1 while a sound is off). `og_menu_disabled` (the hook) returns 1 with the dial on
movie and 0 otherwise; every other mid goes to the previous head unchanged, which ends in the stock code. The switch's
profile keeps v0.1.3's stock "return 1" at +0x50. (The first camera build had this the wrong way round: Mode was
refused on stills and opened on movie, where the apply then refused it.)

The native apply method calls the getter (which returns the value the setter staged while browsing) to find the
confirmed row, then the commit with its alias, and ignores the commit's result; a failed `og_select` drops the staged
value, so the setting shows `og_selected()` again. The texts are the page's own ids, rewritten in place in the 68
banks ([method](../../methods/11-open-gate/README.en.md#menu-item)).
