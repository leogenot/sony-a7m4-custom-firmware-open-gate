# Shutter angle on the dial: research and test build (2026-11-25)

**Goal:** set the movie shutter as an angle (180°, 90°, …) with the camera's dial, like the FX3, on the A7 IV.

**Status:** offline research done. Test builds `alpha-angle-0.1` (dial only) and `alpha-angle-0.2` (dial plus a
Speed / Angle menu item) not yet run on a camera.

## What was already known

From [sony-a7iv-feature-research](https://github.com/leogenot/sony-a7iv-feature-research), on stock 6.02
([`SHUTTER_ANGLE.md`](https://github.com/leogenot/sony-a7iv-feature-research/blob/main/docs/SHUTTER_ANGLE.md),
[`SHUTTER_MENU_GATE.md`](https://github.com/leogenot/sony-a7iv-feature-research/blob/main/docs/SHUTTER_MENU_GATE.md)):

- Backup property `0x02cf1702` = `01` (service mode `bk w`, cold boot) switches the shutter display to an angle,
  and `0x02cf1704` holds the angle (`10` = 180°). The angle icon then shows in movie mode.
- The dial doesn't change the angle in M, or in Flexible Exposure with Tv on Manual. Touching the value opens the
  shutter-speed editor.
- The A7 IV has no **Shutter Mode** menu item: its menu catalogue is per product class (A7 IV `LS`, FX3
  `ALCIN_UUD`). The angle code, the angle labels and the help text are all in the A7 IV firmware.
- The dial path checks a generated availability ("HAITA") byte before the angle drive.

## Checked against our base

Every function on the dial path is byte-identical in Erlla's v0.1.3 `appFw.so` and stock 6.02 (`1e1b9202…`, the
build the research used): the dial dispatcher `0x1baf328`, the shutter-mode getter `0x28f6e38`, the availability
getter `0x293d634`, the angle drive `0x26e8b08`, the "unavailable" answer `0x26e898c`, the speed drive `0x27b4fa4`
and the angle step `0x2593d20`. Segment 0 differs in 4,156 bytes elsewhere (Erlla's own patches).

## The dial path

The dispatcher, as v0.1.3 holds it:

```text
0x1baf358  mode = *0x28f6e38()            0 = speed, 1 = angle   (u32 at 0x7fee1d0)
0x1baf360  mode != 0                    -> 0x1baf3d4
           speed: 0x293d620 byte == 0   -> speed drive 0x27b4fa4
0x1baf3d4  mode == 1 ?                  no  -> 0x1baf414: "unavailable" 0x26e898c
0x1baf3e4  byte = *0x293d634()
0x1baf3ec  byte != 0                    -> 0x1baf414: "unavailable" 0x26e898c
0x1baf3f0  step = direction x clicks
0x1baf40c  angle drive 0x26e8b08(context, step)
```

`0x293d634` is one of 4,096 generated getters, each `adrp; add; add; add; ret` returning one byte of a table at
`0x84c2d20`. It returns entry `0x281` (`0x84c2fa1`); the research names it
`PRM_HAITA_setting_shutter_speed_a_m_switching_exposure_mode`. Its neighbour `0x293d620` (entry `0x280`) guards the
speed drive.

- **14 BLs** call `0x293d634`, and all of them only read the byte (`ldrb`, then a branch or a copy into another object).
  One of them is inside the angle drive itself (`0x26e8bc0`), so the drive checks again.
- **No code writes it through the getter or the table base by register**; the writer is somewhere else (not found).
- **9 BLs** call the angle drive, including the dispatcher's and four at `0x23648c8`–`0x2365cac`. Those four are the
  "direct call sites" the research found; they read the same byte first.
- Nothing else branches to the getter or the drive, and no pointer to either is stored (checked word by word over
  every executable segment).

So the dial refuses because that byte is set. Why it's set on the A7 IV (an exposure-mode condition, or the product
class), and whether it's the only guard, can't be settled offline.

## The test build

`alpha-angle-0.1`, on the `alpha-angle` branch: Beta 0.1 plus:

- **The gate:** the 14 calls to `0x293d634` go to `og_angle_gate`. In angle mode with the switch on movie it reads
  `0` (available); everywhere else, including speed mode, it reads the real byte. Nothing is written to the table.
- **A safe entry:** the getter is a leaf, and `appFw.so` is built by GCC 6.4 (`.comment`), whose `-fipa-ra` lets a
  caller keep values in scratch registers across a call to a leaf it knows. None of the 14 call sites does, but the
  entry (`og_angle.S`) keeps x1–x18 and the flags anyway.
- **The trace** in `/setting/og_diag.txt` (the previous run kept as `og_diag.prev.txt`):
  - `angle.gate <real byte> <shutter mode> <dial> <let through>` whenever any of them changes;
  - `angle.drive <step> <call site>` for every drive call;
  - `angle.refused <call site>` when the dispatcher still answers "unavailable".
- **No menu switch.** Angle mode is switched on with the service-mode command.

The override only lasts while the camera runs; nothing is saved. To go back: `bk w 0x02cf1702 00` (or install Beta).

## The Shutter Mode menu item (`alpha-angle-0.2`)

The A7 IV already has the FX3's **Shutter Mode** item, complete, and lists it; only its menu lookups leave it out:

| Piece | In v0.1.3 |
| --- | --- |
| Menu group | `0x50fedc8`: id `0x10`, title `0x0200603f` "Exposure", 17 items; the first is item 441 |
| Item map | `0x50d8890`: item 441 → leaf `0x590ab98` |
| Leaf | `0x590ab98`: title `0x2008e2f` "Shutter Mode", help `0x2008e33` "Sets the shutter speed when shooting movies.", setting `0x490c` at `+0x68`, 2 rows at `0x3fdd7f0` |
| Rows | 1543 "Speed" (value 0) and 1544 "Angle" (value 1), each with its own help text; both in the option map (`0x50e2c00`) |
| Getter `+0x78` | `0x1c6c0cc`: reads the shutter mode (`0x28f6e38`) under its lock |
| Setter `+0x80` | `0x1cabae8`: sets the mode, writes its backup value and notifies (`0x256c714(0x65b)`) |
| Commit `+0x88` | `0x1cd40d8`: alias → value (`0x1c18368`), then `0x25d3f8c`, which runs setting `0x490c`'s change through `0x25d3ecc`, the same set, save and notify |

All texts are in the English function bank (`string_english_func_j.uxc`), so nothing is renamed. The live setter
the dial build was missing is Sony's own commit: choosing a row changes the mode at once, with no cold boot.

The A7 IV leaves the item out the same way it leaves out DCI ([DCI research](https://github.com/leogenot/sony-a7m4-custom-firmware-open-gate/blob/alpha-dci/methods/13-dci-4k/RESEARCH.md)): the item
check `0x1c08bc0`, the item map `0x1c1116c` and the option resolve `0x1c18004` don't know it. `alpha-angle-0.2` adds
three hooks that go before Erlla's chains at those sites (as `alpha-dci-0.1` does for DCI): item 441 is supported,
maps to itself, and options 1543 and 1544 resolve to themselves. Everything else is Sony's code. The trace adds
`angle.item 441 <Sony's answer> 1` and `angle.option <option> <Sony's answer> <option>`, once each.

Where it should show: **MENU → Exposure/Color → Exposure → Shutter Mode** (first item), as on the FX3. Sony
documents it on the FX3 for movie and S&Q; whether the A7 IV greys it out on stills is for the camera run.

## Risks

- **The A7 IV may disable the angle drive for a reason.** The FX3 runs the same code, but an A7 IV-specific state
  could be missing, so the drive could misbehave. Most likely outcome of a problem: the value doesn't change, or
  changes without the exposure following.
- **Entry `0x281` is also copied elsewhere** (into other objects' state at `0x1eef168`, `0x1f6281c`, `0x1f6b724`,
  `0x1fffa94`, `0x2466530`; not identified). In angle mode on movie, those see "available" too, which may change
  what the screen shows as adjustable.
- **Auto shutter modes:** in P or A, the drive may change the stored angle while auto exposure decides the shutter.
  Test in M first.

## Camera tests

On `alpha-angle-0.2`, switch angle mode on from the menu (M1). On `alpha-angle-0.1`, use service mode, as in
`SHUTTER_ANGLE.md`: `bk w 0x02cf1702 01`, `bk s`, cold boot, start on movie.

| Test (0.2 only) | Do this | Check |
| --- | --- | --- |
| M1 | Movie: MENU → Exposure/Color → Exposure → Shutter Mode → **Angle** | The item is there; the shooting screen shows the angle (180°) |
| M2 | Shutter Mode → **Speed** | The screen shows a shutter speed again, at once |
| M3 | Set Angle, power off and on | Still Angle |
| M4 | On stills, open the same page | Whether Shutter Mode shows, greyed out or not |

| Test | Do this | Check |
| --- | --- | --- |
| A1 | Movie, **M**, turn the dial assigned to shutter | The angle changes (180° → 172.8°, 150° …) |
| A2 | Set **90°** and then **360°** at the same ISO and aperture, record 5 s of each | 360° is clearly brighter (two stops more than 90°), with more motion blur |
| A3 | Check both clips' metadata (exposure time) | 1/96 s at 90° and 1/24 s at 360° at 24p |
| A4 | Flexible Exposure, Tv on Manual, turn the control wheel | As A1 |
| A5 | Switch to stills and back to movie | The angle is kept; stills shutter speed unaffected |
| A6 | Speed mode (0.2: the menu; 0.1: `bk w 0x02cf1702 00`, cold boot) | Shutter speed works as on Beta 0.1 |

| Result | Next |
| --- | --- |
| M1–M3 and A1–A3 pass | Promote to beta: the menu item and the dial work |
| M1 fails: no item, or "currently disabled" | Read `angle.item` / `angle.option`: another check hides it (the 0x1c14e6c filter, or the row flags) |
| The trace shows `angle.drive` but the value doesn't change | A second block after the drive (event `0x3fb8` handling): trace it |
| The value changes but the exposure doesn't follow | The angle is display-only on this body: stop |
| Still `angle.refused`, or no `angle.gate` with the byte set | The dial uses another path: read the trace for which |

## Open questions (camera only)

- Is the availability byte set in M, or only in P/A?
- Does the drive change `0x02cf1704`, and does the exposure follow it?
- Does the control wheel use one of the four `0x2364xxx` call sites?
- Does Shutter Mode show on stills, and greyed out or not?
- Does the menu change take effect at once on the shooting screen?
