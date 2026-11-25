# Cinema-feature roadmap

Last updated 2026-11-25. Features for this firmware, with where each one stands and what decides the next step. The
research behind them is in [sony-a7iv-feature-research](https://github.com/leogenot/sony-a7iv-feature-research). Evidence grades (A–E) are the
[feature inventory](https://github.com/leogenot/sony-a7iv-feature-research/blob/main/docs/FEATURE_INVENTORY.md)'s.

## Shipped

| Feature | Release | Notes |
| --- | --- | --- |
| Open gate 4K 3:2 (3840x2560) and 5.7K 3:2 (5760x3840), NTSC and PAL | [`beta-0.1`](https://github.com/leogenot/sony-a7m4-custom-firmware-open-gate/releases/tag/beta-0.1) | Camera-tested. Chosen with the switch on stills. |

## Test builds waiting for a camera run

| Feature | Release | What the test decides |
| --- | --- | --- |
| English Custom LUT screens | [`alpha-0.1`](https://github.com/leogenot/sony-a7m4-custom-firmware-open-gate/releases/tag/alpha-0.1) | Whether long English labels fit on screen |
| Open gate chosen and applied on movie | [`alpha-movie-0.1`](https://github.com/leogenot/sony-a7m4-custom-firmware-open-gate/releases/tag/alpha-movie-0.1) | Whether a movie-mode apply takes effect after the menu closes, needs a pipeline restart, or freezes |
| Shutter angle on the dial | [`alpha-angle-0.1`](https://github.com/leogenot/sony-a7m4-custom-firmware-open-gate/releases/tag/alpha-angle-0.1) | Whether lifting the availability gate `0x293d634` is enough, and whether the exposure follows the angle. Also the pilot for unlocking other FX3-only code. |
| XAVC S-I DCI 4K in File Format, Rec Frame Rate 24p/30p (NTSC), 25p (PAL) | [`alpha-dci-0.1`](https://github.com/leogenot/sony-a7m4-custom-firmware-open-gate/releases/tag/alpha-dci-0.1) | Whether unhiding Sony's DCI item (42) and options (141, 202–207) in the menu lookups is enough, and whether the pipeline records 4096x2160 |
| Open gate in File Format ("4K 3:2 Open Gate", "5.7K 3:2 Open Gate"), Rec Frame Rate limited to 24p / 25p | [`alpha-formats-0.1`](https://github.com/leogenot/sony-a7m4-custom-firmware-open-gate/releases/tag/alpha-formats-0.1) | Whether the reused File Format rows show and whether applying open gate during the format change works on movie |
| 1080p 3:2 open gate (1920x1280) and HD 60p: read-only probe | [`alpha-hd-probe-0.1`](https://github.com/leogenot/sony-a7m4-custom-firmware-open-gate/releases/tag/alpha-hd-probe-0.1) | Which sensor readout HD 24/30/60p uses: route A (the 4K readout, scaled) or route B (binned, new sensor work) |

## Candidates

There are two routes. **Unlock** means making Sony's own compiled code reachable, where an availability rule or the
A7 IV's menu catalogue leaves it out (the shutter-angle pattern). **Build** means our own implementation from parts
the A7 IV already has, as Erlla did for Custom LUTs.

| Feature | Grade | Route | What's missing | Next step |
| --- | --- | --- | --- | --- |
| Anamorphic de-squeeze display (1.3x, 2.0x) | B | Unlock | Proof that the A7 IV display honours `0x02cf1bf2`. Sony ships it on FX3 (3.00) and FX30 (2.00) only; the kit's C3 cycle and Sony's Monitor & Control app already de-squeeze for the A7 IV outside its menu. | One-byte camera test ([De-Squeeze](https://github.com/leogenot/sony-a7iv-feature-research/blob/main/docs/DE_SQUEEZE.md)); then a menu item. Pairs with open gate. |
| Shutter Mode menu (Speed / Angle) | A (display) | Unlock + build | The live setter for the shutter mode | After `alpha-angle-0.1` passes |
| 1080p 3:2 open gate, 24/25/30p | — | Build | HD size, encoder and MP4 changes (route A) | After `alpha-hd-probe-0.1` |
| Monitoring-only User LUT (`.cube` on screen while recording S-Log3) | D (Sony's), build possible | Build | A user table in Gamma Display Assist's display-only path | Find that path's table |
| Cine EI (sensor at base ISO, exposure index on the monitor only, EI in metadata) | D | Unlock, else build | Sony's code runs only for 14 other product classes; otherwise the same display path as the monitoring LUT. Precedent: FireHydrant unlocked Cine EI on the a6700 by opening three gates. | After the shutter-angle pilot |
| Log shooting mode and Flexible ISO | D | Unlock, else build (mostly UI) | A7 IV already changes ISO freely in S-Log3 | After the shutter-angle pilot |
| In-camera time-lapse movie | open lead | Unlock | Whether the A7 IV can reach `timelapse_movie_4k`. Sony only ships it at launch on newer bodies (a6700, a7C II, a7 V …), never added by firmware. | Trace its consumers |
| Big6-style main screen, waveform, false colour | — | Build | New screens drawn with Sony's internal drawing system. Monitor & Control (phone app) already gives the A7 IV waveform, vectorscope and false colour. | Large; after the cheaper items |
| Binned full-frame 60p below 4K (e.g. 3.5K, 3504 wide) | — | Build | A 2x2-binned readout exists as a stills mode (`0x5002`); measurements say FF HD 120p reads the full height in 6.5 ms. Erlla reports working on "3.5K60 in 2x2 binning". | Watch Erlla's work |

## Planned: an all-in-one tool

One app to choose features, build the package, unlock the camera, and read its logs. Most pieces exist as separate
scripts; the blocker is making features combinable.

| Part | What exists | Work |
| --- | --- | --- |
| Choose features, build `BODYDATA.DAT` | `python -m a7m4.open_gate build`, reproducible from public inputs (Erlla's v0.1.3 package, the kit's designs, Zig, the published key) | Download and hash-check the inputs, build on the user's computer; no prebuilt combinations |
| The 10-minute unlock | Erlla's M4 Camera Tool ([`no-fastboot-install/`](no-fastboot-install/README.en.md)) and the Mac port ZM4 | Run that step from the tool over USB (Windows needs the USB driver Erlla's tool installs; a Mac needs the admin password) |
| Update the camera | Copy `BODYDATA.DAT` to the card, run Software Update on the camera | Copy the file and guide the camera steps |
| Logs | [`tools/og_read_diag.py`](https://github.com/leogenot/sony-a7m4-custom-firmware-open-gate/blob/alpha-movie/tools/og_read_diag.py) on the alpha test branches (a debug script), over the kit's read-only USB session | Bring it to `main`, then a button around it |

**Prerequisites** (before the tool):

- **Code space.** All our code shares one 48 KB block in `appFw.so`, nearly full (open gate alone about 45 KB; the
  movie-mode build has 4 bytes free). Combining features needs a second free region, like the one Erlla uses for the
  LUT screens.
- **Feature switches.** Features share hook sites (DCI and the File Format formats both hook the option lookup at
  `0x1c18004`), so the module needs build-time switches and one combined handler per site.

**Safety:** every combination is a firmware nobody has run. Offer camera-tested features by default, put untested
ones behind an "experimental" switch, and block combinations advised against (DCI with open gate on: open gate's sensor changes are made for 4K and 5.7K).

**Order:**

1. Finish the camera tests of the alpha builds.
2. Merge the passing features into one build with feature switches, and find the extra code space.
3. A command-line tool: `a7m4 build --features …`, `a7m4 unlock`, `a7m4 logs`, reusing the existing Python.
4. An app with checkboxes and buttons on top of it, packaged for Mac and Windows.

## Not possible on this body

| Feature | Why |
| --- | --- |
| Full-frame 4K 60p | Measured: FF 4K readout 26.7–27.4 ms, S35 4K 60p about 13 ms. Even at the fastest line clock the camera uses (about 9.56 µs), the 7K readout's 2011 line periods take about 19.2 ms, over the 16.7 ms frame; at FF's own clock it is 1.6x too slow. It would also need about 40% more pixel throughput than any measured mode. |
| RAW over HDMI | Needs the hardware route; Sony enables it only on other product classes |
| Built-in variable ND | Needs ND hardware |
| Base ISO Low/High switch | Sony claims no dual base ISO. DXOMARK puts the stills dual-gain step at ISO 400; an S-Log3 equivalent near ISO 3200 is inferred, not measured. A manual switch would need control of that gain step. |
| Auto framing | Sony ties it to the AI processing unit, which the A7 IV lacks |

## Web research

[Cinema features: web research](https://github.com/leogenot/sony-a7iv-feature-research/blob/main/docs/CINEMA_FEATURES_WEB_RESEARCH.md) (2026-11-25) has the sources. In short:

- **Readout times** (CineD, DPReview, Horshack): FF 4K 26.7–27.4 ms, S35 4K 60p about 13 ms, FF HD 24/30/60p about
  8.9 ms, HD 120p 6.5 ms, stills e-shutter 67.6 ms. They agree with the firmware's readout table.
- **Sony never shipped** de-squeeze, shutter angle, DCI 4K, Log shooting or time-lapse movie on the A7 IV (up to
  6.02). De-squeeze, shutter angle and DCI 4K came to the FX3 and FX30 by firmware; DCI 24.00p to the a7S III by licence.
- **Precedent:** FireHydrant (a6700) unlocked compiled-in Cine EI by opening gates, replaced 1080p pixel skipping with
  oversampling, and added 6K30 and a 2K open gate.
