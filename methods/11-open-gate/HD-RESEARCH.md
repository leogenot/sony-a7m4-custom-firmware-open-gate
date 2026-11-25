# Open gate for 1080p: research (2026-11-24, offline)

> Research notes for later work, not a feature of this build. Nothing here has been tried on a camera.


Question: could the open-gate build also record HD at 3:2 (1920x1280)?

**Answer: probably yes for HD 24/25/30p, but it is new reverse-engineering work, not a setting.** One read-only camera
probe decides how much work. Higher HD frame rates (50p and up) look much harder.

## How 4K open gate is built (what HD would need to copy)

From the kit's designs at `2f2a021` (`windows/tools/modding/designs`) and the runner's notes:

| Step | 4K design | What it changes | Keyed by |
| --- | --- | --- | --- |
| 1 | `dmmwb4688_v1` | raw write buffer 4,688 rows | memory manager, all modes |
| 2 | `ogsq_v3` / `ogsq880_v1` | 13 sensor/front-end sites + 26 bytes of the stored record: readout 7032x3958 -> 7032x4688, sensor register column 6 -> 5 | **drive mode** `0x8c0` (NTSC) / `0x880` (PAL) |
| 3 | `outsz2560_v1` | output size 3840x2160 -> 3840x2560, RAM `0x143c7bc` | movie size table, **size class 3** (4K) |
| 4 | `ogfile2560_hs_s_v2` | 29 av-cam descriptors + their profiles (height), a unit class, a clip-format row; libmpr `g_videoParamComb` rows 0/2/5/7 and the MP4 size instructions | **4K** descriptors, profiles and comb rows |
| 5 | `lcdfill_v2` | rear screen 3:2 fill | display, all modes |

## Findings in v0.1.3's files

**Drive modes** (`cpapp-b.bin`, list at CP `0xc98330`, 30 ids; rows of the tables step 2 edits, index 10 = `0x8c0`):

| Modes | Sensor column | Stage (W x H) | Readout length (line periods) | Reading |
| --- | --- | --- | --- | --- |
| `0x880`, `0x8c0` | 6 | 7008 x 3942 | 2011 | FF 4K 25p / 24p-30p, 7K oversampled |
| `0xa000`, `0xa100` | 4, 5 | 7008 x 4672 | 0 | full sensor 3:2 (step 2 borrows column 5) |
| `0xae0`, `0xb20`, `0xbc0` | 7, 8, 8 | 2334 x 1314 | 682 | FF, 3x binned: fast readout, likely FF HD high frame rates |
| `0x821`, `0x8c1`, `0xb21`, `0xbc1` | 14 | 4672 x 2628 | 1356 | Super 35 |
| `0xae1` | 15 | 1558 x 878 | 467 | Super 35, binned |
| `0x5002` | 2 | 3504 x 2336 | 1195 | full sensor 3:2, 2x binned (not a movie mode) |

