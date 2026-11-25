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