**Encoder** (`av-cam.bin`): the descriptor table (606 entries, stride 0x90, height of #406 at `0x391e1c`) holds 221
HD (1920x1080) descriptors next to 201 4K ones; it is multi-model, so the A7 IV's HD subset must be identified, as the
kit did for its 29 4K ones. The unit-class table (`0x38389c..`) has per-class memory ids like those step 4 remaps.

**MP4 writer** (`libmpr.so`): `g_videoParamComb` has HD rows (1, 3, 6, 8, 10 = 1920x1080) beside the 4K rows the
kit edits (0, 2, 5, 7), so the libmpr part maps one to one.

**H.264 level:** 1920x1280 is 9,600 macroblocks, over the 8,192 of Levels 4.0-4.2 that HD profiles normally use, so
the HD profiles need Level 5.0 or higher (the kit already raises levels for 5.7K: `A2-level`, 150 -> 183).

Public sources confirm only the 4K readouts (FF 4K 24/30p 7K-oversampled, 4K 60p Super 35, no binning:
[PetaPixel](https://petapixel.com/2021/10/21/sony-alpha-7-iv-review-the-best-camera-sony-has-ever-made-almost/),
[Sony](https://www.sony.co.in/interchangeable-lens-cameras/products/ilce-7m4/feature61)); none say how FF HD is read.

## The deciding unknown: which drive mode HD uses

- **A. HD 24/25/30p runs on `0x8c0` / `0x880`** (the 4K drive modes, scaled to 1080): step 2 already opens them, so
  HD needs only new steps 3 and 4: the HD size-class entry (1920x1080 -> 1920x1280) and an HD encoder design (the A7
  IV's XAVC S HD descriptors and profiles: height 1080 -> 1280, level -> 5.x, unit class if 1920x1280 overflows the HD
  buffers, a clip-format row, libmpr HD comb rows and MP4 sizes). Moderate work; same methods as the kit's 4K.
- **B. HD runs on the binned modes** (`0xae0`/`0xb20`/`0xbc0`, 2334x1314): step 2 has no full-height binned readout
  to borrow (the 4K step borrows column 5, a real full-sensor mode). The only 3:2 binned readout is `0x5002`'s 2x
  binning (3504x2336), whose timing would cap the frame rate well below the binned modes'. New sensor work, high risk;
  HD 50p and up likely out.

**Probe (read-only, camera):** with open gate Off, set XAVC S HD 24p (then 25p on PAL, 60p, 120p), turn the dial to
movie and run the kit's read-only levels 8 and 15 (`ct6_level1_run.py --level 8`, `--level 15`): level 8 reports the
drive-mode id and its readout / crop / front end / size source; level 15 the movie size table and the output's size
class. That decides A or B per frame rate.

## Risk to note now

The kit warns that while open gate is applied, switching to a format that isn't set up (it names HD and Super 35) "can
stop the camera responding; it then needs a cold boot". This build keeps the mode across power-offs, so the docs
should say: set Mode to Off before recording HD, Super 35 or other frame rates. (Off works from stills, so it is
recoverable.)

## Probe build (2026-11-25): `alpha-hd-probe-0.1`

The kit's levels 8 and 15 aren't in its public repo, and its runner only accepts a camera showing stock 6.02. So
the probe is a firmware instead: Beta 0.1 plus a read-only log of the coprocessor working state the kit's level 8
copies (its `ct6_level1_run.py`, lines 836–849), at every shooting-screen start, on stills and movie, whenever it
changes:

| Trace line | Address | Values (each `WWWWHHHH` in hex) |
| --- | --- | --- |
| `hd.window` | `0x011d1194` | readout, crop, front end, crop start |
| `hd.mode` | `0x011d11ac` | readout mode id, size source, NTSC/PAL family |
| `hd.scaler` | `0x0145091c` | second scaler input, window start, window size, the four fractions |

The addresses are stock 6.02's; v0.1.3's coprocessor program differs (`cebf825e…` against `7b9b137d…`). The kit's
expected state for 4K 3:2 (`ogsq_v3`: readout 7032x4688, crop 7008x4672, front end 4336x2438, size source 3840x2160,
mode `0x8c0` / `0x880`) is the self-check: if the log shows it with 4K 3:2 on, the other lines can be believed.

### Camera steps

For each format: set it in movie mode, close the menu, open and close the menu once more (the state is read when the
shooting screen comes back, so the second time shows the settled state), record 3 s.

1. Self-check: open gate **4K 3:2**, XAVC HS 4K 24p (PAL: XAVC S 4K 25p).
2. Open gate **Off**, XAVC S 4K 24p (PAL: 25p): stock 4K.
3. Open gate **Off**, XAVC S HD 24p (PAL: 25p), then 30p.
4. Open gate **Off**, XAVC S HD 60p (PAL: 50p).

Then read the log back (`og_read_diag.py`).

### What the answer means

| `hd.mode` for HD | Route |
| --- | --- |
| `0x8c0` / `0x880` (as 4K) | A: HD scales the 4K readout. 1080p 3:2 needs only new HD size, encoder and MP4 changes |
| `0xae0` / `0xb20` / `0xbc0` (binned) | B: HD reads a binned readout; 3:2 needs a full-height binned readout (new sensor work) |

**60p:** the 7K full readout takes too long per frame for 60p (that's why 4K 60p is Super 35), so HD 60p is
expected to be on a binned mode (route B) even if HD 24–30p is route A. The probe shows it either way.
